#include "test_url_link.h"

#include <QtTest>
#include <QDesktopServices>
#include <QUrl>

#include "EditorView.h"

namespace {

// 在 ASCII 文本里字符索引即字节位置（Scintilla 内部 UTF-8），可直接用作 Scintilla 位置。
sptr_t posOf(const QString& text, const QString& needle) {
    const int idx = text.indexOf(needle);
    return idx < 0 ? -1 : static_cast<sptr_t>(idx);
}

} // namespace

void TestUrlLink::underlinesVisibleUrl() {
    EditorView ed;
    const QString text = QStringLiteral("see https://example.com/path now");
    ed.setTextFromUtf8(text.toUtf8().constData());
    ed.refreshUrlHighlighting();

    const sptr_t pos = posOf(text, QStringLiteral("https://example.com/path"));
    QVERIFY2(pos >= 0, "测试文本应包含目标 URL");
    QCOMPARE(ed.urlTextAt(pos), QStringLiteral("https://example.com/path"));
}

void TestUrlLink::plainTextHasNoLink() {
    EditorView ed;
    const QString text = QStringLiteral("hello world, just text");
    ed.setTextFromUtf8(text.toUtf8().constData());
    ed.refreshUrlHighlighting();

    const sptr_t pos = posOf(text, QStringLiteral("world"));
    QCOMPARE(ed.urlTextAt(pos), QString());
}

void TestUrlLink::mailtoDetected() {
    EditorView ed;
    const QString text = QStringLiteral("contact mailto:foo@bar.com please");
    ed.setTextFromUtf8(text.toUtf8().constData());
    ed.refreshUrlHighlighting();

    const sptr_t pos = posOf(text, QStringLiteral("mailto:foo@bar.com"));
    QCOMPARE(ed.urlTextAt(pos), QStringLiteral("mailto:foo@bar.com"));
}

void TestUrlLink::disabledWhenPrefOff() {
    EditorView ed;
    const QString text = QStringLiteral("see https://example.com/path now");
    ed.setTextFromUtf8(text.toUtf8().constData());
    ed.setUrlClickable(false);
    ed.refreshUrlHighlighting();

    const sptr_t pos = posOf(text, QStringLiteral("https://example.com/path"));
    QCOMPARE(ed.urlTextAt(pos), QString());
}

void TestUrlLink::trailingPunctuationStripped() {
    EditorView ed;
    const QString text = QStringLiteral("go to https://example.com. end");
    ed.setTextFromUtf8(text.toUtf8().constData());
    ed.refreshUrlHighlighting();

    const sptr_t pos = posOf(text, QStringLiteral("https://example.com"));
    QCOMPARE(ed.urlTextAt(pos), QStringLiteral("https://example.com"));
}

void TestUrlLink::ctrlClickOpensUrl() {
    UrlCapture capture;
    QDesktopServices::setUrlHandler("http", &capture, "capture");
    QDesktopServices::setUrlHandler("https", &capture, "capture");

    EditorView ed;
    const QString text = QStringLiteral("visit https://example.com/path here");
    ed.setTextFromUtf8(text.toUtf8().constData());
    ed.refreshUrlHighlighting();

    const sptr_t urlPos = posOf(text, QStringLiteral("https://example.com/path"));
    QVERIFY2(ed.openUrlAt(urlPos), "命中 URL 时 openUrlAt 必须返回 true");
    QCOMPARE(capture.count, 1);
    QCOMPARE(capture.last.toString(), QStringLiteral("https://example.com/path"));

    // 非 URL 处点击不应触发打开。
    const sptr_t textPos = posOf(text, QStringLiteral("visit"));
    QVERIFY2(!ed.openUrlAt(textPos), "非 URL 位置 openUrlAt 必须返回 false");
    QCOMPARE(capture.count, 1);

    QDesktopServices::unsetUrlHandler("http");
    QDesktopServices::unsetUrlHandler("https");
}
