#include "test_encoding.h"
#include "utils/EncodingConverter.h"

#include <QtTest>
#include <QByteArray>
#include <string>

namespace {

std::string convert(const std::string& input, std::string_view enc, bool encode, bool* ok) {
    std::string out;
    const bool result = encode
        ? Encoding::encodeFromUtf8Chunks(input.data(), input.size(), enc,
              [&out](const char* d, std::size_t n) { out.append(d, n); })
        : Encoding::decodeToUtf8Chunks(input.data(), input.size(), enc,
              [&out](const char* d, std::size_t n) { out.append(d, n); });
    if (ok) *ok = result;
    return out;
}

QByteArray bytes(const std::string& s) {
    return QByteArray(s.data(), static_cast<qsizetype>(s.size()));
}

} // namespace

void TestEncoding::utf8Passthrough() {
    const std::string src = "中文 abc\n第二行";
    bool ok = false;
    const std::string encoded = convert(src, "UTF-8", true, &ok);
    QVERIFY(ok);
    QCOMPARE(bytes(encoded), bytes(src));

    const std::string decoded = convert(encoded, "UTF-8 BOM", false, &ok);
    QVERIFY(ok);
    QCOMPARE(bytes(decoded), bytes(src));
}

void TestEncoding::gbkEncodeDecodeRoundTrip() {
    // "中文" 的 GBK 字节是 D6 D0 CE C4，同时锁住别名映射与分块 sink 拼接。
    const std::string src = "中文";
    bool ok = false;
    const std::string gbk = convert(src, "GBK", true, &ok);
    QVERIFY(ok);
    QCOMPARE(bytes(gbk), QByteArray("\xD6\xD0\xCE\xC4", 4));

    const std::string back = convert(gbk, "GBK", false, &ok);
    QVERIFY(ok);
    QCOMPARE(bytes(back), bytes(src));
}

void TestEncoding::bomRules() {
    QCOMPARE(bytes(Encoding::bomForSave("UTF-8 BOM")), QByteArray("\xEF\xBB\xBF", 3));
    QCOMPARE(bytes(Encoding::bomForSave("UTF-8")), QByteArray());
    QCOMPARE(bytes(Encoding::bomForSave("UTF-16 LE BOM")), QByteArray("\xFF\xFE", 2));
    QCOMPARE(bytes(Encoding::bomForSave("UTF-16 BE BOM")), QByteArray("\xFE\xFF", 2));
    QCOMPARE(bytes(Encoding::bomForSave("GBK")), QByteArray());

    // 拆成相邻字面量，避免 \xBFabc 把 abc 一起吞进十六进制转义。
    const char withBom[] = "\xEF\xBB\xBF" "abc";
    QCOMPARE(Encoding::bomLength(withBom, sizeof(withBom) - 1), std::size_t(3));
    const char plain[] = "abc";
    QCOMPARE(Encoding::bomLength(plain, sizeof(plain) - 1), std::size_t(0));
}

void TestEncoding::unsupportedEncodingEncodeDoesNotLeakBytes() {
    const std::string src = "中文 ABC";
    bool ok = true;
    const std::string out = convert(src, "NO-SUCH-CHARSET-NPP", true, &ok);
    QVERIFY2(!ok, "iconv_open 失败必须返回 false");
    QCOMPARE(bytes(out), QByteArray());
}

void TestEncoding::unsupportedEncodingDecodeDoesNotLeakBytes() {
    const std::string src = "\xD6\xD0\xCE\xC4";
    bool ok = true;
    const std::string out = convert(src, "NO-SUCH-CHARSET-NPP", false, &ok);
    QVERIFY2(!ok, "iconv_open 失败必须返回 false");
    QCOMPARE(bytes(out), QByteArray());
}

void TestEncoding::utf16Detection() {
    QVERIFY(Encoding::isUtf16("UTF-16 LE"));
    QVERIFY(Encoding::isUtf16("UTF-16 BE BOM"));
    QVERIFY(!Encoding::isUtf16("UTF-8"));

    const char utf16le[] = "a\0b\0";
    QVERIFY(!Encoding::looksBinary(utf16le, sizeof(utf16le) - 1, "UTF-16 LE"));
    QVERIFY(Encoding::looksBinary(utf16le, sizeof(utf16le) - 1, "UTF-8"));
}
