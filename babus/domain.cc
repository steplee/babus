#include "domain.h"

namespace babus {

    namespace { }



	void* Domain::getSlot(int i) {
		assert(i < numSlots);

		uint8_t* base = reinterpret_cast<uint8_t*>(this) + sizeof(Domain);

		// Make sure we are aligned to the size of a pointer on this machine, no matter the size of `Domain`.
		base += reinterpret_cast<std::size_t>(base) % sizeof(void*);

		// Offset past the end of `this`
		void* out = base + sizeof(void*) * i;

		// Make sure all pointers are aligned.
		assert(reinterpret_cast<std::size_t>(this) % sizeof(void*) == 0);
		assert(reinterpret_cast<std::size_t>(base) % sizeof(void*) == 0);
		assert(reinterpret_cast<std::size_t>(out)  % sizeof(void*) == 0);

		// Make sure we don't access out-of-bounds of the mmap.
		assert(reinterpret_cast<uint8_t*>(out) - reinterpret_cast<uint8_t*>(this) < DomainFileSize);

		return out;
	}

}

