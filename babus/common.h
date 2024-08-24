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

        constexpr std::size_t MaxNameLength       = 32;

        constexpr std::size_t DomainFileSize      = (4 * (1 << 20));
        constexpr std::size_t SlotFileSize        = (16 * (1 << 20));
        constexpr std::size_t SlotDataOffset      = 256; // space allocated for slot header.

        constexpr std::size_t SlotItemOffset      = 4096;
        constexpr std::size_t SlotMaxRingLength   = 8;
    }

    struct SlotFlags {
		union {
			uint32_t bits;
			struct {
				uint8_t kind;
			};
		};
    };
	static_assert(sizeof(SlotFlags) == 4);

    struct ByteSpan {
        void* ptr  = nullptr;
		std::size_t len = 0;

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

}
