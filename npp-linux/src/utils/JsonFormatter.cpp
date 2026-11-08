#include "JsonFormatter.h"

#include <cstring>
#include <vector>

namespace {

constexpr int kIndentSpaces = 4;
// 显式栈深度上限：纯嵌套 JSON 的输出缩进随深度呈 O(depth^2) 增长，不设限会让
// 一个几 KB 的 "[[[[..." 病态文件膨胀出 GB 级输出。真实 JSON 极少超过几十层
// （jsoncpp 自身 stackLimit 默认 256），1024 已远超实际，超限按“未改动”处理。
constexpr size_t kMaxDepth = 1024;

enum class Expect { Value, Key, Colon, CommaOrEnd, Done };

struct Frame {
    bool isObject;
};

inline bool isWs(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

inline bool isHex(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

class Formatter {
public:
    explicit Formatter(const std::string& in) : in_(in), n_(in.size()) {
        out_.reserve(n_ + n_ / 2 + 64);
    }

    bool run() {
        Expect expect = Expect::Value;
        size_t i = 0;
        bool atLineStart = false; // 下一个 key/元素前需换行+缩进（开括号或逗号后置位）

        while (true) {
            while (i < n_ && isWs(in_[i])) ++i;
            if (i >= n_) return expect == Expect::Done && stack_.empty();
            const char c = in_[i];

            switch (expect) {
            case Expect::Done:
                return false; // 根值之后仍有非空白内容

            case Expect::Value: {
                if (c == '{' || c == '[') {
                    if (atLineStart) { newlineIndent(); atLineStart = false; }
                    const bool isObj = (c == '{');
                    const char close = isObj ? '}' : ']';
                    size_t j = i + 1;
                    while (j < n_ && isWs(in_[j])) ++j;
                    if (j < n_ && in_[j] == close) {
                        // 空容器：保持 {} / [] 内联，不换行、不入栈。
                        out_ += c;
                        out_ += close;
                        i = j + 1;
                        expect = afterValue();
                    } else {
                        if (stack_.size() >= kMaxDepth) return false;
                        out_ += c;
                        stack_.push_back(Frame{isObj});
                        ++i;
                        atLineStart = true;
                        expect = isObj ? Expect::Key : Expect::Value;
                    }
                    continue;
                }
                if (atLineStart) { newlineIndent(); atLineStart = false; }
                bool ok = false;
                if (c == '"') ok = copyString(i);
                else if (c == '-' || (c >= '0' && c <= '9')) ok = copyNumber(i);
                else if (c == 't') ok = copyLiteral(i, "true");
                else if (c == 'f') ok = copyLiteral(i, "false");
                else if (c == 'n') ok = copyLiteral(i, "null");
                else return false;
                if (!ok) return false;
                expect = afterValue();
                continue;
            }

            case Expect::Key: {
                if (c != '"') return false; // 拒绝尾逗号/非法键
                if (atLineStart) { newlineIndent(); atLineStart = false; }
                if (!copyString(i)) return false;
                expect = Expect::Colon;
                continue;
            }

            case Expect::Colon: {
                if (c != ':') return false;
                out_ += ':';
                out_ += ' ';
                ++i;
                expect = Expect::Value;
                continue;
            }

            case Expect::CommaOrEnd: {
                if (c == ',') {
                    out_ += ',';
                    ++i;
                    atLineStart = true;
                    expect = stack_.back().isObject ? Expect::Key : Expect::Value;
                    continue;
                }
                const bool isObj = stack_.back().isObject;
                if ((isObj && c == '}') || (!isObj && c == ']')) {
                    const size_t depth = stack_.size() - 1;
                    out_ += '\n';
                    appendIndent(depth);
                    out_ += c;
                    ++i;
                    stack_.pop_back();
                    expect = afterValue();
                    continue;
                }
                return false;
            }
            }
            return false;
        }
    }

    std::string take() { return std::move(out_); }

private:
    Expect afterValue() const {
        return stack_.empty() ? Expect::Done : Expect::CommaOrEnd;
    }

    void appendIndent(size_t level) {
        out_.append(level * static_cast<size_t>(kIndentSpaces), ' ');
    }

    void newlineIndent() {
        out_ += '\n';
        appendIndent(stack_.size());
    }

    // 逐字节原样复制字符串字面量（含引号与转义），同时校验合法性。
    bool copyString(size_t& i) {
        const size_t start = i;
        ++i; // 开引号
        while (i < n_) {
            const unsigned char ch = static_cast<unsigned char>(in_[i]);
            if (ch == '"') {
                ++i;
                out_.append(in_, start, i - start);
                return true;
            }
            if (ch == '\\') {
                ++i;
                if (i >= n_) return false;
                const char e = in_[i];
                if (e == 'u') {
                    if (i + 4 >= n_) return false;
                    for (int k = 1; k <= 4; ++k)
                        if (!isHex(in_[i + k])) return false;
                    i += 5;
                } else if (e == '"' || e == '\\' || e == '/' || e == 'b' || e == 'f'
                           || e == 'n' || e == 'r' || e == 't') {
                    ++i;
                } else {
                    return false;
                }
            } else if (ch < 0x20) {
                return false; // 字符串内裸控制字符非法
            } else {
                ++i;
            }
        }
        return false; // 未闭合
    }

    // 按 JSON 数字文法扫描并原样复制（保留 1e3 / 2.50 等字面量，不做规范化）。
    bool copyNumber(size_t& i) {
        const size_t start = i;
        if (in_[i] == '-') ++i;
        if (i >= n_) return false;
        if (in_[i] == '0') {
            ++i;
        } else if (in_[i] >= '1' && in_[i] <= '9') {
            while (i < n_ && in_[i] >= '0' && in_[i] <= '9') ++i;
        } else {
            return false;
        }
        if (i < n_ && in_[i] == '.') {
            ++i;
            if (i >= n_ || in_[i] < '0' || in_[i] > '9') return false;
            while (i < n_ && in_[i] >= '0' && in_[i] <= '9') ++i;
        }
        if (i < n_ && (in_[i] == 'e' || in_[i] == 'E')) {
            ++i;
            if (i < n_ && (in_[i] == '+' || in_[i] == '-')) ++i;
            if (i >= n_ || in_[i] < '0' || in_[i] > '9') return false;
            while (i < n_ && in_[i] >= '0' && in_[i] <= '9') ++i;
        }
        out_.append(in_, start, i - start);
        return true;
    }

    bool copyLiteral(size_t& i, const char* lit) {
        const size_t len = std::strlen(lit);
        if (i + len > n_) return false;
        if (in_.compare(i, len, lit) != 0) return false;
        out_.append(in_, i, len);
        i += len;
        return true;
    }

    const std::string& in_;
    const size_t n_;
    std::string out_;
    std::vector<Frame> stack_;
};

} // namespace

namespace JsonFormatter {

std::string format(const std::string& utf8) {
    if (utf8.empty()) return std::string();
    Formatter f(utf8);
    if (!f.run()) return std::string(); // 非法 JSON：按“未改动”处理
    return f.take();
}

} // namespace JsonFormatter