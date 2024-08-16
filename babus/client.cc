#include "client.h"

namespace babus {

    namespace {

        void throwIfNotValidFileName(const char* s) {
            for (int i = 0; s[i] != 0; i++) {
                char c = s[i];
                if (c == '/') throw std::runtime_error("name cannot have a '/'");
                if (c == ' ') throw std::runtime_error("name cannot have a ' '");
                if (not(c >= ' ' and c <= '~')) throw std::runtime_error("name had invalid char " + std::to_string((int)c));
            }
        }

        inline bool magicMatches(const std::array<char, 4>& a, const std::array<char, 4>& b) {
            for (int i = 0; i < 4; i++)
                if (a[i] != b[i]) return false;
            return true;
        }

    }

    ClientSlot ClientSlot::openOrCreate(Domain* dom, const std::string& name, std::size_t size, void* targetAddr) {
        auto builder = MmapBuilder {};
        Mmap mmap    = builder.path(std::string { Prefix } + name).allowCreate().size(size).targetAddr(targetAddr).build();

        assert(reinterpret_cast<std::size_t>(mmap.ptr()) % 8 == 0);
        auto ptr = reinterpret_cast<Slot*>(mmap.ptr());

        if (builder.didCreateFile()) {
            SPDLOG_TRACE("construct Slot using placement new.");
            new (ptr) Slot {};
            memcpy(ptr->name, name.c_str(), name.length());
        }

        // SPDLOG_TRACE("check Slot magic @ 0x{:0x}", (std::size_t)ptr);
        // if (!ptr->magicIsCorrect()) {
        if (!magicMatches(ptr->magic, SlotMagic)) {
            SPDLOG_ERROR("failed Slot magic check");
            throw std::runtime_error("failed Slot magic check");
        }

        if (strcmp(ptr->name, name.c_str()) != 0) {
            SPDLOG_ERROR("failed Slot name check (slot name '{}' != expected '{}')", ptr->name, name.c_str());
            throw std::runtime_error("failed Slot name check");
        }

        // SPDLOG_CRITICAL("ini mtx val : {}", ptr->mtx.load());

        return ClientSlot { std::move(mmap), dom };
    }

    ClientDomain ClientDomain::openOrCreate(const std::string& name, std::size_t size, void* targetAddr) {
        auto builder = MmapBuilder {};
        Mmap mmap    = builder.path(std::string { Prefix } + name).allowCreate().size(size).targetAddr(targetAddr).build();

        assert(reinterpret_cast<std::size_t>(mmap.ptr()) % 8 == 0);
        auto ptr = reinterpret_cast<Domain*>(mmap.ptr());

        if (builder.didCreateFile()) {
            SPDLOG_TRACE("construct Domain using placement new.");
            new (ptr) Domain {};
        }

        // SPDLOG_TRACE("check Domain magic @ 0x{:0x}", (std::size_t)ptr);
        if (!magicMatches(ptr->magic, DomainMagic)) {
            SPDLOG_ERROR("failed Domain magic check");
            throw std::runtime_error("failed Domain magic check");
        }

        return ClientDomain(std::move(mmap));
    }

    ClientSlot& ClientDomain::getSlot(const char* s) {
        std::lock_guard<std::mutex> lck(processPrivateMtx_);

        auto it = slots_.find(s);

        if (it != slots_.end()) { return *it->second; }

        throwIfNotValidFileName(s);

        auto newSlot = std::unique_ptr<ClientSlot>(new ClientSlot(ClientSlot::openOrCreate(ptr(), s)));
        slots_.insert(s, std::move(newSlot));

        it = slots_.find(s);
        if (it != slots_.end()) {
            return *it->second;
        } else {
            SPDLOG_ERROR("just created+inserted slot '{}' but its missing?", s);
            throw std::runtime_error("just created+inserted slot but its missing?");
        }
    }

}

namespace fmt {
    static char format_char(uint8_t cc) {
        char c = cc;
        if (c == 0) return '0';
        if (c >= ' ' and c <= '~') return c;
        return '?';
    }
    using namespace babus;
    fmt::appender formatter<ClientDomain>::format(const ClientDomain& a, format_context& ctx) {
        fmt::format_to(ctx.out(), "Domain {{\n");

        // FIXME: Not hygenic.
        std::lock_guard<std::mutex> lck(const_cast<ClientDomain&>(a).processPrivateMtx_);
        for (auto& kv : a.slots_) { fmt::format_to(ctx.out(), "{}\n", *kv.second->ptr()); }
        return fmt::format_to(ctx.out(), "}}");
    }
}
