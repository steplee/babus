#pragma once

#include <spdlog/spdlog.h>

#include <cstdint>
#include <string>
#include <cstring>
#include <cassert>

#include <unistd.h>

namespace {

	struct ProfileConfig {
		bool valid = false;

		struct {
			bool useTcp = false;
		} redis;

		std::size_t imageSize;
		int imuRate;

		int64_t testDuration;

		std::string title;
	};

	inline std::string getString(const char* key, const std::string& def="noTitle") {
		const char* val = getenv(key);
		if (val == nullptr) return def;
		return std::string{val};
	}

	inline bool getOn(const char* key, bool def=false) {
		const char* val = getenv(key);
		if (val == nullptr) return def;
		return strcmp(val, "1") == 0 or strncmp("val", "t", 1) == 0 or strncmp("val", "T", 1) == 0;
	}
	inline int64_t getInt(const char* key, int64_t def) {
		const char* val = getenv(key);
		if (val == nullptr) return def;
		int64_t v;
		int n = sscanf(val, "%ld", &v);
		assert(n == 1);
		return v;
	}

	inline ProfileConfig getConfig() {
		ProfileConfig c;
		c.title = getString("title", "noTitle");

		c.redis.useTcp = getOn("redisUseTcp");

		c.imageSize = getInt("imageSize", 1920 * 1080 * 3);
		c.imuRate = getInt("imuRate", 1000);
		SPDLOG_INFO("imageSize: {}", c.imageSize);
		SPDLOG_INFO("imuRate: {}", c.imuRate);

		c.testDuration = getInt("testDuration", 30'000'000);
		SPDLOG_INFO("testDuration: {}", c.testDuration);

		c.valid = true;

		return c;
	}
}
