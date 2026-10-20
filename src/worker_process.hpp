#pragma once
#include "debugger.hpp"
#include <functional>
#include <string>

namespace pd {
class WorkerProcess {
    Handle process_, job_, input_, output_, error_;
    std::string pending_, diagnostics_, version_;
    std::wstring executable_,script_;
    void drain_errors();
    std::string line(Debugger& dbg,ULONGLONG deadline);
    void write(const std::string& message,Debugger& dbg,ULONGLONG deadline);
public:
    ~WorkerProcess() { stop(); }
    bool running() const;
    const std::string& version() const { return version_; }
    void stop() noexcept;
    void start(const std::wstring& executable,const std::wstring& script,Debugger& dbg);
    void transact(const std::vector<std::string>& request,Debugger& dbg,std::uint64_t id,unsigned deadline_ms,bool allow_rpc);
};
}
