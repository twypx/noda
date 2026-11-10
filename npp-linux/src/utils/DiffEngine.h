#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace Diff {

enum class ChangeType {
    Unchanged,
    Added,
    Removed,
    Modified,
    Moved   // 内容相同、仅位置变化（P.7）
};

struct CharRange {
    int start = 0;
    int length = 0;
};

struct LineDiff {
    ChangeType type = ChangeType::Unchanged;
    int lineA = -1; // 0-based
    int lineB = -1; // 0-based
    int movedToLine = -1; // Moved：对侧对应行号
    bool consumed = false; // 已被 Moved 配对消费，不参与对齐/绘制
    std::vector<CharRange> intraA;
    std::vector<CharRange> intraB;
};

struct DiffOptions {
    bool ignoreWhitespace = false;
    bool ignoreCase = false;
    bool ignoreEol = true;
    bool detectMoves = true; // 检测「内容相同、仅位置变化」的块
};

struct DiffResult {
    std::vector<LineDiff> lines;
    int addedCount = 0;
    int removedCount = 0;
    int modifiedCount = 0;
    int movedCount = 0;
};

struct AlignmentItem {
    int lineA = -1; // 0-based text line, -1 if blank placeholder
    int lineB = -1; // 0-based text line, -1 if blank placeholder
    ChangeType type = ChangeType::Unchanged;
};

using AlignmentList = std::vector<AlignmentItem>;

struct LineToken {
    uint64_t hash = 0;
    std::string_view rawText;
    int lineIndex = 0;
};

class DiffEngine {
public:
    // 计算两份文本的高性能比对差异
    static DiffResult computeDiff(std::string_view textA, std::string_view textB, const DiffOptions& options = {});

    // 计算两份文档在 Scintilla Annotation 视觉对齐下的双栏行映射列表
    static AlignmentList computeAlignment(const DiffResult& diff, int totalLinesA, int totalLinesB);

    // 计算两行之间的细粒度字符差异区间
    static void computeIntraLineCharDiff(std::string_view lineA, std::string_view lineB,
                                         std::vector<CharRange>& intraA,
                                         std::vector<CharRange>& intraB,
                                         const DiffOptions& options = {});

    // 零拷贝行扫描与 64 位极速哈希提取
    static std::vector<LineToken> tokenize(std::string_view text, const DiffOptions& options);

    // wyhash：8 字节块读 + 128 位乘混合，逐字节 FNV 的数倍吞吐。
    // 忽略选项开启时才构造规范化临时串，常规路径零分配。
    static uint64_t hashLine(std::string_view line, const DiffOptions& options = {});
};

} // namespace Diff
