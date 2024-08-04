#pragma once

#include <atomic>
#include <cstdint>

#include <linux/futex.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <spdlog/spdlog.h>

#include "futex.hpp"

namespace babus {

    struct RwMutex {

    private:
        static constexpr auto seq_cst      = std::memory_order_seq_cst;

        static constexpr uint32_t Locked   = 0;
        static constexpr uint32_t Unlocked = 1;

        std::atomic<uint32_t> value;

    public:
        inline RwMutex() {
            value.store(Unlocked, seq_cst);
        }

        inline uint32_t* asPtr() {
            return reinterpret_cast<uint32_t*>(&value);
        }

        inline uint32_t load() {
            return value.load(seq_cst);
        }

        inline void w_lock() {
            while (1) {
                auto old  = load();
                auto old_ = old; // cmpexh actually modifies our `old`
                if (old == Unlocked) {
                    uint32_t nxt = 0;
                    if (value.compare_exchange_strong(old, nxt, seq_cst, seq_cst)) {
                        // The op successfully completed. We've set `nxt`, meaning WE now hold the lock.
                        // This is the only place we can break from the loop.
                        SPDLOG_TRACE("cmpexh completed happy path ({} == {}). Returning without futex wait.", old_, old);
                        return;
                    } else {
                        // We've failed the 'happy' path and must use futex wait.
                        SPDLOG_TRACE("cmpexh failed happy path ({} != {}). Entering futex wait.", old_, old);
                    }
                } else {
                    SPDLOG_TRACE("lock is reader-held, cmpexh/happy path not even tried. Entering futex wait.");
                }

                assert(old < Unlocked);

                FutexView ftx { asPtr() };
                auto ftxStat = ftx.wait(old);
                if (ftxStat < 0) {
                    if (errno == EAGAIN) {
                        SPDLOG_TRACE("futex got EAGAIN. loop back.");
                    } else {
                        SPDLOG_ERROR("futex got errno {} ('{}').", errno, strerror(errno));
                        throw std::runtime_error("futex error.");
                    }
                } else {
                    SPDLOG_TRACE("futex.wait() success. Loop back.");
                }
            }
        }

        inline void r_lock() {
            while (1) {
                auto old  = load();
                auto old_ = old; // cmpexh actually modifies our `old`
                if (old != Locked) {
                    uint32_t nxt = old + 1;
                    if (value.compare_exchange_strong(old, nxt, seq_cst, seq_cst)) {
                        // The op successfully completed. We've set `nxt`, meaning WE now hold the lock.
                        // This is the only place we can break from the loop.
                        SPDLOG_TRACE("cmpexh completed happy path ({} == {}). Returning without futex wait.", old_, old);
                        return;
                    } else {
                        // We've failed the 'happy' path and must use futex wait.
                        SPDLOG_TRACE("cmpexh failed happy path ({} != {}). Entering futex wait.", old_, old);
                    }
                } else {
                    SPDLOG_TRACE("lock is writer-held, cmpexh/happy path not even tried. Entering futex wait.");
                }

                assert(old < Unlocked);

                FutexView ftx { asPtr() };
                auto ftxStat = ftx.wait(old);
                if (ftxStat < 0) {
                    if (errno == EAGAIN) {
                        SPDLOG_TRACE("futex got EAGAIN. loop back.");
                    } else {
                        SPDLOG_ERROR("futex got errno {} ('{}').", errno, strerror(errno));
                        throw std::runtime_error("futex error.");
                    }
                } else {
                    SPDLOG_TRACE("futex.wait() success. Loop back.");
                }
            }
        }

        inline void w_unlock() {
            auto old = value.fetch_add(1, seq_cst);
            auto nxt = old + 1;
            assert(old == Locked);
            assert(nxt == Unlocked);

            // Wake all readers.
            FutexView ftx { asPtr() };
            auto ftxStat = ftx.wake(65536);
            if (ftxStat < 0) {
                SPDLOG_ERROR("ftx.wake() failed errno {} ('{}')", errno, strerror(errno));
            } else {
                SPDLOG_TRACE("ftx.wake() woke {}.", ftxStat);
            }
        }

        inline void r_unlock() {
            auto old = value.fetch_sub(1, seq_cst);
            auto nxt = old - 1;
            assert(old > Unlocked);
            assert(nxt >= Unlocked);

            // If this unlocked it, wake one sleeper (one writer).
            // FIXME: Is this sound? Is it really impossible for the one sleeper to be a writer, and would that prevent forward
            // progress?
            if (nxt == Unlocked) {
                FutexView ftx { asPtr() };
                auto ftxStat = ftx.wake(1);
                if (ftxStat < 0) {
                    SPDLOG_ERROR("ftx.wake() failed errno {} ('{}')", errno, strerror(errno));
                } else {
                    SPDLOG_TRACE("ftx.wake() woke {}.", ftxStat);
                }
            }
        }
    };

    static_assert(sizeof(RwMutex) == 4, "RwMutex must be four bytes");

    template <bool Write> struct RwMutexLockGuard {
        inline RwMutexLockGuard(RwMutex& m)
            : mtx_(&m) {
			if (mtx_) {
            if constexpr (Write)
                mtx_->w_lock();
            else
                mtx_->r_lock();
			}
        }
        inline ~RwMutexLockGuard() {
			if (mtx_) {
				if constexpr (Write)
					mtx_->w_unlock();
				else
					mtx_->r_unlock();
			}
        }

		// This should not be needed except to make the FFI code cleaner.
		inline RwMutex* forgetUnsafe() {
			// SPDLOG_DEBUG("forgetUnsafe() called -- are you sure you want this?");
			RwMutex* out = mtx_;
			mtx_ = nullptr;
			return out;
		}

		private:
        RwMutex* mtx_ = nullptr;
    };

    using RwMutexWriteLockGuard = RwMutexLockGuard<true>;
    using RwMutexReadLockGuard  = RwMutexLockGuard<false>;

}
