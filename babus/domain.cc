#include "domain.h"

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
    }

    bool Slot::magicIsCorrect() const {
        return magic[0] == 's';
        return magic[1] == 'l';
        return magic[2] == 'o';
        return magic[3] == 't';
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
        if (!ptr->magicIsCorrect()) {
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

    bool Domain::magicIsCorrect() const {
        return magic[0] == 'd';
        return magic[1] == 'o';
        return magic[2] == 'm';
        return magic[3] == ' ';
    }

    ClientDomain ClientDomain::openOrCreate(const std::string& path, std::size_t size, void* targetAddr) {
        auto builder = MmapBuilder {};
        Mmap mmap    = builder.path(path).allowCreate().size(size).targetAddr(targetAddr).build();

        assert(reinterpret_cast<std::size_t>(mmap.ptr()) % 8 == 0);
        auto ptr = reinterpret_cast<Domain*>(mmap.ptr());

        if (builder.didCreateFile()) {
            SPDLOG_TRACE("construct Domain using placement new.");
            new (ptr) Domain {};
        }

        // SPDLOG_TRACE("check Domain magic @ 0x{:0x}", (std::size_t)ptr);
        if (!ptr->magicIsCorrect()) {
            SPDLOG_ERROR("failed Domain magic check");
            throw std::runtime_error("failed Domain magic check");
        }

        return ClientDomain(std::move(mmap));
    }

    ClientSlot& ClientDomain::getSlot(const char* s) {
        std::lock_guard<std::mutex> lck(processPrivateMtx);

        auto it = slots_.find(s);

        if (it != slots_.end()) { return it->second; }

        throwIfNotValidFileName(s);

        ClientSlot newSlot = ClientSlot::openOrCreate(ptr(), s);
        slots_.insert(s, std::move(newSlot));

        it = slots_.find(s);
        if (it != slots_.end()) {
            return it->second;
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
	fmt::appender formatter<Slot>::format(const Slot& a, format_context& ctx) {
        fmt::format_to(ctx.out(), "   Slot {{\n");
        fmt::format_to(ctx.out(), "       name: '{}'\n", a.name);
        fmt::format_to(ctx.out(), "       seq : '{}'\n", a.seq.load());
        {
            auto view = const_cast<Slot&>(a).read();
            if (view.span.len == 0)
                fmt::format_to(ctx.out(), "       data: <empty>\n");
            else if (view.span.ptr == nullptr)
                fmt::format_to(ctx.out(), "       data: <null>\n");
            else
                fmt::format_to(ctx.out(), "       data: [{} {} {} {} {} {}...] len={}\n", format_char(view.span[0]),
                               format_char(view.span[1]), format_char(view.span[2]), format_char(view.span[3]),
                               format_char(view.span[4]), format_char(view.span[5]), view.span.len);
        }
        return fmt::format_to(ctx.out(), "   }}");
    }
	fmt::appender formatter<ClientDomain>::format(const ClientDomain& a, format_context& ctx) {
        fmt::format_to(ctx.out(), "Domain {{\n");

        // FIXME: Not hygenic.
        std::lock_guard<std::mutex> lck(const_cast<ClientDomain&>(a).processPrivateMtx);
        for (auto& kv : a.slots_) { fmt::format_to(ctx.out(), "{}\n", *kv.second.ptr()); }
        return fmt::format_to(ctx.out(), "}}");
    }
}
