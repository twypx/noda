#include "DocumentBuffer.h"

LineEnding DocumentBuffer::detectLineEnding(std::string_view content) {
    size_t crCount = 0;
    size_t lfCount = 0;
    size_t crlfCount = 0;

    // 超大文件仅需扫描头部样本即可判断主流换行风格，避免全文遍历拖慢打开。
    constexpr size_t kScanCap = 4u << 20; // 4 MiB
    const size_t limit = content.size() > kScanCap ? kScanCap : content.size();

    for (size_t i = 0; i < limit; ++i) {
        if (content[i] == '\r') {
            if (i + 1 < content.size() && content[i + 1] == '\n') {
                ++crlfCount;
                ++i;
            } else {
                ++crCount;
            }
        } else if (content[i] == '\n') {
            ++lfCount;
        }
    }

    if (crlfCount >= lfCount && crlfCount >= crCount && crlfCount > 0) {
        return LineEnding::CRLF;
    }
    if (lfCount >= crCount && lfCount > 0) {
        return LineEnding::LF;
    }
    if (crCount > 0) {
        return LineEnding::CR;
    }
    return LineEnding::LF;
}
