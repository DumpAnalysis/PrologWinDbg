#include "architecture.hpp"
#include <iostream>
#include <stdexcept>

int main() {
    // Every pairing matters: both native paths, cross-targets, 32-bit, EC, and unknown.
    const std::uint32_t machines[] = {0x8664, 0xaa64, 0x14c, 0x1c4, 0xa641, 0};
    unsigned checks=0;
    for(auto host:machines) for(auto target:machines) {
        const bool expected=(host==0x8664 && target==0x8664) || (host==0xaa64 && target==0xaa64);
        if(pd::native_target(host,target)!=expected)
            throw std::runtime_error("Unsafe native context architecture pairing");
        ++checks;
    }
#ifdef _WIN32
    if(!pd::supported_machine(pd::native_machine)) throw std::runtime_error("Unsupported Windows build architecture");
#endif
    std::cout << checks << " architecture checks passed; build=" << pd::architecture_name(pd::native_machine) << '\n';
}
