#pragma once

#include <vector>
#include <utility>
#include <stdexcept>
#include <algorithm>
#include <cstring>

#include <spdlog/spdlog.h>

namespace babus {

	//
	// Like unordered_map, but no hashing. Instead linear search is used.
	//
	// TODO:
	// I wanted this to work with const char*, but there's no way to enforce the pointer
	// to point to a region of *static* lifetime. So that leads to another good idea:
	// Have an implementation of this class that always stores std::string internally, but can
	// take const char* as insert/find/erase arguments. That avoids extraneous std::string constructions,
	// which happens now.
	//

	template <class K, class V>
	class SmallMap {

		private:
			using Pair = std::pair<K,V>;
			using Vec = std::vector<Pair>;

		public:
			inline SmallMap() {};

			inline typename Vec::iterator begin() { return items.begin(); }
			inline typename Vec::iterator   end() { return items.end(); }
			inline typename Vec::const_iterator begin() const { return items.begin(); }
			inline typename Vec::const_iterator   end() const { return items.end(); }
			inline size_t size() const { return items.size(); }

			inline bool keyIsSame(const K& a, const K& b) {
				// WARNING: This requires user to ensure stored char* pointers have proper lifetime (probably static).
				if constexpr (std::is_convertible_v<K, const char*>) {
					// SPDLOG_INFO("cmp {} == {}", (void*)a,(void*)b);
					return strcmp(a,b) == 0;
				}
				return a == b;
			}

			inline typename Vec::iterator find(const K& k) {
				auto it = items.begin();
				while (it != items.end()) {
					// if (it->first == k) return it;
					if (keyIsSame(it->first,k)) return it;
					it++;
				}
				return it;
			}

			inline void insert(const K& k, V&& v) {
				if (find(k) != end()) {
					throw std::runtime_error("SmallMap::insert() called but key already existed in map");
				}
				items.push_back(std::make_pair(k, std::move(v)));
			}

			inline void erase(const K& k) {
				auto it = find(k);
				if (it == end()) {
					throw std::runtime_error("erase(k) failed to find key k.");
				}
				if (auto n = size(); n > 1) {
					std::iter_swap(it, items.begin() + n-1);
				}
				items.pop_back();
			}

		private:
			Vec items;

	};

}

