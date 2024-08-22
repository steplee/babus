#include <spdlog/spdlog.h>

#include <sw/redis++/redis++.h>

#include <chrono>
#include <cassert>
#include <unistd.h>

#include "babus/benchmark/profileCfg.hpp"
#include "babus/detail/csv_writer.hpp"

using namespace sw::redis;



namespace {
	ProfileConfig g_cfg;
	std::unique_ptr<CsvWriter> g_csv;

    volatile bool _doStop              = false;

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

    inline std::string allocMessage(std::size_t len) {
        assert(len > sizeof(int64_t));
		std::string out;
		out.resize(len);
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



	template <class T>
	auto set(Redis& redis, const StringView& k, const T& t) -> bool {
		return redis.set(k, StringView{reinterpret_cast<const char*>(&t), sizeof(T)});
	}

	template <class T>
	T getCopy(Redis& redis, const StringView& k) {
		auto v = redis.get(k);
		assert(v.has_value());
		assert(v->length() == sizeof(T));
		return T(*reinterpret_cast<const T*>(v->data()));
	}

	Redis getRedis() {
		if (g_cfg.redis.useTcp)
			return Redis("tcp://127.0.0.1:6379");
		else
			return Redis("unix:///tmp/redis.sock");
	}

}


namespace {
    struct Producer {
        std::string name;
        std::string slotName;
        int frequency;
        std::size_t msgSize;
		Redis redis;
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
			, redis(getRedis())
            , thread(&Producer::loop, this)
		{
        }

        inline Producer(Producer&& o)
            : name(std::move(o.name))
            , slotName(std::move(o.slotName))
            , frequency(std::move(o.frequency))
            , msgSize(std::move(o.msgSize))
            , redis(std::move(o.redis))
            , thread(std::move(o.thread)) {
        }

        inline Producer& operator=(Producer&& o) {
            name      = std::move(o.name);
            slotName  = std::move(o.slotName);
            frequency = std::move(o.frequency);
            msgSize   = std::move(o.msgSize);
            thread    = std::move(o.thread);
            redis     = std::move(o.redis);
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
            int64_t sleepTime = 1'000'000 / frequency - 1;

			redis.xtrim(slotName, 0);

            while (!_doStop) {
                usleep(sleepTime);

                auto msg             = allocMessage(msgSize);
                int64_t startOfWrite = getMicros();

                // clientSlot.write({ msg.data(), msg.size() });
				using Attrs = std::vector<std::pair<std::string, std::string>>;
				Attrs attrs { {"msg", std::move(msg)} };

				// SPDLOG_INFO("xadd to '{}' len {}", slotName, attrs[0].second.size());
				redis.xadd(slotName, "*", attrs.begin(), attrs.end());
				redis.xtrim(slotName, 1); // TODO: Pipeline

                sum.writeLatency += getDuration(startOfWrite);
                sum.n++;
            }
        }
    };

    struct Consumer {
        std::string name;
        std::vector<std::string> slotNames;
        int sleepTime;
		Redis redis;
        std::thread thread;

        struct {
            // int64_t viewLatency = 0;
            int64_t copyLatency = 0;
            int64_t n           = 0;
        } sum;

        inline Consumer(std::string name, const std::vector<std::string>& slotNames, int sleepTime = 0)
            : name(name)
            , slotNames(slotNames)
            , sleepTime(sleepTime)
			, redis(getRedis())
            , thread(&Consumer::loop, this)
		{
        }

        inline Consumer(Consumer&& o)
            : name(std::move(o.name))
            , slotNames(std::move(o.slotNames))
            , sleepTime(std::move(o.sleepTime))
            , redis(std::move(o.redis))
            , thread(std::move(o.thread))
		{
        }

        inline ~Consumer() {
            // SPDLOG_TRACE("Consumer '{}' dtor, waiting for join", name);
            thread.join();
            // SPDLOG_TRACE("Consumer '{}' dtor, waiting for join ... done", name);

            // SPDLOG_INFO("Consumer '{:>40}' avg latency of view        : {} (n {:>12L})", name, fmtDuration(static_cast<double>(sum.viewLatency) / sum.n), sum.n);
            SPDLOG_INFO("Consumer '{:>40}' avg latency of view + copy : {} (n {:>12L})", name,
                        fmtDuration(static_cast<double>(sum.copyLatency) / sum.n), sum.n);
			auto lck = g_csv->lock();
			g_csv->set("name", std::string(name));
			g_csv->set("type", "consumer");
			g_csv->set("totalTime", fmt::format("{:.8f}", sum.copyLatency * 1e-6));
			g_csv->set("n", fmt::format("{}", sum.n));
			g_csv->finishRow();
        }

        inline void loop() {
			using Attrs = std::vector<std::pair<std::string, std::string>>;
			using Item = std::pair<std::string, Optional<Attrs>>;
			using ItemStream = std::vector<Item>;

			// std::unordered_map<std::string, std::string> keys = { {"key", id}, {"another-key", "0-0"} };

			std::vector<std::pair<std::string, std::string>> keysAndIds;
			auto lookupKeyRef = [&](const std::string& k) -> std::pair<std::string,std::string>& {
				for (auto &kv : keysAndIds) {
					if (k == kv.first) return kv;
				}
				throw std::runtime_error("invalid key");
			};

			for (const auto& slotName : slotNames) {
				keysAndIds.push_back({slotName, "0-0"});
				// SPDLOG_INFO("consumer '{}' listening to key '{}'", name, slotName);
			}


            while (!_doStop) {

				std::vector<std::pair<std::string, ItemStream>> results;
				// SPDLOG_INFO("consumer '{}' xread");
				redis.xread(keysAndIds.begin(), keysAndIds.end(), std::chrono::seconds(10), 1, std::inserter(results, results.end()));
				// SPDLOG_INFO("consumer '{}' xread... done", name);

				for (auto& result : results) {
					const std::string& key = result.first;
					for (auto& msg : result.second) {
						const std::string& id = msg.first;
						const std::string& field = (msg.second)->operator[](0).first;
						const std::string& value = (msg.second)->operator[](0).second;

						// SPDLOG_INFO("field: {}",field);
						// SPDLOG_INFO("value: {}",*reinterpret_cast<const uint64_t*>(value.data()));

						// sum.viewLatency += getElapsedFromMessageCreation((const uint8_t*)view.span.ptr);
						auto elapsed = getElapsedFromMessageCreation((const uint8_t*)value.data());
						// if (elapsed > 1e6) SPDLOG_INFO("warning, long delay: {}s (slot {}) (n {})", elapsed * 1e-6, key, sum.n);
						sum.copyLatency += elapsed;
						sum.n++;

						// SPDLOG_INFO("set key '{}' id from '{}' to '{}' duration {}", key, lookupKeyRef(key).second, id, getElapsedFromMessageCreation((const uint8_t*)value.data()));
						lookupKeyRef(key).second = id;
					}
				}

				/*
				fmt::print(" - result.size() = {}\n", result.size());
				// Yikes.
				fmt::print(" - dequed from strm1: (key={}) (nmsg={}) (id={}) (field0={} value0={})\n", result[0].first, result[0].second.size(), result[0].second[0].first,
						result[0].second[0].second->operator[](0).first,
						result[0].second[0].second->operator[](0).second);

				*/
				}

			}
    };

    struct App {
        std::vector<std::unique_ptr<Producer>> producers;
        std::vector<std::unique_ptr<Consumer>> consumers;

        inline App(bool noImuNorMed345) {
            // SPDLOG_INFO("Running with `noImuNorMed345`={}", noImuNorMed345);
            if (!noImuNorMed345) producers.push_back(std::make_unique<Producer>("imu", "imu", g_cfg.imuRate, 128));
            producers.push_back(std::make_unique<Producer>("image", "image", 30, g_cfg.imageSize));

            producers.push_back(std::make_unique<Producer>("med01", "med01", 40, 356));
            producers.push_back(std::make_unique<Producer>("med02", "med02", 41, 356));
            if (!noImuNorMed345) {
                producers.push_back(std::make_unique<Producer>("med03", "med03", 42, 356));
                producers.push_back(std::make_unique<Producer>("med04", "med04", 43, 356));
                producers.push_back(std::make_unique<Producer>("med05", "med05", 44, 356));
            }

            usleep(5);

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
        }
    };

}

int main() {
    // spdlog::set_level(spdlog::level::trace);
    // spdlog::set_level(spdlog::level::debug);
    // spdlog::set_level(spdlog::level::info);


	g_cfg = getConfig();
	assert(g_cfg.valid);
	SPDLOG_INFO("redis using tcp: {}", g_cfg.redis.useTcp);

	g_csv = std::make_unique<CsvWriter>(g_cfg.title + std::string{".csv"}, std::vector<std::string>{"name", "type", "n", "totalTime", "viewTime"});

	static constexpr int64_t testDuration = 30'000'000;
    App app(false);
	app.run(testDuration);


	return 0;
}

