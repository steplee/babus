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

    // struct __attribute__((packed)) SequenceCounter {
    struct SequenceCounter {

    private:
        static constexpr auto seq_cst = std::memory_order_seq_cst;

        std::atomic<uint32_t> value;

    public:
        inline SequenceCounter() {
            value.store(0);
        }

        inline volatile uint32_t* asPtr() {
            return reinterpret_cast<volatile uint32_t*>(&value);
        }

        inline uint32_t load() const {
            return value.load(seq_cst);
        }

        inline uint32_t incrementNoFutexWake() {
            return value++;
        }

        inline uint32_t increment(uint32_t mask) {
            auto out = value++;

            FutexView ftx(asPtr());
            auto stat = ftx.wakeBitset(65536, mask);
            if (stat < 0) {
                SPDLOG_ERROR("futex.wakeBitset errno {} ('{}')", errno, strerror(errno));
            } else {
                // SPDLOG_TRACE("futex.wakeBitset ftx 0x{:0x}", (std::size_t)asPtr());
            }

            return out;
        }

        // Wait for the value to change, then return the old value.
        inline uint32_t waitForChange(uint32_t prv, uint32_t mask) {
            uint32_t cur = load();

            if (cur != prv) {
                SPDLOG_TRACE("waitForChange prv != cur ({} vs {}). No futex wait needed.", cur, prv);
                return cur;
            }

            FutexView ftx(asPtr());
            // SPDLOG_TRACE("futex.waitBitset ftx 0x{:0x}", (std::size_t)asPtr());
            auto stat = ftx.waitBitset(cur, mask);

            if (stat < 0) {
                if (errno == EAGAIN) {
                    SPDLOG_TRACE("futex.waitBitset returned EAGAIN. This is not an error.");
                } else {
                    SPDLOG_ERROR("futex.waitBitset errno {} ('{}')", errno, strerror(errno));
                }
            } else {
                SPDLOG_TRACE("futex.waitBitset returned w/o error.");
            }

            return cur;
        }
    };

    static_assert(sizeof(SequenceCounter) == 4, "SequenceCounter must be four bytes");
}
