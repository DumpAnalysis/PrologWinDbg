#pragma once
#include <windows.h>
#include <unknwn.h>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sstream>
#include <utility>

namespace pd {
inline std::string win_error(const char* operation, DWORD code = GetLastError()) {
    return std::string(operation) + " (Windows error " + std::to_string(code) + ")";
}
inline std::string hr_text(HRESULT hr) {
    std::ostringstream s; s << "HRESULT 0x" << std::hex << static_cast<unsigned long>(hr);
    return s.str();
}
inline void check(HRESULT hr, const char* operation) {
    if (FAILED(hr)) throw std::runtime_error(std::string(operation) + ": " + hr_text(hr));
}
inline std::wstring widen(std::string_view s, UINT cp = CP_UTF8) {
    if (s.empty()) return {};
    const DWORD flags = cp == CP_UTF8 ? MB_ERR_INVALID_CHARS : 0;
    int n = MultiByteToWideChar(cp,flags,s.data(),static_cast<int>(s.size()),nullptr,0);
    if (!n) throw std::runtime_error(win_error("Decode text"));
    std::wstring out(n,L'\0');
    MultiByteToWideChar(cp,flags,s.data(),static_cast<int>(s.size()),out.data(),n); return out;
}
inline std::string utf8(std::wstring_view s) {
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0,nullptr,nullptr);
    if (!n) throw std::runtime_error(win_error("Encode text"));
    std::string out(n,'\0');
    WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),out.data(),n,nullptr,nullptr); return out;
}
inline std::wstring environment(const wchar_t* name) {
    DWORD size = GetEnvironmentVariableW(name,nullptr,0);
    if (!size) return {};
    std::wstring result(size,L'\0');
    DWORD used = GetEnvironmentVariableW(name,result.data(),size);
    if (used >= size) throw std::runtime_error("Environment variable changed while reading");
    result.resize(used); return result;
}
class Handle {
    HANDLE h_ = nullptr;
public:
    Handle() = default;
    explicit Handle(HANDLE h): h_(h) {}
    ~Handle() { reset(); }
    Handle(const Handle&) = delete; Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept: h_(other.release()) {}
    Handle& operator=(Handle&& other) noexcept { if(this!=&other) reset(other.release()); return *this; }
    HANDLE get() const { return h_; }
    explicit operator bool() const { return h_ && h_ != INVALID_HANDLE_VALUE; }
    HANDLE release() { HANDLE h=h_; h_=nullptr; return h; }
    void reset(HANDLE h=nullptr) { if(*this) CloseHandle(h_); h_=h; }
};
template<class T> class Com {
    T* ptr_ = nullptr;
public:
    Com(IUnknown* source,REFIID iid) { check(source->QueryInterface(iid,reinterpret_cast<void**>(&ptr_)),"QueryInterface"); }
    ~Com() { if(ptr_) ptr_->Release(); }
    Com(const Com&) = delete; Com& operator=(const Com&) = delete;
    T* operator->() const { return ptr_; }
    T* get() const { return ptr_; }
};
}
