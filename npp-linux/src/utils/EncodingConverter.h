#pragma once
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>

// 统一的磁盘编码 <-> 内部 UTF-8 转码工具。
//
// 设计目标：
//  - 真正参与加载/保存（uchardet 只负责“识别”，这里负责“转换”），修复历史上
//    “探测到 GBK/UTF-16 却原样当 UTF-8 塞进 Scintilla”导致的乱码问题。
//  - 使用可分块处理的 iconv 流式转换，配合调用方的 mmap 只读缓冲，超大文件的
//    峰值内存仅由“分块缓冲 + 文档本体”决定，不额外做一次全文堆拷贝，
//    因此加载性能不劣于原版 Notepad++（纯 UTF-8/ASCII 走零转换透传快路径）。
namespace Encoding {

// 每产出一段 UTF-8/目标字节就回调一次（sink 负责写入 Scintilla 或文件）。
using ChunkSink = std::function<void(const char* data, std::size_t len)>;

// 编码名是否为可直接透传的 UTF-8/ASCII 族（无需 iconv）。
bool isUtf8Like(std::string_view encName);

// 把 uchardet / BOM / 菜单得到的编码名规范化为 iconv 可识别名；透传族返回空串。
std::string iconvNameFor(std::string_view encName);

// 编码名是否为 UTF-16 族（其源字节天然含 NUL，二进制判定需跳过）。
bool isUtf16(std::string_view encName);

// 原始字节 [in,in+inLen) 按 encName 解码为 UTF-8，分块回调 sink（BOM 由调用方先行剥离）。
bool decodeToUtf8Chunks(const char* in, std::size_t inLen, std::string_view encName, const ChunkSink& sink);

// UTF-8 文本 [utf8,len) 按 encName 编码到目标字节，分块回调 sink（BOM 由调用方先行写入）。
bool encodeFromUtf8Chunks(const char* utf8, std::size_t len, std::string_view encName, const ChunkSink& sink);

// 依据头部字节返回 BOM 长度（0 / 2 / 3）。
std::size_t bomLength(const char* p, std::size_t n);

// 保存为该编码时应前置写入的 BOM 字节（无则返回空串）。
// 名称容忍空格/连字符差异（"UTF-16 LE BOM" 与 "UTF-16LE" 等等价）。
std::string bomForSave(std::string_view encName);

// 粗略判断是否为二进制文件（头部含 NUL 且非 UTF-16/32）。
bool looksBinary(const char* p, std::size_t n, std::string_view encName);

} // namespace Encoding
