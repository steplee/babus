#include "mmap.h"
#include "common.h"

#include <spdlog/spdlog.h>

#include <cerrno>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace babus {

    MmapBuilder& MmapBuilder::path(const std::string& path) {
        path_ = path;
        return *this;
    }
    MmapBuilder& MmapBuilder::anonymous() {
        anonymous_ = true;
        return *this;
    }
    MmapBuilder& MmapBuilder::size(std::size_t size) {
        size_ = size;
        return *this;
    }
    MmapBuilder& MmapBuilder::allowCreate() {
        allowCreate_ = true;
        return *this;
    }
    MmapBuilder& MmapBuilder::useTwoMegabytePages() {
        useTwoMegabytePages_ = true;
        return *this;
    }
    MmapBuilder& MmapBuilder::targetAddr(void* addr) {
        SPDLOG_DEBUG("You specified a null `targetAddr`. It will be disregarded.");
        targetAddr_ = addr;
        return *this;
    }
    MmapBuilder& MmapBuilder::doNotTruncateOnCreate() {
        truncateOnCreate_ = false;
        return *this;
    }

    Mmap MmapBuilder::build() {
        if (not(anonymous_ ^ (path_.length() > 0))) {
            SPDLOG_CRITICAL("must have set exactly one of `anonymous()` or `path()` (anon: {}, path: {})", anonymous_, path_);
            throw std::runtime_error("must have set one of `anonymous()` or `path()`");
        }
        if (size_ <= 0) {
            SPDLOG_CRITICAL("must set size");
            throw std::runtime_error("must set size");
        }

        int fd         = -1;
        void* mmap_ptr = 0;

        if (anonymous_) {
            SPDLOG_DEBUG("anonymous specified.");
            fd = -1;
        } else {
            SPDLOG_DEBUG("will open file '{}'", path_);
            int flags = O_RDWR;
            // if (allowCreate_) flags |= O_CREAT;

            fd = open(path_.c_str(), flags, 0777);

            if (fd < 0) {

                //
                // FIXME: This is NOT correct. We must use a file lock to make sure nobody creates the file before
                // we get in and do it.
                //

                if (allowCreate_) {
                    SPDLOG_DEBUG("first open('{}') failed with errno {} ('{}') but `allowCreate_` is true. Trying to create it.", path_,
                                 errno, strerror(errno));
                    flags |= O_CREAT;
                    flags |= O_EXCL;
                    fd = open(path_.c_str(), flags, 0777);

                    if (fd < 0) {
                        SPDLOG_ERROR("second open('{}') failed with errno {} ('{}')", path_, errno, strerror(errno));
                        throw std::runtime_error("open failed");
                    } else {
                        SPDLOG_DEBUG("Created file '{}'.", path_);
                        didCreateFile_ = true;
                    }

                } else {
                    SPDLOG_ERROR("first open('{}') failed with errno {} ('{}') and `allowCreate_` is false", path_, errno,
                                 strerror(errno));
                    throw std::runtime_error("open failed");
                }
            } else {
                SPDLOG_TRACE("opened existing file '{}'.", path_);
            }
        }

        if (didCreateFile_ and truncateOnCreate_) {
            SPDLOG_DEBUG("Since created file, truncating len={}.", size_);
            ftruncate(fd, size_);
        }

        int flags = 0;
        if (targetAddr_) flags |= MAP_FIXED;
        if (anonymous_)
            flags |= MAP_ANONYMOUS;
        else
            flags |= MAP_SHARED;
#ifdef MAP_HUGE_2MB
        if (useTwoMegabytePages_) flags |= MAP_HUGETLB | MAP_HUGE_2MB;
#else
        if (useTwoMegabytePages_) flags |= MAP_HUGETLB;
#endif

        mmap_ptr = mmap(targetAddr_, size_, PROT_READ | PROT_WRITE, flags, fd, 0);
        SPDLOG_TRACE("mmap @ 0x{:0x}", (std::size_t)mmap_ptr);

        if (mmap_ptr == 0) {
            SPDLOG_CRITICAL("mmap failed with errno {} ('{}')", path_, errno, strerror(errno));
            throw std::runtime_error("mmap failed");
        }

        if (fd > 0) {
            int closeStat = close(fd);
            if (closeStat != 0) {
                SPDLOG_ERROR("after mmap(), close('{}') failed with errno {} ('{}')", path_, errno, strerror(errno));
                throw std::runtime_error("close failed");
            }
        }

        didBuild_ = true;
        Mmap map(mmap_ptr, size_);
        return map;
    }

    Mmap::Mmap(void* addr, std::size_t len)
        : addr_(addr)
        , len_(len) {
    }
    Mmap::~Mmap() {
        SPDLOG_TRACE("[~Mmap] munmap(0x{:0x}, n={})", (std::size_t)addr_, len_);
        if (addr_) { munmap(addr_, len_); }
    }

    void Mmap::swapWith(Mmap& o) {
        std::swap(addr_, o.addr_);
        std::swap(len_, o.len_);
    }
}
