#include "protocol.hpp"
#include <iostream>
#include <functional>
#include <stdexcept>

static int checked=0;
static void expect(bool v) { ++checked;if(!v) throw std::runtime_error("Protocol assertion failed: "+std::to_string(checked)); }
static void rejects(const std::function<void()>& f) { bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}expect(rejected); }
int main() {
    using namespace pd;
    std::string raw("a\0b\n\xff",5);
    expect(unhex_text(hex(raw))==raw);
    expect(hex("PROLOGDB")=="50524f4c4f474442");
    expect(number("18446744073709551615")==UINT64_MAX);
    expect(number("ffffffffffffffff",16)==UINT64_MAX);
    rejects([]{number("18446744073709551616");});
    rejects([]{number("-1");});rejects([]{number("1x");});rejects([]{number("");});
    rejects([]{unhex_text("a");});rejects([]{unhex_text("gg");});
    expect(fields("MEM\t1\t2\tok\t\t").size()==6);
    expect(message({"A","B",""})=="A\tB\t\n");
    rejects([]{message({"A","bad\nline"});});
    expect(valid_range(UINT64_MAX,1)); expect(!valid_range(UINT64_MAX,2));
    expect(valid_range(0,65536)); expect(!valid_range(0,65537));
    expect(atom("x'y\\z\n")=="'x\\'y\\\\z\\u000a'");
    expect(quote_arg(L"C:\\some path\\")==L"\"C:\\some path\\\\\"");
    expect(quote_arg(L"a\"b")==L"\"a\\\"b\"");
    expect(command_goal("  \"(X=a;X=b).\"  ")=="(X=a;X=b).");
    expect(command_goal(" X is 2+2. ")=="X is 2+2.");
    std::cout<<checked<<" protocol checks passed\n";
}
