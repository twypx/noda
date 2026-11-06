#include "EncodingConverter.h"

#include <iconv.h>
#include <cerrno>
#include <cctype>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <langinfo.h>
#include <map>
#include <algorithm>

namespace Encoding {

namespace {

constexpr std::size_t kChunk = 1u << 20; // 1 MiB 工作缓冲

// 去掉空格/连字符/下划线并转小写，用于编码名别名匹配。
std::string canonName(std::string_view s) {
    std::string r;
    r.reserve(s.size());
    for (char c : s) {
        if (c == ' ' || c == '-' || c == '_') continue;
        r.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return r;
}

} // namespace

bool isUtf8Like(std::string_view encName) {
    std::string n = canonName(encName);
    // UTF-8 / UTF-8 BOM / ASCII / 未知一律按透传处理（BOM 由调用方剥离）。
    return n.empty() || n == "utf8" || n == "utf8bom" || n == "ascii" || n == "unknownencoding";
}

std::string iconvNameFor(std::string_view encName) {
    std::string n = canonName(encName);
    if (n.empty() || n == "utf8" || n == "utf8bom" || n == "ascii") return "";

    if (n == "ansi") {
        const char* cs = nl_langinfo(CODESET);
        return (cs && *cs) ? std::string(cs) : std::string("ISO-8859-1");
    }
    if (n == "utf16le" || n == "utf16" || n == "utf16lebom") return "UTF-16LE";
    if (n == "utf16be" || n == "utf16bebom") return "UTF-16BE";

    static const std::map<std::string, std::string> alias = {
        {"gb2312", "GB2312"}, {"gbk", "GBK"}, {"gb18030", "GB18030"},
        {"big5", "BIG5"}, {"cp950", "BIG5"},
        {"shiftjis", "SHIFT-JIS"}, {"sjis", "SHIFT-JIS"},
        {"eucjp", "EUC-JP"}, {"euckr", "EUC-KR"},
        {"iso88591", "ISO-8859-1"}, {"latin1", "ISO-8859-1"},
        {"windows1250", "CP1250"}, {"cp1250", "CP1250"},
        {"windows1251", "CP1251"}, {"cp1251", "CP1251"},
        {"windows1252", "CP1252"}, {"cp1252", "CP1252"},
        {"koi8r", "KOI8-R"}, {"tis620", "TIS-620"},
        {"iso88592", "ISO-8859-2"}, {"iso885915", "ISO-8859-15"},
    };
    auto it = alias.find(n);
    if (it != alias.end()) return it->second;

    // 未识别：原样交给 iconv 试探（不支持时上层会退化为透传）。
    return std::string(encName);
}

bool isUtf16(std::string_view encName) {
    std::string n = canonName(encName);
    return n.rfind("utf16", 0) == 0; // utf16 / utf16le / utf16be / *bom
}

std::size_t bomLength(const char* p, std::size_t n) {
    if (!p || n == 0) return 0;
    const auto* b = reinterpret_cast<const unsigned char*>(p);
    if (n >= 3 && b[0] == 0xEF && b[1] == 0xBB && b[2] == 0xBF) return 3; // UTF-8 BOM
    if (n >= 2 && b[0] == 0xFF && b[1] == 0xFE) return 2;                 // UTF-16LE
    if (n >= 2 && b[0] == 0xFE && b[1] == 0xFF) return 2;                 // UTF-16BE
    return 0;
}

std::string bomForSave(std::string_view encName) {
    std::string n = canonName(encName);
    if (n == "utf8bom") return std::string("\xEF\xBB\xBF", 3);
    if (n.rfind("utf16le", 0) == 0 || n == "utf16") return std::string("\xFF\xFE", 2);
    if (n.rfind("utf16be", 0) == 0) return std::string("\xFE\xFF", 2);
    return std::string();
}

bool looksBinary(const char* p, std::size_t n, std::string_view encName) {
    if (isUtf16(encName)) return false; // UTF-16 天然含 NUL 字节
    if (!p || n == 0) return false;
    std::size_t scan = std::min<std::size_t>(n, 8192);
    for (std::size_t i = 0; i < scan; ++i) {
        if (p[i] == '\0') return true;
    }
    return false;
}

bool decodeToUtf8Chunks(const char* in, std::size_t inLen, std::string_view encName, const ChunkSink& sink) {
    if (isUtf8Like(encName)) {
        // 透传快路径：ADDTEXT 分块不影响文档内 UTF-8 字节的最终连续性（样式在全部
        // 文本载入后才重算），因此无需跨块保留不完整多字节序列。
        for (std::size_t off = 0; off < inLen; off += kChunk) {
            std::size_t n = std::min(kChunk, inLen - off);
            sink(in + off, n);
        }
        return true;
    }

    std::string iname = iconvNameFor(encName);
    if (iname.empty()) { sink(in, inLen); return true; }

    iconv_t cd = iconv_open("UTF-8", iname.c_str());
    if (cd == reinterpret_cast<iconv_t>(-1)) {
        // 目标编码不受支持：退化为透传，保证不丢内容（等同旧行为）。
        sink(in, inLen);
        return false;
    }

    std::string out;
    out.resize(kChunk);
    static const char kReplacement[] = {'\xEF', '\xBF', '\xBD'}; // U+FFFD

    char* inptr = const_cast<char*>(in);
    std::size_t inleft = inLen;
    bool ok = true;
    for (;;) {
        char* outptr = out.data();
        std::size_t outleft = out.size();
        std::size_t r = iconv(cd, &inptr, &inleft, &outptr, &outleft);
        if (std::size_t produced = outptr - out.data()) sink(out.data(), produced);
        if (r != static_cast<std::size_t>(-1)) break; // 转换完成

        int e = errno;
        if (e == E2BIG) {
            continue; // 输出缓冲写满，已 flush，继续
        } else if (e == EINVAL) {
            sink(kReplacement, 3); // 输入尾部不完整序列
            break;
        } else if (e == EILSEQ) {
            sink(kReplacement, 3); // 非法字节，跳过一个字节后继续
            if (inleft > 0) { ++inptr; --inleft; } else break;
        } else {
            ok = false;
            break;
        }
    }
    iconv_close(cd);
    return ok;
}

bool encodeFromUtf8Chunks(const char* utf8, std::size_t len, std::string_view encName, const ChunkSink& sink) {
    if (isUtf8Like(encName)) { // UTF-8 族：原样写出（BOM 由上层写）
        if (len) sink(utf8, len);
        return true;
    }

    std::string oname = iconvNameFor(encName);
    if (oname.empty()) { if (len) sink(utf8, len); return true; }

    iconv_t cd = iconv_open(oname.c_str(), "UTF-8");
    if (cd == reinterpret_cast<iconv_t>(-1)) {
        if (len) sink(utf8, len); // 无法转换：至少保证内容可保存
        return false;
    }

    std::string out;
    out.resize(kChunk);
    char* inptr = const_cast<char*>(utf8);
    std::size_t inleft = len;
    bool ok = true;
    for (;;) {
        char* outptr = out.data();
        std::size_t outleft = out.size();
        std::size_t r = iconv(cd, &inptr, &inleft, &outptr, &outleft);
        if (std::size_t produced = outptr - out.data()) sink(out.data(), produced);
        if (r != static_cast<std::size_t>(-1)) break;

        int e = errno;
        if (e == E2BIG) {
            continue;
        } else if (e == EINVAL || e == EILSEQ) {
            sink("?", 1); // 目标编码无法表示：以 '?' 占位，跳过源字节
            if (inleft > 0) { ++inptr; --inleft; } else break;
        } else {
            ok = false;
            break;
        }
    }
    iconv_close(cd);
    return ok;
}

} // namespace Encoding
