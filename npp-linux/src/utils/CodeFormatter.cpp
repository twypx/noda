#include "CodeFormatter.h"
#include "JsonFormatter.h"
#include <cstdlib>

// AStyle 的 C ABI（ASTYLE_LIB 构建），实现由 third_party/astyle 静态库提供。
// 这里只前置声明，避免把 astyle.h 的大头拉进应用侧编译。
extern "C" {
using AStyleErrorFn = void (*)(int, const char*);
using AStyleAllocFn = char* (*)(unsigned long);
char* AStyleMain(const char* source, const char* options, AStyleErrorFn, AStyleAllocFn);
}

namespace {
// 只做空白重排的保守选项：不加/删大括号、不改动任何非空白字符，
// 保证格式化前后“去掉空白后的内容逐字节一致”，从而可安全整篇替换并精确映射光标。
const char* kOptions =
    "--style=allman --indent=spaces=4 "
    "--pad-oper --pad-header --unpad-paren "
    "--align-pointer=name --align-reference=name";

void onError(int, const char*) {
    // 引擎报错（含选项解析失败）时按“未改动”处理，不打断用户。
}

char* allocMem(unsigned long n) {
    return static_cast<char*>(std::malloc(n));
}
} // namespace

namespace CodeFormatter {

bool isSupported(const std::string& langId) {
    return langId == "c" || langId == "cpp" || langId == "cs"
        || langId == "java" || langId == "objc" || langId == "json";
}

std::string format(const std::string& utf8, const std::string& langId) {
    if (utf8.empty()) return utf8;
    if (langId == "json") return JsonFormatter::format(utf8);
    char* out = AStyleMain(utf8.c_str(), kOptions, &onError, &allocMem);
    if (!out) return utf8; // 分配失败等：视为未改动
    std::string result(out);
    std::free(out);
    return result;
}

} // namespace CodeFormatter