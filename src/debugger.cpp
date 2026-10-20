#include "debugger.hpp"
#include "protocol.hpp"
#include <algorithm>
#include <set>
#include <sstream>
#include <vector>
#include <atomic>

namespace pd {
static std::atomic<std::uint64_t> session_generation{1};
void notify_session() noexcept { ++session_generation; }
void Debugger::output(const std::string& text,ULONG kind) const {
    std::wstring wide;
    try { wide=widen(text); } catch(...) { wide=widen(text,CP_ACP); }
    control_->OutputWide(kind,L"%ls",wide.c_str());
}
void Debugger::check_interrupt() const {
    if(control_->GetInterrupt()==S_OK) throw std::runtime_error("Interrupted by user");
}
void Debugger::require_stopped() {
    ULONG klass=0,qualifier=0,status=0,machine=0;
    check(control_->GetDebuggeeType(&klass,&qualifier),"GetDebuggeeType");
    if(klass!=DEBUG_CLASS_USER_WINDOWS) throw std::runtime_error("Version 1 supports user-mode Windows targets only");
    check(control_->GetExecutionStatus(&status),"GetExecutionStatus");
    if(status!=DEBUG_STATUS_BREAK) throw std::runtime_error("Break into the target before querying");
    check(control_->GetEffectiveProcessorType(&machine),"GetEffectiveProcessorType");
    if(!supported_machine(machine))
        throw std::runtime_error("Supported target architectures are native AMD64 and ARM64; WOW64 and ARM64EC are unsupported");
    if(!native_target(native_machine,machine))
        throw std::runtime_error(std::string("Target architecture ")+architecture_name(machine)+
            " does not match this "+architecture_name(native_machine)+
            " extension; use a matching native debugger and DLL");
    machine_=machine;
    check(systems_->GetCurrentProcessId(&process_),"GetCurrentProcessId");
    Com<IDebugSystemObjects3> sys3(systems_.get(),IID_IDebugSystemObjects3);
    check(sys3->GetCurrentSystemId(&system_),"GetCurrentSystemId");
    bytes_read_=0;
    generation_=session_generation.load();
}
void Debugger::check_session() {
    check_interrupt();
    ULONG status=0,p=0,s=0,machine=0;
    check(control_->GetExecutionStatus(&status),"GetExecutionStatus");
    check(systems_->GetCurrentProcessId(&p),"GetCurrentProcessId");
    Com<IDebugSystemObjects3> sys3(systems_.get(),IID_IDebugSystemObjects3);
    check(sys3->GetCurrentSystemId(&s),"GetCurrentSystemId");
    check(control_->GetEffectiveProcessorType(&machine),"GetEffectiveProcessorType");
    if(status!=DEBUG_STATUS_BREAK || p!=process_ || s!=system_ || machine!=machine_ || generation_!=session_generation.load())
        throw std::runtime_error("Debugger target changed during the query; snapshot invalidated");
}

class ScopeGuard {
    IDebugControl4* control_;
    IDebugSystemObjects* systems_;
    IDebugSymbols3* symbols_;
    ULONG thread_ = 0;
    ULONG64 ip_ = 0;
    DEBUG_STACK_FRAME frame_{};
    CONTEXT context_{};
    bool restored_ = false;
public:
    ScopeGuard(IDebugControl4* control,IDebugSystemObjects* systems,IDebugSymbols3* symbols):
        control_(control),systems_(systems),symbols_(symbols) {
        check(systems_->GetCurrentThreadId(&thread_),"Save current thread");
        check(symbols_->GetScope(&ip_,&frame_,&context_,sizeof(context_)),"Save debugger scope");
    }
    void restore() {
        if(restored_) return;
        ULONG machine=0;
        check(control_->GetEffectiveProcessorType(&machine),"Check scope architecture");
        if(!native_target(native_machine,machine))
            throw std::runtime_error("Effective processor changed; saved scope cannot be restored safely");
        check(systems_->SetCurrentThreadId(thread_),"Restore current thread");
        check(symbols_->SetScope(ip_,&frame_,&context_,sizeof(context_)),"Restore debugger scope");
        restored_=true;
    }
    ~ScopeGuard() {
        ULONG machine=0;
        if(!restored_ && SUCCEEDED(control_->GetEffectiveProcessorType(&machine)) && native_target(native_machine,machine)) {
            systems_->SetCurrentThreadId(thread_);
            symbols_->SetScope(ip_,&frame_,&context_,sizeof(context_));
        }
    }
};

std::string Debugger::snapshot(std::uint64_t id,const Limits& limits) {
    require_stopped(); ScopeGuard saved(control_.get(),systems_.get(),symbols_.get());
    const auto started=GetTickCount64();
    auto budget=[&] {
        check_session();
        if(GetTickCount64()-started > limits.time_ms)
            throw std::runtime_error("Snapshot collection exceeded time budget; increase !prolog.limits if needed");
    };
    std::string out="["; bool first=true;
    const std::string sid=std::to_string(id);
    auto fact=[&](const std::string& f) {
        if(!first) out+=',';
        first=false; out+=f;
        if(out.size()>8u*1024u*1024u) throw std::runtime_error("Snapshot exceeded the 8 MiB fact limit");
    };
    ULONG os_pid=0,klass=0,qual=0;
    check(systems_->GetCurrentProcessSystemId(&os_pid),"Get process OS id");
    check(control_->GetDebuggeeType(&klass,&qual),"Get target type");
    std::string kind=qual>=DEBUG_USER_WINDOWS_SMALL_DUMP ? "dump" : "live";
    fact("target("+sid+","+kind+","+std::to_string(os_pid)+",8)");
    fact("architecture("+sid+","+architecture_name(machine_)+")");
    ULONG total=0;
    check(systems_->GetNumberThreads(&total),"GetNumberThreads");
    ULONG count=std::min<ULONG>(total,limits.threads);
    std::vector<ULONG> ids(count),osids(count);
    if(count) check(systems_->GetThreadIdsByIndex(0,count,ids.data(),osids.data()),"GetThreadIdsByIndex");
    fact("coverage("+sid+",threads,"+(count==total ? std::string("enumerated(")+std::to_string(count)+")"
         : "truncated("+std::to_string(count)+","+std::to_string(total)+")")+")");
    std::set<ULONG64> resolved;
    for(ULONG i=0;i<count;++i) {
        budget(); const std::string tid=std::to_string(ids[i]);
        fact("thread("+sid+","+tid+")");
        fact("thread_id("+sid+","+tid+","+std::to_string(osids[i])+")");
        HRESULT hr=systems_->SetCurrentThreadId(ids[i]);
        if(FAILED(hr)) { fact("coverage("+sid+",stack("+tid+"),unavailable("+atom(hr_text(hr))+"))"); continue; }
        // Reset symbol scope to this thread's frame zero before stack capture.
        check(symbols_->ResetScope(),"Reset thread scope");
        std::vector<DEBUG_STACK_FRAME> frames(limits.frames);
        ULONG filled=0;
        hr=control_->GetStackTrace(0,0,0,frames.data(),limits.frames,&filled);
        if(filled>limits.frames) throw std::runtime_error("Invalid stack frame count returned by debugger");
        if(FAILED(hr) || !filled) {
            fact("coverage("+sid+",stack("+tid+"),unavailable("+atom(hr_text(hr))+"))");
            continue;
        }
        // DbgEng does not certify a complete unwind. Never label the prefix complete.
        fact("coverage("+sid+",stack("+tid+"),captured_prefix("+std::to_string(filled)+","+
             (filled==limits.frames ? "frame_limit" : "unverified_end")+"))");
        for(ULONG depth=0;depth<filled;++depth) {
            budget(); const auto& f=frames[depth];
            std::string ip=std::to_string(f.InstructionOffset);
            fact("stack_frame("+sid+","+tid+","+std::to_string(depth)+","+ip+")");
            fact("stack_pointer("+sid+","+tid+","+std::to_string(depth)+","+std::to_string(f.StackOffset)+")");
            if(!resolved.insert(f.InstructionOffset).second) continue;
            WCHAR name[4096]{}; ULONG used=0; ULONG64 displacement=0;
            hr=symbols_->GetNameByOffsetWide(f.InstructionOffset,name,4096,&used,&displacement);
            if(hr!=S_OK || used>4096) {
                fact("symbol_status("+sid+","+ip+",unavailable("+atom(hr_text(hr))+"))"); continue;
            }
            std::string full=utf8(name); auto bang=full.find('!');
            if(bang==full.npos) { fact("symbol_status("+sid+","+ip+",unavailable('No qualified symbol'))"); continue; }
            fact("symbol("+sid+","+ip+","+atom(full.substr(0,bang))+","+atom(full.substr(bang+1))+","+std::to_string(displacement)+")");
            fact("symbol_status("+sid+","+ip+",resolved)");
        }
    }
    budget();
    ULONG loaded=0,unloaded=0;
    HRESULT hr=symbols_->GetNumberModules(&loaded,&unloaded);
    if(FAILED(hr)) fact("coverage("+sid+",modules,unavailable("+atom(hr_text(hr))+"))");
    else {
        const ULONG cap=std::min<ULONG>(loaded,4096); ULONG captured=0;
        for(ULONG i=0;i<cap;++i) {
            budget(); ULONG64 base=0; DEBUG_MODULE_PARAMETERS p{}; WCHAR name[4096]{}; ULONG used=0;
            if(FAILED(symbols_->GetModuleByIndex(i,&base))) continue;
            if(FAILED(symbols_->GetModuleParameters(1,&base,0,&p))) continue;
            if(symbols_->GetModuleNameStringWide(DEBUG_MODNAME_MODULE,i,base,name,4096,&used)!=S_OK || used>4096) continue;
            fact("module("+sid+","+atom(utf8(name))+","+std::to_string(base)+","+std::to_string(p.Size)+")"); ++captured;
        }
        fact("coverage("+sid+",modules,"+(captured==loaded ? "enumerated("+std::to_string(captured)+")"
             : "partial("+std::to_string(captured)+","+std::to_string(loaded)+")")+")");
    }
    saved.restore(); check_session(); return out+"]";
}

std::vector<std::string> Debugger::rpc(const std::vector<std::string>& f,std::uint64_t id) {
    if(f.size()<3 || number(f[1])!=id) throw std::runtime_error("Mismatched worker request");
    number(f[2]); check_session();
    if(f[0]=="READ" && f.size()==5) {
        const auto address=number(f[3],16),length=number(f[4]);
        if(!valid_range(address,length)) throw std::runtime_error("Worker requested an invalid memory range");
        bytes_read_+=static_cast<std::size_t>(length);
        if(bytes_read_>16u*1024u*1024u) throw std::runtime_error("Query exceeded the 16 MiB memory-read budget");
        std::string data(static_cast<std::size_t>(length),'\0'); ULONG read=0;
        HRESULT hr=length ? memory_->ReadVirtual(address,data.data(),static_cast<ULONG>(length),&read) : S_OK;
        check_session();
        if(read>length) throw std::runtime_error("Invalid byte count returned by debugger");
        data.resize(read);
        std::string state=read==length && SUCCEEDED(hr) ? "ok" : read ? "partial" : "unavailable";
        std::string reason=state=="ok" ? "" : hr_text(hr)+"; requested "+std::to_string(length)+", read "+std::to_string(read);
        return {"MEM",f[1],f[2],state,hex(data),hex(reason)};
    }
    if(f[0]=="LOOKUP" && f.size()==4) {
        auto name=widen(unhex_text(f[3]));
        if(name.size()>4096 || name.find(L'\0')!=name.npos) throw std::runtime_error("Invalid symbol name");
        ULONG64 address=0; HRESULT hr=symbols_->GetOffsetByNameWide(name.c_str(),&address);
        check_session();
        if(FAILED(hr)) return {"ADDRESS",f[1],f[2],"unavailable",hex(hr_text(hr))};
        std::ostringstream a; a<<std::hex<<address;
        return {"ADDRESS",f[1],f[2],"ok",a.str()};
    }
    throw std::runtime_error("Unknown worker RPC");
}
}
