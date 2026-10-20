#include "worker_process.hpp"
#include "protocol.hpp"
#include <algorithm>
#include <atomic>
#include <thread>
#include <vector>

namespace pd {
bool WorkerProcess::running() const { return process_ && WaitForSingleObject(process_.get(),0)==WAIT_TIMEOUT; }
void WorkerProcess::stop() noexcept {
    // The job owns only this worker and its descendants. Closing it kills them.
    if(process_ && running()) TerminateProcess(process_.get(),1);
    job_.reset();
    if(process_) WaitForSingleObject(process_.get(),1000);
    process_.reset(); input_.reset(); output_.reset(); error_.reset();
    pending_.clear(); diagnostics_.clear(); version_.clear();
}
void WorkerProcess::drain_errors() {
    if(!error_) return;
    DWORD available=0;
    if(!PeekNamedPipe(error_.get(),nullptr,0,nullptr,&available,nullptr)) return;
    while(available) {
        char b[4096]; DWORD n=0;
        if(!ReadFile(error_.get(),b,std::min<DWORD>(available,sizeof(b)),&n,nullptr) || !n) return;
        diagnostics_.append(b,n);
        if(diagnostics_.size()>16384) diagnostics_.erase(0,diagnostics_.size()-16384);
        available-=n;
    }
}
std::string WorkerProcess::line(Debugger& dbg,ULONGLONG deadline) {
    for(;;) {
        dbg.check_interrupt(); drain_errors();
        auto eol=pending_.find('\n');
        if(eol!=pending_.npos) {
            std::string result=pending_.substr(0,eol); pending_.erase(0,eol+1);
            if(!result.empty() && result.back()=='\r') result.pop_back();
            return result;
        }
        if(GetTickCount64()>=deadline) throw std::runtime_error("Worker deadline exceeded; runtime will be reset");
        DWORD available=0;
        if(!PeekNamedPipe(output_.get(),nullptr,0,nullptr,&available,nullptr))
            throw std::runtime_error("Worker pipe closed. "+diagnostics_);
        if(available) {
            char b[8192]; DWORD n=0;
            if(!ReadFile(output_.get(),b,std::min<DWORD>(available,sizeof(b)),&n,nullptr))
                throw std::runtime_error(win_error("Read worker output"));
            pending_.append(b,n);
            if(pending_.size()>max_line) throw std::runtime_error("Worker output exceeded protocol limit");
        } else {
            if(!running()) throw std::runtime_error("Prolog worker exited. "+diagnostics_);
            Sleep(10);
        }
    }
}
void WorkerProcess::write(const std::string& text,Debugger& dbg,ULONGLONG deadline) {
    // Anonymous pipes do not support overlapped writes. A dedicated writer has no
    // debugger interfaces; the command thread polls for interruption and timeout.
    std::atomic<bool> done{false}; std::atomic<DWORD> failure{ERROR_SUCCESS};
    HANDLE pipe=input_.get();
    std::thread writer([&] {
        std::size_t at=0;
        while(at<text.size()) {
            DWORD n=0;
            if(!WriteFile(pipe,text.data()+at,static_cast<DWORD>(std::min<std::size_t>(text.size()-at,65536)),&n,nullptr) || !n) {
                failure=GetLastError(); if(failure==ERROR_SUCCESS) failure=ERROR_WRITE_FAULT; break;
            }
            at+=n;
        }
        done=true;
    });
    try {
        while(!done) {
            dbg.check_interrupt(); drain_errors();
            if(GetTickCount64()>=deadline || !running()) throw std::runtime_error("Worker input deadline exceeded");
            Sleep(5);
        }
        writer.join();
        if(failure!=ERROR_SUCCESS) throw std::runtime_error(win_error("Write worker request",failure));
    } catch(...) {
        // Killing the worker closes the pipe reader and releases a blocked write.
        if(process_) TerminateProcess(process_.get(),1);
        job_.reset();
        if(writer.joinable()) writer.join();
        throw;
    }
}

void WorkerProcess::start(const std::wstring& exe,const std::wstring& script,Debugger& dbg) {
    if(running() && executable_==exe && script_==script) return;
    stop(); executable_=exe;script_=script;
    SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};
    HANDLE read=nullptr,write=nullptr;
    if(!CreatePipe(&read,&write,&sa,65536)) throw std::runtime_error(win_error("Create worker input"));
    Handle child_in(read); input_.reset(write);
    if(!CreatePipe(&read,&write,&sa,65536)) { stop(); throw std::runtime_error(win_error("Create worker output")); }
    output_.reset(read); Handle child_out(write);
    if(!CreatePipe(&read,&write,&sa,65536)) { stop(); throw std::runtime_error(win_error("Create worker error pipe")); }
    error_.reset(read); Handle child_err(write);
    if(!SetHandleInformation(input_.get(),HANDLE_FLAG_INHERIT,0) ||
       !SetHandleInformation(output_.get(),HANDLE_FLAG_INHERIT,0) ||
       !SetHandleInformation(error_.get(),HANDLE_FLAG_INHERIT,0))
        throw std::runtime_error(win_error("Configure pipe inheritance"));
    SIZE_T bytes=0; InitializeProcThreadAttributeList(nullptr,1,0,&bytes);
    std::vector<unsigned char> storage(bytes);
    auto attributes=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    if(!InitializeProcThreadAttributeList(attributes,1,0,&bytes)) throw std::runtime_error(win_error("Create handle list"));
    struct AttributeGuard { LPPROC_THREAD_ATTRIBUTE_LIST p; ~AttributeGuard(){DeleteProcThreadAttributeList(p);} } ag{attributes};
    HANDLE handles[]={child_in.get(),child_out.get(),child_err.get()};
    if(!UpdateProcThreadAttribute(attributes,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,handles,sizeof(handles),nullptr,nullptr))
        throw std::runtime_error(win_error("Set child handle list"));
    STARTUPINFOEXW si{}; si.StartupInfo.cb=sizeof(si); si.lpAttributeList=attributes;
    si.StartupInfo.dwFlags=STARTF_USESTDHANDLES;
    si.StartupInfo.hStdInput=child_in.get(); si.StartupInfo.hStdOutput=child_out.get(); si.StartupInfo.hStdError=child_err.get();
    std::wstring command=quote_arg(exe)+L" -q -f none --no-packs --stack-limit=512m -s "+quote_arg(script)+
        L" -g prologdbg_worker:main -t halt --";
    job_.reset(CreateJobObjectW(nullptr,nullptr));
    if(!job_) throw std::runtime_error(win_error("Create worker job"));
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limit{};
    limit.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE|JOB_OBJECT_LIMIT_PROCESS_MEMORY;
    limit.ProcessMemoryLimit=768u*1024u*1024u;
    if(!SetInformationJobObject(job_.get(),JobObjectExtendedLimitInformation,&limit,sizeof(limit)))
        throw std::runtime_error(win_error("Limit worker memory"));
    PROCESS_INFORMATION pi{};
    if(!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,TRUE,
        CREATE_NO_WINDOW|CREATE_SUSPENDED|EXTENDED_STARTUPINFO_PRESENT,nullptr,nullptr,&si.StartupInfo,&pi))
        throw std::runtime_error(win_error("Start swipl.exe"));
    process_.reset(pi.hProcess); Handle thread(pi.hThread);
    if(!AssignProcessToJobObject(job_.get(),process_.get())) { auto error=GetLastError(); stop(); throw std::runtime_error(win_error("Assign worker job",error)); }
    if(ResumeThread(thread.get())==DWORD(-1)) { auto error=GetLastError();stop();throw std::runtime_error(win_error("Resume worker",error)); }
    child_in.reset();child_out.reset();child_err.reset();
    try {
        auto hello=fields(line(dbg,GetTickCount64()+15000));
        if(hello.size()!=3 || hello[0]!="HELLO" || hello[1]!="1") throw std::runtime_error("Invalid worker handshake");
        version_=unhex_text(hello[2]);
    } catch(...) { stop();throw; }
}

void WorkerProcess::transact(const std::vector<std::string>& request,Debugger& dbg,std::uint64_t id,unsigned deadline_ms,bool allow_rpc) {
    const auto deadline=GetTickCount64()+deadline_ms;
    try {
        write(message(request),dbg,deadline);
        unsigned rpc_count=0; std::size_t output_count=0;
        for(;;) {
            auto f=fields(line(dbg,deadline));
            if(f.size()<2 || number(f[1])!=id) throw std::runtime_error("Worker response id mismatch");
            if((f[0]=="READ" || f[0]=="LOOKUP") && allow_rpc) {
                if(++rpc_count>10000) throw std::runtime_error("Query exceeded RPC budget");
                write(message(dbg.rpc(f,id)),dbg,deadline);
            } else if((f[0]=="ANSWER" || f[0]=="OUTPUT") && f.size()==3) {
                std::string text=unhex_text(f[2]); output_count+=text.size();
                if(output_count>4u*1024u*1024u) throw std::runtime_error("Query exceeded output budget");
                if(f[0]=="ANSWER") text+="\n";
                dbg.output(text);
            } else if(f[0]=="DONE" && f.size()==5) {
                if(allow_rpc) dbg.check_session();
                number(f[3]);
                dbg.output("[prolog] "+f[2]+"; "+f[3]+" answer(s). "+unhex_text(f[4])+"\n");
                return;
            } else throw std::runtime_error("Unexpected worker response");
        }
    } catch(...) { stop(); throw; }
}
}
