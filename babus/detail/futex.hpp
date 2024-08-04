#pragma once

#include <cstdint>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

namespace babus {

    struct FutexView {
        uint32_t* uaddr;

        inline FutexView(uint32_t* p)
            : uaddr(p) {
        }

        inline long waitBitset(uint32_t expectedValue, uint32_t mask) {
            return syscall(SYS_futex, this->uaddr, FUTEX_WAIT_BITSET, expectedValue, 0, 0, mask);
        }

        inline long wakeBitset(uint32_t numToWake, uint32_t mask) {
            return syscall(SYS_futex, this->uaddr, FUTEX_WAKE_BITSET, numToWake, 0, 0, mask);
        }

        inline long wait(uint32_t expectedValue) {
            return syscall(SYS_futex, this->uaddr, FUTEX_WAIT, expectedValue, 0, 0, 0);
        }

        inline long wake(uint32_t numToWake) {
            return syscall(SYS_futex, this->uaddr, FUTEX_WAKE, numToWake, 0, 0, 0);
        }
    };

}
