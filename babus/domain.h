#pragma once

#include "babus/common.h"
#include "detail/rw_mutex.hpp"
#include "detail/sequence_counter.hpp"
#include "detail/small_map.hpp"
#include "fs/mmap.h"

#include "babus/slot/slotPtr.h"

#include <mutex>

#include <spdlog/spdlog.h>

namespace babus {

    struct Slot;

    struct LockedView {
        ByteSpan span;
        RwMutexReadLockGuard lck;
        // Slot* slot = nullptr;
		// SlotPtr slotPtr;
		void* ptr = nullptr;

        inline std::vector<uint8_t> cloneBytes() const {
            std::vector<uint8_t> out;
            out.resize(span.len);
            memcpy(out.data(), span.ptr, span.len);
            return out;
        }
    };

    struct Domain {

    public:
        std::array<char, 4> magic = DomainMagic;
        RwMutex slotMtx; // actually I don't think I need this.
        SequenceCounter seq;
        std::size_t slotFileSizes;
        char name[MaxNameLength] = { 0 };
		int numSlots = 64;
        // Slot* slots[64];

		void* getSlot(int i);
    };


} // namespace babus

