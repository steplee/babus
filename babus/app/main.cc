#include "babus/client.h"

using namespace babus;

int main() {

    spdlog::set_level(spdlog::level::trace);

    ClientDomain d = ClientDomain::openOrCreate(std::string { Prefix } + "dom2");

    ClientSlot& s { d.getSlot("mySlot") };

    SPDLOG_INFO("{}", d);

    const char hello[] = "hello1\0";
    s.write({ (void*)hello, 7 });

    SPDLOG_INFO("{}", d);

    const char bye[] = "bye1\0";
    s.write({ (void*)bye, 5 });

    SPDLOG_INFO("{}", d);

    return 0;
}
