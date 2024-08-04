#pragma once

#include "babus/common.h"
#include "detail/rw_mutex.hpp"
#include "detail/sequence_counter.hpp"
#include "detail/small_map.hpp"
#include "fs/mmap.h"

#include <mutex>

#include <spdlog/spdlog.h>

namespace babus {

    struct ByteSpan {
        void* ptr  = nullptr;
        size_t len = 0;

        inline bool valid() const {
            return ptr != 0 and len > 0;
        }
        inline uint8_t& operator[](std::size_t i) {
            return reinterpret_cast<uint8_t*>(ptr)[i];
        }
        inline const uint8_t& operator[](std::size_t i) const {
            return reinterpret_cast<uint8_t*>(ptr)[i];
        }
    };

    struct Slot;

    struct LockedView {
        ByteSpan span;
        RwMutexReadLockGuard lck;
        Slot* slot = nullptr;
    };

    struct SlotFlags {
        uint64_t bits = 0;
    };

    struct Domain;

    struct Slot {
    public:
        std::array<char, 4> magic = SlotMagic;
        RwMutex mtx;
        uint32_t index = 0; // Used for event futex mask.
        SequenceCounter seq;

		// Using a ring-buffer is more complicated than I realized because it requires
		// dynamic offsets from the mmap ptr. The offsets should remain aligned to 4096, but
		// must be dynamic length because data is variable length.
		// OR specify max data size and SIMPLY treat as static size. BECAUSE we align to page size (4096)
		// I BELIEVE this does not waste ANY ram actually !?!?
		// It should be built in sparse acccess for free, wasting only virtual address space (who cares).
		//
		// uint32_t ringSize = 1; // single-buffered, double-buffered, up to N-buffered
		// std::array<uint32_t, SlotMaxRingLength> length = {0}; // current data length
		uint32_t length = 0; // current data length

        SlotFlags flags;
        char name[MaxNameLength] = { 0 };

        inline uint8_t* data_ptr() {
            return reinterpret_cast<uint8_t*>(this) + SlotDataOffset;
        }
        inline const uint8_t* data_ptr() const {
            return reinterpret_cast<const uint8_t*>(this) + SlotDataOffset;
        }
        inline RwMutexWriteLockGuard getWriteLock() {
            return RwMutexWriteLockGuard { mtx };
        }
        inline RwMutexReadLockGuard getReadLock() {
            return RwMutexReadLockGuard { mtx };
        }
        inline LockedView read() {
            return LockedView {
                ByteSpan { data_ptr(), length },
                getReadLock(),
				this
            };
        }

        void write(Domain* dom, ByteSpan span);
    };

    static_assert(sizeof(Slot) < SlotDataOffset, "Slot type too large for SlotDataOffset");

    struct Domain {

    public:
        std::array<char, 4> magic = DomainMagic;
        RwMutex slotMtx; // actually I don't think I need this.
        SequenceCounter seq;
        std::size_t slotFileSizes;
        char name[MaxNameLength] = { 0 };
        Slot slots[64];
    };

    inline void Slot::write(Domain* dom, ByteSpan span) {
        assert(span.len < SlotFileSize - SlotDataOffset);
        {
            auto lck { getWriteLock() };
            std::memcpy(data_ptr(), span.ptr, span.len);
            length = span.len;
            seq.incrementNoFutexWake();
        }
        SPDLOG_TRACE("slot::write() wrote n={} to 0x{:0x}", span.len, (std::size_t)data_ptr());
        dom->seq.increment(1u << index);
    }

} // namespace babus

namespace fmt {
    template <> struct formatter<babus::Slot> : formatter<std::string> {
        fmt::appender format(const babus::Slot& t, format_context& ctx);
    };
}
