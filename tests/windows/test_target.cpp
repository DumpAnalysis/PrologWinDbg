#include <windows.h>
#include <dbghelp.h>
#include <cstdio>
#include <cstring>
#include <cstdint>

// Named exports make the smoke tests usable even without private PDB symbols.
extern "C" {
__declspec(dllexport) unsigned char g_magic[8]={'P','R','O','L','O','G','D','B'};
__declspec(dllexport) unsigned char* g_partial=nullptr;
__declspec(dllexport) unsigned char* g_missing=nullptr;
__declspec(dllexport) volatile LONG g_ready=0;
}
static HANDLE stop_event=nullptr;
extern "C" __declspec(dllexport) __declspec(noinline) DWORD WINAPI WaitForRelease(LPVOID) {
    InterlockedIncrement(&g_ready);
    WaitForSingleObject(stop_event,INFINITE);
    return 0;
}
extern "C" __declspec(dllexport) __declspec(noinline) void CapturePoint() {
    if(IsDebuggerPresent()) DebugBreak();
}
static bool write_dump(const char* path,MINIDUMP_TYPE type) {
    HANDLE file=CreateFileA(path,GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE) return false;
    bool ok=MiniDumpWriteDump(GetCurrentProcess(),GetCurrentProcessId(),file,type,nullptr,nullptr,nullptr)!=FALSE;
    CloseHandle(file);return ok;
}
int main(int argc,char** argv) {
    SYSTEM_INFO si{};GetSystemInfo(&si);
    auto block=static_cast<unsigned char*>(VirtualAlloc(nullptr,si.dwPageSize*2,MEM_RESERVE,PAGE_NOACCESS));
    if(!block || !VirtualAlloc(block,si.dwPageSize,MEM_COMMIT,PAGE_READWRITE)) return 2;
    std::memcpy(block+si.dwPageSize-4,"TAIL",4);
    g_partial=block+si.dwPageSize-4; g_missing=block+si.dwPageSize;
    stop_event=CreateEventW(nullptr,TRUE,FALSE,nullptr); if(!stop_event) return 2;
    HANDLE threads[2]={CreateThread(nullptr,0,WaitForRelease,nullptr,0,nullptr),CreateThread(nullptr,0,WaitForRelease,nullptr,0,nullptr)};
    if(!threads[0] || !threads[1]) return 2;
    while(InterlockedCompareExchange(&g_ready,0,0)<2) Sleep(1);
    // Give both workers time to enter the wait, for stable demonstration stacks.
    Sleep(50);
    std::printf("PID=%lu magic=%p partial=%p missing=%p\n",GetCurrentProcessId(),g_magic,g_partial,g_missing);
    std::fflush(stdout);
    int result=0;
    if(argc==3 && std::strcmp(argv[1],"--dump")==0) {
        if(!write_dump(argv[2],MiniDumpWithFullMemory)) { std::fprintf(stderr,"Dump failed: %lu\n",GetLastError());result=3; }
    } else if(argc==3 && std::strcmp(argv[1],"--minidump")==0) {
        if(!write_dump(argv[2],MiniDumpNormal)) result=3;
    } else if(argc==2 && std::strcmp(argv[1],"--live")==0) {
        CapturePoint();
    } else if(argc==2 && std::strcmp(argv[1],"--wait")==0) {
        std::puts("Attach WinDbg to this PID, then break. Waiting for up to 10 minutes.");
        WaitForSingleObject(stop_event,600000);
    } else {
        std::puts("Usage: prologdbg_target.exe --dump file.dmp | --minidump file.dmp | --live | --wait");
        result=1;
    }
    SetEvent(stop_event);WaitForMultipleObjects(2,threads,TRUE,5000);
    CloseHandle(threads[0]);CloseHandle(threads[1]);CloseHandle(stop_event);
    VirtualFree(block,0,MEM_RELEASE);return result;
}
