#pragma once

#include "detail/small_map.hpp"
#include "mmap.h"
#include "rw_mutex.hpp"
#include "sequence_counter.hpp"
#include <mutex>

#include <spdlog/spdlog.h>

namespace babus {

    namespace {
        constexpr const char Prefix[]        = "/dev/shm/";

        constexpr std::size_t DomainFileSize = (4 * (1 << 20));
        constexpr std::size_t SlotFileSize   = (16 * (1 << 20));
        constexpr std::size_t SlotDataOffset = 256; // space allocated for slot header.
    }

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
        char magic[4] = { 's', 'l', 'o', 't' };
        RwMutex mtx;
        uint32_t index = 0; // Used for event futex mask.
        SequenceCounter seq;
        uint32_t length = 0; // current data length
        SlotFlags flags;
        char name[32] = { 0 };

        bool magicIsCorrect() const;


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
            };
        }

		void write(Domain* dom, ByteSpan span);

    };

    static_assert(sizeof(Slot) < SlotDataOffset, "Slot type too large for SlotDataOffset");

    struct Domain {

    public:
        char magic[4] = { 'd', 'o', 'm', ' ' };
        RwMutex slotMtx; // actually I don't think I need this.
        SequenceCounter seq;
        std::size_t slotFileSizes;
        char name[32] = { 0 };
        Slot slots[64];

        bool magicIsCorrect() const;
    };



	inline void Slot::write(Domain* dom, ByteSpan span) {
		assert(span.len < SlotFileSize - SlotDataOffset);
		{
			auto lck { getWriteLock() };
			std::memcpy(data_ptr(), span.ptr, span.len);
			length = span.len;
			seq.incrementNoFutexWake();
		}
		SPDLOG_TRACE("slot::write() given dom 0x{:0x}", (std::size_t)dom);
		dom->seq.increment(1u<<index);
	}


    struct ClientSlot {
    private:
        Mmap mmap_;
		Domain *domain_;

        inline ClientSlot(Mmap&& mmap, Domain* dom)
            : mmap_(std::move(mmap)), domain_(dom) {
        }

    public:
        inline ClientSlot(ClientSlot&& o)
            : mmap_(std::move(o.mmap_)), domain_(std::move(o.domain_)) {
        }
        inline ClientSlot& operator=(ClientSlot&& o) {
            mmap_ = std::move(o.mmap_);
			domain_ = std::move(o.domain_);
            return *this;
        }

        static ClientSlot openOrCreate(Domain* dom, const std::string& name, std::size_t size = SlotFileSize, void* targetAddr = 0);
        inline ~ClientSlot() {
        }

		inline operator Slot&() {
			return *ptr();
		}
        inline Slot* ptr() const {
            return reinterpret_cast<Slot*>(mmap_.ptr());
        }


        inline uint8_t* data_ptr() const {
            return ptr()->data_ptr();
        }
        inline Slot* operator->() const {
            return ptr();
        }
        inline RwMutexWriteLockGuard getWriteLock() const {
            return ptr()->getWriteLock();
        }
        inline RwMutexReadLockGuard getReadLock() const {
            return ptr()->getReadLock();
        }
        inline LockedView read() const {
			return ptr()->read();
        }
        inline void write(ByteSpan span) {
			return ptr()->write(domain_, span);
		}
    };

    struct ClientDomain {
    private:
        Mmap mmap_;

        // This is a mutex just for the `slots_` map below -- it's not
        // shared between different processes.
        std::mutex processPrivateMtx;
        SmallMap<const char*, ClientSlot> slots_;

        inline ClientDomain(Mmap&& mmap)
            : mmap_(std::move(mmap)) {
        }

        // RwMutexReadLock readLockSlotMap();

    public:
        static ClientDomain openOrCreate(const std::string& path, std::size_t size = DomainFileSize, void* targetAddr = 0);

        inline Domain* ptr() const {
            return reinterpret_cast<Domain*>(mmap_.ptr());
        }

        ClientSlot& getSlot(const char* s);
        friend struct fmt::formatter<ClientDomain>;
    };

} // namespace babus

namespace fmt {
    template <> struct formatter<babus::Slot> : formatter<std::string> {
		fmt::appender format(const babus::Slot& t, format_context& ctx);
    };
    template <> struct formatter<babus::ClientDomain> : formatter<std::string> {
		fmt::appender format(const babus::ClientDomain& t, format_context& ctx);
    };
}

