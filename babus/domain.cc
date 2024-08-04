#include "domain.h"

namespace babus {

    namespace {


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
}
