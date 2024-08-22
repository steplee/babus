#include "babus/client.h"
#include "babus/domain.h"
#include "babus/waiter.h"

#include <cassert>
#include <chrono>
#include <string>
#include <vector>

#include "babus/benchmark/profileCfg.hpp"
#include "babus/detail/csv_writer.hpp"

//
// There is a producer of a small message at 1000Hz.
// There is a producer of a large message at 30Hz.
// There are 5 producers of small messages at 40Hz.
//
// There is one consumer to the small/frequent message.
// There are five consumers to all messages other than the small one.
//
// Is the consumer to the small/frequent message ever delayed?
// What's the average and max latencies across all reads and event signals?
// What's the total time held for write locks? Does it make sense to double- or N- buffer?
//

namespace {
	ProfileConfig g_cfg;
	std::unique_ptr<CsvWriter> g_csv;

    int64_t getMicros() {
        using namespace std::chrono;
        auto tp = high_resolution_clock::now();
        return duration_cast<microseconds>(tp.time_since_epoch()).count();
    }

    inline std::string fmtDuration(double micros) {
        if (micros > 1'000'000)
            return fmt::format("{:>5.1f} s", micros / 1e6);
        else if (micros > 1'000)
            return fmt::format("{:>5.1f}ms", micros / 1e3);
        else
            return fmt::format("{:>5.1f}us", micros);
    }

    using MessageBuffer = std::vector<uint8_t>;

    inline MessageBuffer allocMessage(std::size_t len) {
        assert(len > sizeof(int64_t));
        MessageBuffer out(len);
        for (std::size_t i = sizeof(int64_t); i < len; i++) out[i] = (i % 256);
        reinterpret_cast<int64_t*>(out.data())[0] = getMicros();
        return out;
    }
    inline int64_t getDuration(int64_t then) {
        return getMicros() - then;
    }
    inline int64_t getElapsedFromMessageCreation(const uint8_t* buf) {
        int64_t now  = getMicros();
        int64_t then = reinterpret_cast<const int64_t*>(buf)[0];
        // SPDLOG_INFO("now {} then {} diff {}", now, then, now-then);
        return now - then;
    }
}

namespace {

    // Use a few globals because otherwise the code bloats up with irrelevant details.
    volatile bool _doStop              = false;
    babus::ClientDomain* _clientDomain = 0;

    struct Producer {
        std::string name;
        std::string slotName;
        int frequency;
        std::size_t msgSize;
        std::thread thread;

        struct {
            int64_t writeLatency = 0;
            int64_t n            = 0;
        } sum;

        inline Producer(std::string name, std::string slotName, int frequency, std::size_t msgSize)
            : name(name)
            , slotName(slotName)
            , frequency(frequency)
            , msgSize(msgSize)
            , thread(&Producer::loop, this) {
        }

        inline Producer(Producer&& o)
            : name(std::move(o.name))
            , slotName(std::move(o.slotName))
            , frequency(std::move(o.frequency))
            , msgSize(std::move(o.msgSize))
            , thread(std::move(o.thread)) {
        }

        inline Producer& operator=(Producer&& o) {
            name      = std::move(o.name);
            slotName  = std::move(o.slotName);
            frequency = std::move(o.frequency);
            msgSize   = std::move(o.msgSize);
            thread    = std::move(o.thread);
            return *this;
        }

        inline ~Producer() {
            // SPDLOG_TRACE("Producer '{}' dtor, waiting for join", name);
            thread.join();
            // SPDLOG_TRACE("Producer '{}' dtor, waiting for join ... done", name);
            SPDLOG_INFO("Producer '{:>40}' avg latency of write       : {} (n {:>12L})", name,
                        fmtDuration(static_cast<double>(sum.writeLatency) / sum.n), sum.n);
			auto lck = g_csv->lock();
			g_csv->set("name", std::string(name));
			g_csv->set("type", "producer");
			g_csv->set("totalTime", fmt::format("{:.8f}", sum.writeLatency * 1e-6));
			g_csv->set("n", fmt::format("{}", sum.n));
			g_csv->finishRow();
        }

        inline void loop() {
            while (_clientDomain == nullptr) usleep(1'000);
            int64_t sleepTime = 1'000'000 / frequency - 1;

            auto& clientSlot  = _clientDomain->getSlot(slotName.c_str());

            while (!_doStop) {
                usleep(sleepTime);

                auto msg             = allocMessage(msgSize);
                int64_t startOfWrite = getMicros();
                clientSlot.write({ msg.data(), msg.size() });
                sum.writeLatency += getDuration(startOfWrite);
                sum.n++;
            }
        }
    };

    struct Consumer {
        std::string name;
        std::vector<std::string> slotNames;
        int sleepTime;
        std::thread thread;

        struct {
            int64_t viewLatency = 0;
            int64_t copyLatency = 0;
            int64_t n           = 0;
        } sum;

        inline Consumer(std::string name, const std::vector<std::string>& slotNames, int sleepTime = 0)
            : name(name)
            , slotNames(slotNames)
            , sleepTime(sleepTime)
            , thread(&Consumer::loop, this) {
        }

        inline Consumer(Consumer&& o)
            : name(std::move(o.name))
            , slotNames(std::move(o.slotNames))
            , sleepTime(std::move(o.sleepTime))
            , thread(std::move(o.thread)) {
        }

        inline ~Consumer() {
            // SPDLOG_TRACE("Consumer '{}' dtor, waiting for join", name);
            thread.join();
            // SPDLOG_TRACE("Consumer '{}' dtor, waiting for join ... done", name);

            SPDLOG_INFO("Consumer '{:>40}' avg latency of view        : {} (n {:>12L})", name,
                        fmtDuration(static_cast<double>(sum.viewLatency) / sum.n), sum.n);
            SPDLOG_INFO("Consumer '{:>40}' avg latency of view + copy : {} (n {:>12L})", name,
                        fmtDuration(static_cast<double>(sum.copyLatency) / sum.n), sum.n);

			auto lck = g_csv->lock();
			g_csv->set("name", std::string(name));
			g_csv->set("type", "consumer");
			g_csv->set("totalTime", fmt::format("{:.8f}", sum.copyLatency * 1e-6));
			g_csv->set("viewTime", fmt::format("{:.8f}", sum.viewLatency * 1e-6));
			g_csv->set("n", fmt::format("{}", sum.n));
			g_csv->finishRow();
        }

        inline void loop() {
            std::vector<babus::ClientSlot*> slots;

            for (const auto& slotName : slotNames) {
                slots.push_back(&_clientDomain->getSlot(slotName.c_str()));
                assert(slots.back() != nullptr);
                assert(slots.back()->ptr() != nullptr);
            }

            babus::Waiter waiter(_clientDomain->ptr());
            for (auto& slotPtr : slots) {
                assert(slotPtr != nullptr);
                assert(slotPtr->ptr() != nullptr);
                waiter.subscribeTo(slotPtr->ptr());
            }

            while (!_doStop) {

                waiter.waitExclusive();
                waiter.forEachNewSlot([&](babus::LockedView&& view) {
                    sum.viewLatency += getElapsedFromMessageCreation((const uint8_t*)view.span.ptr);
                    auto msg = view.cloneBytes();
                    sum.copyLatency += getElapsedFromMessageCreation((const uint8_t*)msg.data());
                    sum.n++;
                });

                /*
                // Without a sleep or sched_yield, the loop can spin and never check _doStop / never exit.
                if (sleepTime > 0) usleep(sleepTime);
                else sched_yield();
                */
            }
        }
    };

    struct App {
        std::vector<std::unique_ptr<Producer>> producers;
        std::vector<std::unique_ptr<Consumer>> consumers;

        inline App(bool noImuNorMed345) {
            SPDLOG_INFO("");
            SPDLOG_INFO("Running with `noImuNorMed345`={}", noImuNorMed345);
            if (!noImuNorMed345) producers.push_back(std::make_unique<Producer>("imu", "imu", g_cfg.imuRate, 128));
            producers.push_back(std::make_unique<Producer>("image", "image", 30, g_cfg.imageSize));

            producers.push_back(std::make_unique<Producer>("med01", "med01", 40, 356));
            producers.push_back(std::make_unique<Producer>("med02", "med02", 41, 356));
            if (!noImuNorMed345) {
                producers.push_back(std::make_unique<Producer>("med03", "med03", 42, 356));
                producers.push_back(std::make_unique<Producer>("med04", "med04", 43, 356));
                producers.push_back(std::make_unique<Producer>("med05", "med05", 44, 356));
            }

            consumers.push_back(std::make_unique<Consumer>("imu", std::vector<std::string> {
                                                                         "control",
                                                                         "imu",
                                                                     }));
            consumers.push_back(std::make_unique<Consumer>("image", std::vector<std::string> { "control", "image" }));
            consumers.push_back(std::make_unique<Consumer>("image+med01", std::vector<std::string> { "control", "image", "med01" }));
            consumers.push_back(std::make_unique<Consumer>(
                "image+med0[1-5]", std::vector<std::string> { "control", "image", "med01", "med02", "med03", "med04", "med05" }));
            consumers.push_back(
                std::make_unique<Consumer>("imu+image+med0[1-5]", std::vector<std::string> { "control", "imu", "image", "med01",
                                                                                                "med02", "med03", "med04", "med05" }));
            consumers.push_back(std::make_unique<Consumer>(
                "imu+image+med0[1-5] & sleep(10ms)",
                std::vector<std::string> { "control", "imu", "image", "med01", "med02", "med03", "med04", "med05" }, 10000));
        }

        inline void run(int64_t duration) {
            SPDLOG_DEBUG("sleeping for {:.1f} seconds", duration / 1e6);
            usleep(duration);
            SPDLOG_DEBUG("sleeping for {:.1f} seconds ... done", duration / 1e6);
            _doStop   = true;

            auto stop = allocMessage(8 + 4);
            stop[8]   = (uint8_t)'s';
            stop[9]   = (uint8_t)'t';
            stop[10]  = (uint8_t)'o';
            stop[11]  = (uint8_t)'p';
            SPDLOG_DEBUG("writing stop message.");
            _clientDomain->getSlot("control")->write(_clientDomain->ptr(), babus::ByteSpan { stop.data(), stop.size() });
        }
    };

}

using namespace babus;

template <class... Args> void run_app(Args&&... args) {
    _doStop       = false;
    _clientDomain = new ClientDomain(ClientDomain::openOrCreate("profileAppDomain"));

    {
        App app(args...);
        app.run(g_cfg.testDuration);
    }

    delete _clientDomain;
    _clientDomain = 0;
}

int main() {

    // spdlog::set_level(spdlog::level::trace);
    // spdlog::set_level(spdlog::level::debug);
    // spdlog::set_level(spdlog::level::info);

	g_cfg = getConfig();
	assert(g_cfg.valid);

	g_csv = std::make_unique<CsvWriter>(g_cfg.title + std::string{".csv"}, std::vector<std::string>{"name", "type", "n", "totalTime", "viewTime"});

    run_app(false);
    // run_app(true);

    return 0;
}
