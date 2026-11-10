#include "DiffEngine.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>

namespace Diff {

namespace {

constexpr uint64_t kHashSeed = 0x9e3779b97f4a7c15ULL;
constexpr uint64_t kHashMul = 0xc2b2ae3d27d4eb4fULL;

// 有界 Myers 的编辑距离上限：超出即退化为整块替换。
// trace 内存为 O(kMaxMyersD^2) ≈ 4MB，保证最坏情况下也不会 OOM。
constexpr int kMaxMyersD = 512;
// 只有小区间才跑 Myers；大区间即使 D 不大，trace 也会很贵。
constexpr long long kMaxMyersCells = 262144;

// 行内字符级 Myers 的上限（行通常很短）。
constexpr int kMaxCharD = 128;
// 整块替换时最多对多少对行做行内 diff：再大就把它们只标为 Modified，
// 避免几十万行的逐行字符 Myers 造成 UI 卡顿与内存峰值。
constexpr int kMaxIntraLinePairs = 20000;

inline bool isWhitespace(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

// 128 位乘混合：wyhash 的核心运算。无 __int128 的平台退化为 64 位拆分乘。
inline uint64_t wideMulMix(uint64_t a, uint64_t b) {
#if defined(__SIZEOF_INT128__)
    const __uint128_t r = static_cast<__uint128_t>(a) * static_cast<__uint128_t>(b);
    return static_cast<uint64_t>(r) ^ static_cast<uint64_t>(r >> 64);
#else
    const uint64_t al = static_cast<uint32_t>(a), ah = a >> 32;
    const uint64_t bl = static_cast<uint32_t>(b), bh = b >> 32;
    const uint64_t p0 = al * bl, p1 = al * bh, p2 = ah * bl, p3 = ah * bh;
    const uint64_t mid = (p0 >> 32) + static_cast<uint32_t>(p1) + static_cast<uint32_t>(p2);
    return (p0 & 0xffffffffULL) | (mid << 32) ^ (p3 + (p1 >> 32) + (p2 >> 32) + (mid >> 32));
#endif
}

inline uint64_t avalanche(uint64_t x) {
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return x;
}

// wyhash 风格：8 字节块读 + 128 位乘混合，吞吐为逐字节 FNV 的数倍。
// 尾部不足 8 字节一次性读入零填充的 64 位字，无分支逐字节循环。
uint64_t fastHash(const void* key, size_t len) {
    const auto* p = static_cast<const unsigned char*>(key);
    uint64_t h = kHashSeed ^ (static_cast<uint64_t>(len) * kHashMul);
    size_t i = 0;
    for (; i + 8 <= len; i += 8) {
        uint64_t k;
        std::memcpy(&k, p + i, 8);
        h = wideMulMix(h ^ k, kHashMul);
    }
    if (i < len) {
        uint64_t tail = 0;
        std::memcpy(&tail, p + i, len - i);
        h = wideMulMix(h ^ tail, kHashSeed);
    }
    return avalanche(h);
}

// 常规路径零分配；仅当启用忽略选项时才构造规范化临时串。
uint64_t computeLineHash(std::string_view line, const DiffOptions& options) {
    if (!options.ignoreWhitespace && !options.ignoreCase) {
        return fastHash(line.data(), line.size());
    }
    std::string normalized;
    normalized.reserve(line.size());
    for (unsigned char c : line) {
        if (options.ignoreWhitespace && isWhitespace(c)) continue;
        if (options.ignoreCase && c >= 'A' && c <= 'Z') c = static_cast<unsigned char>(c + ('a' - 'A'));
        normalized.push_back(static_cast<char>(c));
    }
    return fastHash(normalized.data(), normalized.size());
}

bool linesEqual(std::string_view a, std::string_view b, const DiffOptions& options) {
    if (!options.ignoreWhitespace && !options.ignoreCase) {
        return a == b;
    }

    size_t ia = 0;
    size_t ib = 0;
    while (ia < a.size() || ib < b.size()) {
        if (options.ignoreWhitespace) {
            while (ia < a.size() && isWhitespace(static_cast<unsigned char>(a[ia]))) ++ia;
            while (ib < b.size() && isWhitespace(static_cast<unsigned char>(b[ib]))) ++ib;
        }
        if (ia >= a.size() && ib >= b.size()) return true;
        if (ia >= a.size() || ib >= b.size()) return false;

        unsigned char ca = static_cast<unsigned char>(a[ia]);
        unsigned char cb = static_cast<unsigned char>(b[ib]);
        if (options.ignoreCase) {
            if (ca >= 'A' && ca <= 'Z') ca = static_cast<unsigned char>(ca + ('a' - 'A'));
            if (cb >= 'A' && cb <= 'Z') cb = static_cast<unsigned char>(cb + ('a' - 'A'));
        }
        if (ca != cb) return false;
        ++ia;
        ++ib;
    }
    return true;
}

struct Edit {
    char op; // '=', '-', '+'
    int a;   // token index in A (or -1)
    int b;   // token index in B (or -1)
};

struct Anchor {
    int a;
    int b;
};

// 找出区间内「两侧都唯一」的公共行作为锚点，再用 LIS 保证锚点单调。
// 使用排序数组而非哈希表：O(N log N)、连续内存、无逐节点堆分配。
std::vector<Anchor> findAnchors(const std::vector<LineToken>& A, int a0, int a1,
                                const std::vector<LineToken>& B, int b0, int b1,
                                const DiffOptions& options) {
    std::vector<Anchor> anchors;
    const int nA = a1 - a0;
    const int nB = b1 - b0;
    if (nA < 2 || nB < 2) return anchors;

    std::vector<std::pair<uint64_t, int>> ha;
    ha.reserve(static_cast<size_t>(nA));
    for (int i = a0; i < a1; ++i) ha.emplace_back(A[i].hash, i);
    std::sort(ha.begin(), ha.end());

    std::vector<std::pair<uint64_t, int>> hb;
    hb.reserve(static_cast<size_t>(nB));
    for (int i = b0; i < b1; ++i) hb.emplace_back(B[i].hash, i);
    std::sort(hb.begin(), hb.end());

    std::vector<Anchor> candidates;
    size_t ia = 0;
    size_t ib = 0;
    while (ia < ha.size() && ib < hb.size()) {
        if (ha[ia].first < hb[ib].first) {
            ++ia;
        } else if (ha[ia].first > hb[ib].first) {
            ++ib;
        } else {
            const uint64_t hash = ha[ia].first;
            size_t ja = ia;
            while (ja < ha.size() && ha[ja].first == hash) ++ja;
            size_t jb = ib;
            while (jb < hb.size() && hb[jb].first == hash) ++jb;
            if (ja - ia == 1 && jb - ib == 1) {
                const int ai = ha[ia].second;
                const int bi = hb[ib].second;
                // 64 位哈希仍做一次真实比较，杜绝碰撞导致的错误锚点。
                if (linesEqual(A[ai].rawText, B[bi].rawText, options)) {
                    candidates.push_back(Anchor{ai, bi});
                }
            }
            ia = ja;
            ib = jb;
        }
    }
    if (candidates.empty()) return anchors;

    std::sort(candidates.begin(), candidates.end(),
              [](const Anchor& x, const Anchor& y) { return x.a < y.a; });

    // 对 b 序列求 LIS（严格递增）。
    std::vector<int> tailsValue;
    std::vector<int> tailsIndex;
    std::vector<int> parent(candidates.size(), -1);
    tailsValue.reserve(candidates.size());
    tailsIndex.reserve(candidates.size());

    for (size_t i = 0; i < candidates.size(); ++i) {
        const int v = candidates[i].b;
        const auto it = std::lower_bound(tailsValue.begin(), tailsValue.end(), v);
        const size_t pos = static_cast<size_t>(it - tailsValue.begin());
        if (it == tailsValue.end()) {
            tailsValue.push_back(v);
            tailsIndex.push_back(static_cast<int>(i));
        } else {
            *it = v;
            tailsIndex[pos] = static_cast<int>(i);
        }
        if (pos > 0) parent[i] = tailsIndex[pos - 1];
    }

    int cur = tailsIndex.empty() ? -1 : tailsIndex.back();
    while (cur >= 0) {
        anchors.push_back(candidates[static_cast<size_t>(cur)]);
        cur = parent[static_cast<size_t>(cur)];
    }
    std::reverse(anchors.begin(), anchors.end());
    return anchors;
}

// 有界 Myers：成功后把编辑脚本追加到 out；超过上限返回 false（不写 out）。
bool myersBounded(const std::vector<LineToken>& A, int a0, int a1,
                  const std::vector<LineToken>& B, int b0, int b1,
                  const DiffOptions& options, std::vector<Edit>& out) {
    const int n = a1 - a0;
    const int m = b1 - b0;
    const int maxD = std::min(n + m, kMaxMyersD);
    const int vOffset = maxD;

    std::vector<int> v(static_cast<size_t>(2 * maxD + 3), 0);
    std::vector<std::vector<int>> trace;
    trace.reserve(static_cast<size_t>(maxD + 1));

    bool reached = false;
    int finalD = 0;
    for (int d = 0; d <= maxD; ++d) {
        trace.push_back(v);
        for (int k = -d; k <= d; k += 2) {
            int x;
            if (k == -d || (k != d && v[k - 1 + vOffset] < v[k + 1 + vOffset])) {
                x = v[k + 1 + vOffset];
            } else {
                x = v[k - 1 + vOffset] + 1;
            }
            int y = x - k;
            while (x < n && y < m &&
                   A[a0 + x].hash == B[b0 + y].hash &&
                   linesEqual(A[a0 + x].rawText, B[b0 + y].rawText, options)) {
                ++x;
                ++y;
            }
            v[k + vOffset] = x;
            if (x >= n && y >= m) {
                reached = true;
                finalD = d;
                break;
            }
        }
        if (reached) break;
    }
    if (!reached) return false;

    std::vector<Edit> reversed;
    int cx = n;
    int cy = m;
    for (int d = finalD; d > 0; --d) {
        const auto& prevV = trace[static_cast<size_t>(d)];
        const int k = cx - cy;
        int prevK;
        if (k == -d || (k != d && prevV[k - 1 + vOffset] < prevV[k + 1 + vOffset])) {
            prevK = k + 1;
        } else {
            prevK = k - 1;
        }
        const int prevX = prevV[prevK + vOffset];
        const int prevY = prevX - prevK;

        while (cx > prevX && cy > prevY) {
            reversed.push_back(Edit{'=', a0 + cx - 1, b0 + cy - 1});
            --cx;
            --cy;
        }
        if (cx > prevX) {
            reversed.push_back(Edit{'-', a0 + cx - 1, -1});
            --cx;
        } else if (cy > prevY) {
            reversed.push_back(Edit{'+', -1, b0 + cy - 1});
            --cy;
        }
    }
    while (cx > 0 && cy > 0) {
        reversed.push_back(Edit{'=', a0 + cx - 1, b0 + cy - 1});
        --cx;
        --cy;
    }

    std::reverse(reversed.begin(), reversed.end());
    out.insert(out.end(), reversed.begin(), reversed.end());
    return true;
}

void diffRange(const std::vector<LineToken>& A, int a0, int a1,
               const std::vector<LineToken>& B, int b0, int b1,
               const DiffOptions& options, std::vector<Edit>& out) {
    // 公共前缀
    while (a0 < a1 && b0 < b1 &&
           A[a0].hash == B[b0].hash &&
           linesEqual(A[a0].rawText, B[b0].rawText, options)) {
        out.push_back(Edit{'=', a0, b0});
        ++a0;
        ++b0;
    }

    // 公共后缀（先不输出，等中间部分处理完再补）
    int suffix = 0;
    while (a0 < a1 && b0 < b1 &&
           A[a1 - 1].hash == B[b1 - 1].hash &&
           linesEqual(A[a1 - 1].rawText, B[b1 - 1].rawText, options)) {
        --a1;
        --b1;
        ++suffix;
    }

    if (a0 == a1) {
        for (int y = b0; y < b1; ++y) out.push_back(Edit{'+', -1, y});
    } else if (b0 == b1) {
        for (int x = a0; x < a1; ++x) out.push_back(Edit{'-', x, -1});
    } else {
        const std::vector<Anchor> anchors = findAnchors(A, a0, a1, B, b0, b1, options);
        if (!anchors.empty()) {
            int pa = a0;
            int pb = b0;
            for (const Anchor& anchor : anchors) {
                diffRange(A, pa, anchor.a, B, pb, anchor.b, options, out);
                out.push_back(Edit{'=', anchor.a, anchor.b});
                pa = anchor.a + 1;
                pb = anchor.b + 1;
            }
            diffRange(A, pa, a1, B, pb, b1, options, out);
        } else {
            const long long cells = static_cast<long long>(a1 - a0) * static_cast<long long>(b1 - b0);
            bool done = false;
            if (cells <= kMaxMyersCells) {
                done = myersBounded(A, a0, a1, B, b0, b1, options, out);
            }
            if (!done) {
                // 无锚点且规模过大：整块替换（O(N) 且内存有界）。
                // 后续配对阶段会按位置两两合并为 Modified 并计算行内差异。
                for (int x = a0; x < a1; ++x) out.push_back(Edit{'-', x, -1});
                for (int y = b0; y < b1; ++y) out.push_back(Edit{'+', -1, y});
            }
        }
    }

    for (int i = 0; i < suffix; ++i) {
        out.push_back(Edit{'=', a1 + i, b1 + i});
    }
}

inline bool charEqual(char a, char b, bool ignoreCase) {
    unsigned char ca = static_cast<unsigned char>(a);
    unsigned char cb = static_cast<unsigned char>(b);
    if (ignoreCase) {
        if (ca >= 'A' && ca <= 'Z') ca = static_cast<unsigned char>(ca + ('a' - 'A'));
        if (cb >= 'A' && cb <= 'Z') cb = static_cast<unsigned char>(cb + ('a' - 'A'));
    }
    return ca == cb;
}

// 行内字符级有界 Myers：成功时把多段差异区间写入 ra/rb（相对传入子串）。
bool charMyers(std::string_view a, std::string_view b, bool ignoreCase,
               std::vector<CharRange>& ra, std::vector<CharRange>& rb) {
    const int n = static_cast<int>(a.size());
    const int m = static_cast<int>(b.size());
    const int maxD = std::min(n + m, kMaxCharD);
    const int vOffset = maxD;

    std::vector<int> v(static_cast<size_t>(2 * maxD + 3), 0);
    std::vector<std::vector<int>> trace;
    trace.reserve(static_cast<size_t>(maxD + 1));

    bool reached = false;
    int finalD = 0;
    for (int d = 0; d <= maxD; ++d) {
        trace.push_back(v);
        for (int k = -d; k <= d; k += 2) {
            int x;
            if (k == -d || (k != d && v[k - 1 + vOffset] < v[k + 1 + vOffset])) {
                x = v[k + 1 + vOffset];
            } else {
                x = v[k - 1 + vOffset] + 1;
            }
            int y = x - k;
            while (x < n && y < m && charEqual(a[static_cast<size_t>(x)], b[static_cast<size_t>(y)], ignoreCase)) {
                ++x;
                ++y;
            }
            v[k + vOffset] = x;
            if (x >= n && y >= m) {
                reached = true;
                finalD = d;
                break;
            }
        }
        if (reached) break;
    }
    if (!reached) return false;

    struct CharEdit {
        char op;
        int x;
        int y;
    };
    std::vector<CharEdit> reversed;
    int cx = n;
    int cy = m;
    for (int d = finalD; d > 0; --d) {
        const auto& prevV = trace[static_cast<size_t>(d)];
        const int k = cx - cy;
        int prevK;
        if (k == -d || (k != d && prevV[k - 1 + vOffset] < prevV[k + 1 + vOffset])) {
            prevK = k + 1;
        } else {
            prevK = k - 1;
        }
        const int prevX = prevV[prevK + vOffset];
        const int prevY = prevX - prevK;
        while (cx > prevX && cy > prevY) {
            reversed.push_back(CharEdit{'=', cx - 1, cy - 1});
            --cx;
            --cy;
        }
        if (cx > prevX) {
            reversed.push_back(CharEdit{'-', cx - 1, -1});
            --cx;
        } else if (cy > prevY) {
            reversed.push_back(CharEdit{'+', -1, cy - 1});
            --cy;
        }
    }
    while (cx > 0 && cy > 0) {
        reversed.push_back(CharEdit{'=', cx - 1, cy - 1});
        --cx;
        --cy;
    }
    std::reverse(reversed.begin(), reversed.end());

    size_t i = 0;
    while (i < reversed.size()) {
        if (reversed[i].op == '-') {
            const int start = reversed[i].x;
            int end = start;
            while (i < reversed.size() && reversed[i].op == '-') {
                end = reversed[i].x + 1;
                ++i;
            }
            ra.push_back(CharRange{start, end - start});
        } else if (reversed[i].op == '+') {
            const int start = reversed[i].y;
            int end = start;
            while (i < reversed.size() && reversed[i].op == '+') {
                end = reversed[i].y + 1;
                ++i;
            }
            rb.push_back(CharRange{start, end - start});
        } else {
            ++i;
        }
    }
    return true;
}

} // namespace

uint64_t DiffEngine::hashLine(std::string_view line, const DiffOptions& options) {
    return computeLineHash(line, options);
}

// P.7：把「内容相同、仅位置变化」的 Removed/Added 行配对为 Moved。
// 用已规范化的行哈希排序分组（连续数组，无哈希节点分配），配对前再做一次真实内容比较防碰撞。
void detectMovedLines(const std::vector<LineToken>& tokensA, const std::vector<LineToken>& tokensB,
                      const DiffOptions& options, DiffResult& result) {
    struct Candidate {
        uint64_t hash;
        size_t index;
        bool isA;
    };
    std::vector<Candidate> candidates;
    candidates.reserve(static_cast<size_t>(result.addedCount + result.removedCount));
    for (size_t i = 0; i < result.lines.size(); ++i) {
        const LineDiff& line = result.lines[i];
        if (line.type == ChangeType::Removed && line.lineA >= 0) {
            candidates.push_back(Candidate{tokensA[static_cast<size_t>(line.lineA)].hash, i, true});
        } else if (line.type == ChangeType::Added && line.lineB >= 0) {
            candidates.push_back(Candidate{tokensB[static_cast<size_t>(line.lineB)].hash, i, false});
        }
    }
    if (candidates.size() < 2) return;

    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& x, const Candidate& y) { return x.hash < y.hash; });

    int moved = 0;
    size_t i = 0;
    while (i < candidates.size()) {
        size_t j = i;
        while (j < candidates.size() && candidates[j].hash == candidates[i].hash) ++j;
        std::vector<size_t> fromA;
        std::vector<size_t> fromB;
        for (size_t k = i; k < j; ++k) {
            (candidates[k].isA ? fromA : fromB).push_back(candidates[k].index);
        }
        const size_t pairs = std::min(fromA.size(), fromB.size());
        for (size_t p = 0; p < pairs; ++p) {
            LineDiff& left = result.lines[fromA[p]];
            LineDiff& right = result.lines[fromB[p]];
            if (!linesEqual(tokensA[static_cast<size_t>(left.lineA)].rawText,
                            tokensB[static_cast<size_t>(right.lineB)].rawText, options)) {
                continue;
            }
            left.type = ChangeType::Moved;
            left.lineB = right.lineB;
            left.movedToLine = right.lineB;
            right.consumed = true; // 对侧项不再产生对齐行/绘制
            ++moved;
        }
        i = j;
    }

    result.movedCount += moved;
    result.addedCount -= moved;
    result.removedCount -= moved;
}

std::vector<LineToken> DiffEngine::tokenize(std::string_view text, const DiffOptions& options) {
    std::vector<LineToken> tokens;
    // 与 Scintilla 行数语义保持一致：行数 = '\n' 个数 + 1（末尾换行后还有一空行）。
    tokens.reserve(text.size() / 40 + 8);

    size_t start = 0;
    int lineIdx = 0;
    for (;;) {
        const size_t nl = text.find('\n', start);
        const size_t contentEnd = (nl == std::string_view::npos) ? text.size() : nl;
        size_t lineEnd = contentEnd;
        if (options.ignoreEol && lineEnd > start && text[lineEnd - 1] == '\r') {
            --lineEnd;
        }
        const std::string_view raw = text.substr(start, lineEnd - start);
        tokens.push_back(LineToken{computeLineHash(raw, options), raw, lineIdx++});

        if (nl == std::string_view::npos) break;
        start = nl + 1;
        if (start == text.size()) {
            // 末尾换行后的最后一空行
            const std::string_view empty = text.substr(start, 0);
            tokens.push_back(LineToken{computeLineHash(empty, options), empty, lineIdx++});
            break;
        }
    }
    return tokens;
}

void DiffEngine::computeIntraLineCharDiff(std::string_view lineA, std::string_view lineB,
                                          std::vector<CharRange>& intraA,
                                          std::vector<CharRange>& intraB,
                                          const DiffOptions& options) {
    intraA.clear();
    intraB.clear();

    const int lenA = static_cast<int>(lineA.size());
    const int lenB = static_cast<int>(lineB.size());
    if (lenA == 0 && lenB == 0) return;

    int prefix = 0;
    while (prefix < lenA && prefix < lenB &&
           charEqual(lineA[static_cast<size_t>(prefix)], lineB[static_cast<size_t>(prefix)],
                     options.ignoreCase)) {
        ++prefix;
    }
    int suffix = 0;
    while ((lenA - 1 - suffix) >= prefix && (lenB - 1 - suffix) >= prefix &&
           charEqual(lineA[static_cast<size_t>(lenA - 1 - suffix)],
                     lineB[static_cast<size_t>(lenB - 1 - suffix)], options.ignoreCase)) {
        ++suffix;
    }

    const int midA = lenA - prefix - suffix;
    const int midB = lenB - prefix - suffix;
    if (midA <= 0 && midB <= 0) return;
    if (midA <= 0) {
        intraB.push_back(CharRange{prefix, midB});
        return;
    }
    if (midB <= 0) {
        intraA.push_back(CharRange{prefix, midA});
        return;
    }

    const std::string_view midViewA = lineA.substr(static_cast<size_t>(prefix), static_cast<size_t>(midA));
    const std::string_view midViewB = lineB.substr(static_cast<size_t>(prefix), static_cast<size_t>(midB));
    if (!charMyers(midViewA, midViewB, options.ignoreCase, intraA, intraB)) {
        // 行内编辑距离过大（极长行）：退化为单段。
        intraA.assign(1, CharRange{prefix, midA});
        intraB.assign(1, CharRange{prefix, midB});
        return;
    }
    for (CharRange& r : intraA) r.start += prefix;
    for (CharRange& r : intraB) r.start += prefix;
}

DiffResult DiffEngine::computeDiff(std::string_view textA, std::string_view textB, const DiffOptions& options) {
    DiffResult result;

    const std::vector<LineToken> tokensA = tokenize(textA, options);
    const std::vector<LineToken> tokensB = tokenize(textB, options);

    std::vector<Edit> edits;
    edits.reserve(tokensA.size() + tokensB.size());
    diffRange(tokensA, 0, static_cast<int>(tokensA.size()),
              tokensB, 0, static_cast<int>(tokensB.size()), options, edits);

    result.lines.reserve(edits.size());

    size_t i = 0;
    while (i < edits.size()) {
        const Edit& edit = edits[i];
        if (edit.op == '=') {
            result.lines.push_back(LineDiff{
                ChangeType::Unchanged, tokensA[static_cast<size_t>(edit.a)].lineIndex,
                tokensB[static_cast<size_t>(edit.b)].lineIndex, {}, {}});
            ++i;
            continue;
        }

        if (edit.op == '-') {
            size_t removedEnd = i;
            while (removedEnd < edits.size() && edits[removedEnd].op == '-') ++removedEnd;
            size_t addedEnd = removedEnd;
            while (addedEnd < edits.size() && edits[addedEnd].op == '+') ++addedEnd;

            const int removed = static_cast<int>(removedEnd - i);
            const int added = static_cast<int>(addedEnd - removedEnd);
            const int pairs = std::min(removed, added);

            for (int p = 0; p < pairs; ++p) {
                const Edit& re = edits[i + static_cast<size_t>(p)];
                const Edit& ae = edits[removedEnd + static_cast<size_t>(p)];
                LineDiff mod{
                    ChangeType::Modified,
                    tokensA[static_cast<size_t>(re.a)].lineIndex,
                    tokensB[static_cast<size_t>(ae.b)].lineIndex,
                    {}, {}};
                if (pairs <= kMaxIntraLinePairs) {
                    computeIntraLineCharDiff(tokensA[static_cast<size_t>(re.a)].rawText,
                                             tokensB[static_cast<size_t>(ae.b)].rawText,
                                             mod.intraA, mod.intraB, options);
                }
                result.lines.push_back(std::move(mod));
                ++result.modifiedCount;
            }
            for (int p = pairs; p < removed; ++p) {
                const Edit& re = edits[i + static_cast<size_t>(p)];
                result.lines.push_back(LineDiff{
                    ChangeType::Removed, tokensA[static_cast<size_t>(re.a)].lineIndex, -1, {}, {}});
                ++result.removedCount;
            }
            for (int p = pairs; p < added; ++p) {
                const Edit& ae = edits[removedEnd + static_cast<size_t>(p)];
                result.lines.push_back(LineDiff{
                    ChangeType::Added, -1, tokensB[static_cast<size_t>(ae.b)].lineIndex, {}, {}});
                ++result.addedCount;
            }
            i = addedEnd;
            continue;
        }

        // '+' run（前面没有 '-'）
        size_t addedEnd = i;
        while (addedEnd < edits.size() && edits[addedEnd].op == '+') ++addedEnd;
        for (size_t p = i; p < addedEnd; ++p) {
            result.lines.push_back(LineDiff{
                ChangeType::Added, -1, tokensB[static_cast<size_t>(edits[p].b)].lineIndex, {}, {}});
            ++result.addedCount;
        }
        i = addedEnd;
    }

    if (options.detectMoves) {
        detectMovedLines(tokensA, tokensB, options, result);
    }
    return result;
}

AlignmentList DiffEngine::computeAlignment(const DiffResult& diff, int totalLinesA, int totalLinesB) {
    AlignmentList align;
    align.reserve(diff.lines.size() + 16);

    int coveredA = 0;
    int coveredB = 0;
    for (const auto& item : diff.lines) {
        if (item.consumed) continue;
        switch (item.type) {
        case ChangeType::Unchanged:
        case ChangeType::Modified:
        case ChangeType::Moved:
            align.push_back(AlignmentItem{item.lineA, item.lineB, item.type});
            if (item.lineA >= 0) coveredA = std::max(coveredA, item.lineA + 1);
            if (item.lineB >= 0) coveredB = std::max(coveredB, item.lineB + 1);
            break;
        case ChangeType::Removed:
            align.push_back(AlignmentItem{item.lineA, -1, item.type});
            if (item.lineA >= 0) coveredA = std::max(coveredA, item.lineA + 1);
            break;
        case ChangeType::Added:
            align.push_back(AlignmentItem{-1, item.lineB, item.type});
            if (item.lineB >= 0) coveredB = std::max(coveredB, item.lineB + 1);
            break;
        }
    }

    // 兜底：若调用方给的行数比 diff 覆盖的更多（例如空文档/异常输入），补成未变更行。
    while (coveredA < totalLinesA || coveredB < totalLinesB) {
        const int lineA = coveredA < totalLinesA ? coveredA : -1;
        const int lineB = coveredB < totalLinesB ? coveredB : -1;
        if (lineA < 0 && lineB < 0) break;
        align.push_back(AlignmentItem{lineA, lineB, ChangeType::Unchanged});
        if (lineA >= 0) ++coveredA;
        if (lineB >= 0) ++coveredB;
    }

    return align;
}

} // namespace Diff
