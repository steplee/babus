#pragma once

#include <cassert>
#include <cstdint>
#include <string>

namespace babus {

    class Mmap;

    struct MmapBuilder {
    public:
        inline MmapBuilder() {
        }
        MmapBuilder(const MmapBuilder&) = delete;
        MmapBuilder(Mmap&&)             = delete;

        MmapBuilder& path(const std::string& path);
        MmapBuilder& anonymous();
        MmapBuilder& size(std::size_t size);
        MmapBuilder& allowCreate();
        MmapBuilder& useTwoMegabytePages();
        MmapBuilder& targetAddr(void* addr);
        MmapBuilder& doNotTruncateOnCreate(); // This is by default on.

        Mmap build();

        inline bool didCreateFile() const {
            assert(didBuild());
            return didCreateFile_;
        }
        inline bool didBuild() const {
            return didBuild_;
        }

    private:
        bool allowCreate_         = false;
        bool anonymous_           = false;
        bool useTwoMegabytePages_ = false;
        bool truncateOnCreate_    = true;
        void* targetAddr_         = nullptr;
        std::string path_;
        std::size_t size_;

        bool didCreateFile_ = false;
        bool didBuild_      = false;
    };

    class Mmap {
    public:
        ~Mmap();
        Mmap(const Mmap&) = delete;
        inline Mmap(Mmap&& o) {
            swapWith(o);
        }
        inline Mmap& operator=(Mmap&& o) {
            swapWith(o);
            return *this;
        }

        inline void* ptr() const {
            return addr_;
        }

    private:
        friend struct MmapBuilder;
        Mmap(void* addr, std::size_t len);

        void swapWith(Mmap& o);

        void* addr_      = 0;
        std::size_t len_ = 0;
    };

}
