#include <spdlog/spdlog.h>

#include <sw/redis++/redis++.h>

#include <chrono>
#include <cassert>
#include <unistd.h>

using namespace sw::redis;

int64_t getMicros() {
	auto t = std::chrono::high_resolution_clock::now().time_since_epoch();
	return std::chrono::duration_cast<std::chrono::microseconds>(t).count();
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

template <class M>
struct AppMessage {
	inline AppMessage(const M& m) : tstamp(getMicros()), inner(m) {}
	inline AppMessage(M&& m) : tstamp(getMicros()), inner(std::move(m)) {}

	inline operator M&() { return inner; }
	inline operator const M&() const { return inner; }

	int64_t tstamp;
	M inner;

	int64_t nowDt() const { return getMicros() - tstamp; }
};

struct SmallData {
	char text[64];
};

struct LargeData {
	// char text[1024*1024*3];
	char text[512*512];
};



int main() {
	auto redis = Redis("tcp://127.0.0.1:6379");

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


	return 0;
}


