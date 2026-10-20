#include "worker_process.hpp"
#include "protocol.hpp"
#include <atomic>
#include <filesystem>
#include <mutex>
#include <sstream>
#include <fstream>

namespace {
HMODULE module_handle=nullptr;
std::mutex command_mutex;
pd::WorkerProcess* worker=nullptr;
pd::Limits limits;
std::wstring runtime_override;
std::atomic<std::uint64_t> next_snapshot{0};

std::wstring extension_directory() {
    std::wstring path(32768,L'\0');
    DWORD n=GetModuleFileNameW(module_handle,path.data(),static_cast<DWORD>(path.size()));
    if(!n || n>=path.size()) throw std::runtime_error(pd::win_error("Find extension directory"));
    path.resize(n); return std::filesystem::path(path).parent_path().wstring();
}
std::wstring runtime_path() {
    std::wstring path=runtime_override;
    if(path.empty()) path=pd::environment(L"PROLOGDBG_SWIPL");
    if(path.empty()) {
        auto program_files=pd::environment(L"ProgramFiles");
        auto installed=std::filesystem::path(program_files)/L"swipl"/L"bin"/L"swipl.exe";
        if(std::filesystem::exists(installed)) path=installed.wstring();
    }
    if(path.empty() || !std::filesystem::path(path).is_absolute() || !std::filesystem::exists(path))
        throw std::runtime_error("Set an absolute runtime path: !prolog.runtime \"C:\\Program Files\\swipl\\bin\\swipl.exe\"");
    return path;
}
pd::WorkerProcess& get_worker(pd::Debugger& dbg) {
    if(!worker) worker=new pd::WorkerProcess;
    auto script=(std::filesystem::path(extension_directory())/L"prolog"/L"worker.pl").wstring();
    if(!std::filesystem::exists(script)) throw std::runtime_error("Missing prolog/worker.pl beside prolog.dll; keep the package directories together");
    worker->start(runtime_path(),script,dbg); return *worker;
}
std::string args_utf8(PCSTR args) {
    // The extension-command ABI uses the Windows ANSI code page.
    return pd::utf8(pd::widen(args ? args : "",CP_ACP));
}
std::string trim(std::string text) {
    auto first=text.find_first_not_of(" \t\r\n"); if(first==text.npos) return {};
    auto last=text.find_last_not_of(" \t\r\n"); return text.substr(first,last-first+1);
}
std::wstring path_argument(PCSTR args) {
    auto text=trim(args_utf8(args));
    if(text.size()>=2 && text.front()=='"' && text.back()=='"') text=text.substr(1,text.size()-2);
    if(text.empty() || text.find('\0')!=text.npos) throw std::runtime_error("Expected a quoted absolute file path");
    auto result=pd::widen(text);
    if(!std::filesystem::path(result).is_absolute()) throw std::runtime_error("Use an absolute file path");
    return result;
}
void execute_query(pd::Debugger& dbg,const std::string& goal,bool capture) {
    if(goal.empty()) throw std::runtime_error("Expected a Prolog goal");
    auto& w=get_worker(dbg); auto id=++next_snapshot;
    auto facts=capture ? dbg.snapshot(id,limits) : "[]";
    if(capture) dbg.output("[prolog] Snapshot "+std::to_string(id)+"\n");
    w.transact({"QUERY",std::to_string(id),std::to_string(limits.time_ms),std::to_string(limits.inferences),
        std::to_string(limits.answers),pd::hex(goal),pd::hex(facts)},dbg,id,limits.time_ms+3000,capture);
}
template<class F> HRESULT command(PDEBUG_CLIENT client,F body) noexcept {
    std::unique_lock<std::mutex> lock(command_mutex,std::try_to_lock);
    if(!lock.owns_lock()) return E_UNEXPECTED;
    try {
        pd::Debugger dbg(client);
        try { body(dbg); return S_OK; }
        catch(const std::exception& e) {
            if(worker && !worker->running()) worker->stop();
            dbg.output(std::string("[prolog] ")+e.what()+"\n",DEBUG_OUTPUT_ERROR); return E_FAIL;
        }
    } catch(...) { return E_FAIL; }
}
}

BOOL WINAPI DllMain(HINSTANCE h,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH) { module_handle=h; DisableThreadLibraryCalls(h); }
    return TRUE;
}
extern "C" HRESULT CALLBACK DebugExtensionInitialize(PULONG version,PULONG flags) {
    *version=DEBUG_EXTENSION_VERSION(0,2); *flags=0; return S_OK;
}
extern "C" void CALLBACK DebugExtensionUninitialize() {
    delete worker;worker=nullptr;
}
extern "C" void CALLBACK DebugExtensionNotify(ULONG,ULONG64) {
    pd::notify_session();
}
extern "C" HRESULT CALLBACK help(PDEBUG_CLIENT client,PCSTR) {
    return command(client,[](pd::Debugger& dbg) {
        dbg.output(
          "Prolog WinDbg " PROLOGDBG_VERSION " - native AMD64/ARM64 user-mode dumps and stopped live processes\n"
          "!prolog.runtime \"C:\\Program Files\\swipl\\bin\\swipl.exe\"  Set runtime\n"
          "!prolog.info                             Start/check worker\n"
          "!prolog.consult \"C:\\rules\\file.pl\"     Load/reload Prolog source\n"
          "!prolog.query Goal.                      Query a fresh snapshot\n"
          "!prolog.queryfile \"C:\\rules\\goal.txt\"   Read a UTF-8 goal and query\n"
          "!prolog.eval Goal.                       Pure Prolog, no target snapshot\n"
          "!prolog.limits [ms answers frames inferences]  View/set budgets\n"
          "!prolog.reset                            Stop worker; clears consulted rules\n"
          "Use WinDbg's Break/Ctrl+Break to interrupt; normal debugging commands control execution.\n"
          "Snapshot facts exist only during a query. Data providers are read-only.\n");
    });
}
extern "C" HRESULT CALLBACK runtime(PDEBUG_CLIENT client,PCSTR args) {
    return command(client,[&](pd::Debugger& dbg) {
        auto p=path_argument(args);
        if(!std::filesystem::is_regular_file(p)) throw std::runtime_error("swipl.exe path does not exist");
        if(worker) worker->stop();
        runtime_override=p;
        dbg.output("[prolog] Runtime set to "+pd::utf8(p)+"\n");
    });
}
extern "C" HRESULT CALLBACK info(PDEBUG_CLIENT client,PCSTR) {
    return command(client,[](pd::Debugger& dbg) {
        auto& w=get_worker(dbg); dbg.output(std::string("[prolog] " PROLOGDBG_VERSION " / protocol 1; extension=")+pd::architecture_name(pd::native_machine)+"; SWI-Prolog "+w.version()+"; worker running\n");
    });
}
extern "C" HRESULT CALLBACK reset(PDEBUG_CLIENT client,PCSTR) {
    return command(client,[](pd::Debugger& dbg) {
        if(worker) worker->stop();
        dbg.output("[prolog] Worker stopped. Consult files again after restart.\n");
    });
}
extern "C" HRESULT CALLBACK consult(PDEBUG_CLIENT client,PCSTR args) {
    return command(client,[&](pd::Debugger& dbg) {
        auto path=path_argument(args);
        if(!std::filesystem::is_regular_file(path)) throw std::runtime_error("Prolog source file does not exist");
        auto& w=get_worker(dbg); auto id=++next_snapshot;
        w.transact({"CONSULT",std::to_string(id),pd::hex(pd::utf8(path))},dbg,id,17000,false);
    });
}
extern "C" HRESULT CALLBACK query(PDEBUG_CLIENT client,PCSTR args) {
    return command(client,[&](pd::Debugger& dbg) {
        execute_query(dbg,pd::command_goal(args_utf8(args)),true);
    });
}
extern "C" HRESULT CALLBACK queryfile(PDEBUG_CLIENT client,PCSTR args) {
    return command(client,[&](pd::Debugger& dbg) {
        auto path=path_argument(args);
        std::ifstream input(std::filesystem::path(path),std::ios::binary);
        if(!input) throw std::runtime_error("Cannot open query file");
        std::string goal; char chunk[4096];
        while(input.read(chunk,sizeof(chunk)) || input.gcount()) {
            goal.append(chunk,static_cast<std::size_t>(input.gcount()));
            if(goal.size()>1024u*1024u) throw std::runtime_error("Query file exceeds 1 MiB");
        }
        if(input.bad()) throw std::runtime_error("Cannot read query file");
        if(goal.compare(0,3,"\xef\xbb\xbf")==0) goal.erase(0,3);
        pd::widen(goal); // Validate UTF-8 before sending it to the worker.
        execute_query(dbg,goal,true);
    });
}
extern "C" HRESULT CALLBACK eval(PDEBUG_CLIENT client,PCSTR args) {
    return command(client,[&](pd::Debugger& dbg) {
        execute_query(dbg,pd::command_goal(args_utf8(args)),false);
    });
}
extern "C" HRESULT CALLBACK limits_cmd(PDEBUG_CLIENT client,PCSTR args) {
    return command(client,[&](pd::Debugger& dbg) {
        auto text=trim(args_utf8(args));
        if(!text.empty()) {
            std::istringstream in(text); std::string a,b,c,d,extra;
            if(!(in>>a>>b>>c>>d) || in>>extra) throw std::runtime_error("Usage: !prolog.limits ms answers frames inferences");
            auto ms=pd::number(a),answers=pd::number(b),frames=pd::number(c),inf=pd::number(d);
            if(ms<1 || ms>600000 || answers<1 || answers>10000 || frames<1 || frames>1024 || inf<1 || inf>1000000000)
                throw std::runtime_error("Limits: ms 1..600000, answers 1..10000, frames 1..1024, inferences 1..1000000000");
            limits.time_ms=static_cast<unsigned>(ms);limits.answers=static_cast<unsigned>(answers);
            limits.frames=static_cast<unsigned>(frames);limits.inferences=inf;
        }
        dbg.output("[prolog] "+std::to_string(limits.time_ms)+" ms; "+std::to_string(limits.answers)+" answers; "+
            std::to_string(limits.frames)+" frames/thread; "+std::to_string(limits.inferences)+" inferences\n");
    });
}
