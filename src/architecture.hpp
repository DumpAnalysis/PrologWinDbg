#pragma once
#include <cstdint>

namespace pd {
// PE machine identifiers, also returned by DbgEng::GetEffectiveProcessorType.
constexpr std::uint32_t machine_amd64 = 0x8664;
constexpr std::uint32_t machine_arm64 = 0xaa64;

constexpr bool supported_machine(std::uint32_t machine) {
    return machine == machine_amd64 || machine == machine_arm64;
}
constexpr bool native_target(std::uint32_t host, std::uint32_t target) {
    return supported_machine(host) && host == target;
}
constexpr const char* architecture_name(std::uint32_t machine) {
    return machine == machine_amd64 ? "amd64" : machine == machine_arm64 ? "arm64" : "unsupported";
}

// CONTEXT is defined by Windows for the DLL's compilation architecture.
// ARM64EC uses a different ABI and must never be mistaken for native ARM64/x64.
#if defined(_M_ARM64EC) || defined(__arm64ec__)
#error "PrologWinDbg requires native x64 or ARM64, not ARM64EC"
#elif defined(_M_ARM64) || defined(__aarch64__)
constexpr auto native_machine = machine_arm64;
#elif defined(_M_X64) || defined(__x86_64__)
constexpr auto native_machine = machine_amd64;
#elif defined(_WIN32)
#error "PrologWinDbg requires native x64 or ARM64"
#else
constexpr std::uint32_t native_machine = 0; // Portable protocol tests on other hosts.
#endif
}
