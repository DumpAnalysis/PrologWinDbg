#pragma once
#include "win_util.hpp"
#include "dbgeng_compat.hpp"
#include <functional>
#include <vector>

namespace pd {
void notify_session() noexcept;
struct Limits {
    unsigned time_ms = 5000;
    unsigned answers = 100;
    unsigned frames = 128;
    unsigned threads = 4096;
    std::uint64_t inferences = 10000000;
};
class Debugger {
    Com<IDebugControl4> control_;
    Com<IDebugSystemObjects> systems_;
    Com<IDebugSymbols3> symbols_;
    Com<IDebugDataSpaces> memory_;
    ULONG process_ = DEBUG_ANY_ID;
    ULONG system_ = DEBUG_ANY_ID;
    std::size_t bytes_read_ = 0;
    std::uint64_t generation_ = 0;
public:
    explicit Debugger(IUnknown* client): control_(client,IID_IDebugControl4),systems_(client,IID_IDebugSystemObjects),
        symbols_(client,IID_IDebugSymbols3),memory_(client,IID_IDebugDataSpaces) {}
    IDebugControl4* control() const { return control_.get(); }
    void output(const std::string& text, ULONG kind=DEBUG_OUTPUT_NORMAL) const;
    void require_stopped();
    void check_session();
    void check_interrupt() const;
    std::string snapshot(std::uint64_t id,const Limits& limits);
    std::vector<std::string> rpc(const std::vector<std::string>& fields,std::uint64_t id);
};
}
