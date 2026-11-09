#pragma once

#include <QObject>

class TestEncoding : public QObject {
    Q_OBJECT
private slots:
    // 先锁住当前正确行为，作为后续改动的回归基线。
    void utf8Passthrough();
    void gbkEncodeDecodeRoundTrip();
    void bomRules();
    void utf16Detection();
    // iconv 打不开编码时必须硬失败，绝不能把内部 UTF-8 冒充目标编码写出去。
    void unsupportedEncodingEncodeDoesNotLeakBytes();
    void unsupportedEncodingDecodeDoesNotLeakBytes();
};
