#include <spdlog/spdlog.h>

#include <sw/redis++/redis++.h>

#include <chrono>
#include <cassert>
#include <unistd.h>

using namespace sw::redis;



namespace {
    constexpr std::size_t BytesInOneImage = 1920 * 1080 * 3;
    // constexpr std::size_t BytesInOneImage = 1920 * 2;

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
		// return Redis("tcp://127.0.0.1:6379");
		return Redis("unix:///tmp/redis.sock");
	}

}

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
        }

        inline void loop() {
            int64_t sleepTime = 1'000'000 / frequency - 1;

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
						sum.copyLatency += getElapsedFromMessageCreation((const uint8_t*)value.data());
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
            SPDLOG_INFO("");
            SPDLOG_INFO("Running with `noImuNorMed345`={}", noImuNorMed345);
            if (!noImuNorMed345) producers.push_back(std::make_unique<Producer>("imu =>", "imu", 1000, 128));
            producers.push_back(std::make_unique<Producer>("image =>", "image", 30, BytesInOneImage));

            producers.push_back(std::make_unique<Producer>("med01 =>", "med01", 40, 356));
            producers.push_back(std::make_unique<Producer>("med02 =>", "med02", 41, 356));
            if (!noImuNorMed345) {
                producers.push_back(std::make_unique<Producer>("med03 =>", "med03", 42, 356));
                producers.push_back(std::make_unique<Producer>("med04 =>", "med04", 43, 356));
                producers.push_back(std::make_unique<Producer>("med05 =>", "med05", 44, 356));
            }

            consumers.push_back(std::make_unique<Consumer>("=> imu", std::vector<std::string> {
                                                                         "control",
                                                                         "imu",
                                                                     }));
            consumers.push_back(std::make_unique<Consumer>("=> image", std::vector<std::string> { "control", "image" }));
            consumers.push_back(std::make_unique<Consumer>("=> image+med01", std::vector<std::string> { "control", "image", "med01" }));
            consumers.push_back(std::make_unique<Consumer>(
                "=> image+med0[1-5]", std::vector<std::string> { "control", "image", "med01", "med02", "med03", "med04", "med05" }));
            consumers.push_back(
                std::make_unique<Consumer>("=> imu+image+med0[1-5]", std::vector<std::string> { "control", "imu", "image", "med01",
                                                                                                "med02", "med03", "med04", "med05" }));
            consumers.push_back(std::make_unique<Consumer>(
                "=> imu+image+med0[1-5] & sleep(10ms)",
                std::vector<std::string> { "control", "imu", "image", "med01", "med02", "med03", "med04", "med05" }, 10000));
        }

        inline void run(int64_t duration) {
            SPDLOG_INFO("sleeping for {:.1f} seconds", duration / 1e6);
            usleep(duration);
            SPDLOG_INFO("sleeping for {:.1f} seconds ... done", duration / 1e6);
            _doStop   = true;

            auto stop = allocMessage(8 + 4);
            stop[8]   = (uint8_t)'s';
            stop[9]   = (uint8_t)'t';
            stop[10]  = (uint8_t)'o';
            stop[11]  = (uint8_t)'p';
            SPDLOG_INFO("writing stop message.");
            // _clientDomain->getSlot("control")->write(_clientDomain->ptr(), babus::ByteSpan { stop.data(), stop.size() });
        }
    };


int main() {
	auto redis = Redis("tcp://127.0.0.1:6379");

#if 0
	redis.set("key", "val1");
    auto val = redis.get("key");

	fmt::print("val: {}\n", *val);

	int64_t t0 = getMicros();
	// int64_t t0 = 'a';
	fmt::print("t0: {}\n", t0);
	set(redis, "t", t0);
	int64_t t1 = getCopy<int64_t>(redis, "t");
	int64_t t2 = getMicros();
	fmt::print("t1: {}\n", t1);
	fmt::print("latency: {}us\n", t2-t1);

	{
		AppMessage<SmallData> am (SmallData{"hello"});
		fmt::print("smalldata latency0: {}us\n", am.nowDt());
		set(redis, "hello", am);
		auto am2 = getCopy<decltype(am)>(redis, "hello");
		fmt::print("smalldata latency1: {}us\n", am2.nowDt());
	}

	//
	// I see about 100-200 micros for the memcpy, and about 3.2 ms for the redis set/get.
	//

	std::mutex mtx;
	for (int i=0; i<10; i++)
	{
		AppMessage<LargeData> am (LargeData{});
		memset((void*)am.inner.text, 0, sizeof(LargeData));
		sched_yield();
		usleep(1);
		sched_yield();
		mtx.lock();
		AppMessage<LargeData> am2 (LargeData{});
		for (int j=0; j<1; j++) {
			memcpy(&am2, &am, sizeof(decltype(am)));
			((volatile char*)am2.inner.text)[0] = 0;
		}
		mtx.unlock();
		fmt::print("largedata lock+memcpy latency1: {}us\n", am2.nowDt());
	}

	for (int i=0; i<10; i++)
	{
		AppMessage<LargeData> am (LargeData{});
		// fmt::print("largedata latency0: {}us\n", am.nowDt());
		set(redis, "hello2", am);
		int64_t t1 = getMicros();
		auto am2 = getCopy<decltype(am)>(redis, "hello2");
		fmt::print("largedata latency1: {}us, {}us readonly\n", am2.nowDt(), getMicros()-t1);
	}


	{
		using Attrs = std::vector<std::pair<std::string, std::string>>;
		Attrs attrs = { {"f1", "v1"}, {"f2", "v2"} };
		auto id = redis.xadd("strm1", "*", attrs.begin(), attrs.end());
		fmt::print(" - xadd id = {}\n", id);

		using Item = std::pair<std::string, Optional<Attrs>>;
		using ItemStream = std::vector<Item>;

		// std::unordered_map<std::string, ItemStream> result;
		std::vector<std::pair<std::string, ItemStream>> result;
		redis.xread("strm1", "0", std::chrono::seconds(1), 1, std::inserter(result, result.end()));
		fmt::print(" - result.size() = {}\n", result.size());
		// Yikes.
		fmt::print(" - dequed from strm1: (key={}) (nmsg={}) (id={}) (field0={} value0={})\n", result[0].first, result[0].second.size(), result[0].second[0].first,
				result[0].second[0].second->operator[](0).first,
				result[0].second[0].second->operator[](0).second);

		// fmt::print(" - dequed from strm1: (key={}) (val={})\n", result[0].first);

		//// std::unordered_map<std::string, std::string> keys = { {"strm1", id}, {"another-key", "0-0"} };
		// std::vector<std::pair<std::string, std::string>> keys = { {"strm1", id}, {"another-key", "0-0"} };
		// redis.xread(keys.begin(), keys.end(), 10, std::inserter(result, result.end()));
	}
#endif
    spdlog::set_level(spdlog::level::trace);
    // spdlog::set_level(spdlog::level::debug);
    // spdlog::set_level(spdlog::level::info);


	static constexpr int64_t testDuration = 30'000'000;
    App app(false);
	app.run(testDuration);


	return 0;
}

