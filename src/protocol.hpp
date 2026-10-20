#pragma once
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace pd {
constexpr std::size_t max_line = 32u * 1024u * 1024u;
constexpr std::uint64_t max_read = 65536;
inline char hex_digit(unsigned x) { return "0123456789abcdef"[x & 15]; }
inline unsigned unhex(char c) {
    if (c >= '0' && c <= '9') return unsigned(c - '0');
    if (c >= 'a' && c <= 'f') return unsigned(c - 'a' + 10);
    if (c >= 'A' && c <= 'F') return unsigned(c - 'A' + 10);
    throw std::runtime_error("Invalid hexadecimal field");
}
inline std::string hex(std::string_view s) {
    std::string out; out.reserve(s.size() * 2);
    for (unsigned char c : s) { out += hex_digit(c >> 4); out += hex_digit(c); }
    return out;
}
inline std::string unhex_text(std::string_view s) {
    if (s.size() % 2) throw std::runtime_error("Odd hexadecimal field length");
    std::string out; out.reserve(s.size() / 2);
    for (std::size_t i = 0; i < s.size(); i += 2)
        out += static_cast<char>((unhex(s[i]) << 4) | unhex(s[i+1]));
    return out;
}
inline std::uint64_t number(std::string_view s, unsigned base = 10) {
    if (s.empty() || (base != 10 && base != 16)) throw std::runtime_error("Invalid integer");
    std::uint64_t value = 0;
    for (char c : s) {
        unsigned d = unhex(c);
        if (d >= base || value > (std::numeric_limits<std::uint64_t>::max() - d) / base)
            throw std::runtime_error("Integer is out of range");
        value = value * base + d;
    }
    return value;
}
inline std::vector<std::string> fields(std::string_view line) {
    std::vector<std::string> result;
    std::size_t from = 0;
    for (;;) {
        auto at = line.find('\t', from);
        result.emplace_back(line.substr(from, at == line.npos ? at : at - from));
        if (at == line.npos) return result;
        from = at + 1;
    }
}
inline std::string message(const std::vector<std::string>& parts) {
    std::string out;
    for (const auto& part : parts) {
        if (part.find_first_of("\t\r\n") != part.npos) throw std::runtime_error("Invalid protocol field");
        if (!out.empty()) out += '\t';
        out += part;
    }
    if (out.size() > max_line) throw std::runtime_error("Protocol message too large");
    return out + '\n';
}
// Input is UTF-8; quoted Prolog atoms preserve it. Escape syntax and controls.
inline std::string atom(std::string_view text) {
    std::string out = "'";
    for (unsigned char c : text) {
        if (c == '\\' || c == '\'') { out += '\\'; out += char(c); }
        else if (c < 32 || c == 127) {
            out += "\\u00"; out += hex_digit(c >> 4); out += hex_digit(c);
        } else out += char(c);
    }
    return out + "'";
}
inline bool valid_range(std::uint64_t address, std::uint64_t length) {
    return length <= max_read && (length == 0 || address <= UINT64_MAX - (length - 1));
}
inline std::string command_goal(std::string text) {
    auto first=text.find_first_not_of(" \t\r\n");
    if(first==text.npos) return {};
    auto last=text.find_last_not_of(" \t\r\n");
    text=text.substr(first,last-first+1);
    // Quotes protect Prolog semicolons from WinDbg's command separator.
    if(text.size()>=2 && text.front()=='"' && text.back()=='"')
        text=text.substr(1,text.size()-2);
    return text;
}
// Windows CommandLineToArgvW/MSVCRT-compatible quoting, including trailing slashes.
inline std::wstring quote_arg(std::wstring_view arg) {
    std::wstring out = L"\""; std::size_t slashes = 0;
    for (wchar_t c : arg) {
        if (c == L'\\') { ++slashes; continue; }
        if (c == L'\"') out.append(slashes * 2 + 1, L'\\');
        else out.append(slashes, L'\\');
        slashes = 0; out += c;
    }
    out.append(slashes * 2, L'\\'); out += L'\"'; return out;
}
}
