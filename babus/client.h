#pragma once

#include "domain.h"

#include <spdlog/spdlog.h>

namespace babus {

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
        std::mutex processPrivateMtx_;
        SmallMap<const char*, ClientSlot> slots_;

        inline ClientDomain(Mmap&& mmap)
            : mmap_(std::move(mmap)) {
        }

		inline ClientDomain(ClientDomain&& o)
			: mmap_(std::move(o.mmap_))
			  , slots_(std::move(o.slots_))
			  // , processPrivateMtx(std::move(o.processPrivateMtx))
		{
		}

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
    template <> struct formatter<babus::ClientDomain> : formatter<std::string> {
		fmt::appender format(const babus::ClientDomain& t, format_context& ctx);
    };
}

