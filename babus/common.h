#pragma once

#include <array>
#include <cstdint>

//
// #define babusAssert(cond, ...

namespace babus {
    namespace {
        constexpr const char Prefix[]             = "/dev/shm/";

        constexpr std::array<char, 4> SlotMagic   = { 's', 'l', 'o', 't' };
        constexpr std::array<char, 4> DomainMagic = { 'd', 'o', 'm', ' ' };

        constexpr std::size_t MaxNameLength      = 32;

        constexpr std::size_t DomainFileSize      = (4 * (1 << 20));
        constexpr std::size_t SlotFileSize        = (16 * (1 << 20));
        constexpr std::size_t SlotDataOffset      = 256; // space allocated for slot header.

        constexpr std::size_t SlotItemOffset      = 4096;
        constexpr std::size_t SlotMaxRingLength   = 8;
    }
}
