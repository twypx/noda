#pragma once

#include <QObject>
#include <QUrl>

// 捕获 QDesktopServices::openUrl 实际要打开的 URL，避免测试里真的拉起浏览器。
class UrlCapture : public QObject {
    Q_OBJECT
public slots:
    void capture(const QUrl& url) { last = url; ++count; }
public:
    QUrl last;
    int count{0};
};

// URL 可点击链接（Clickable Link）：自动识别可见文本里的 URL 并加下划线，Ctrl+点击打开。
class TestUrlLink : public QObject {
    Q_OBJECT
private slots:
    void underlinesVisibleUrl();
    void plainTextHasNoLink();
    void mailtoDetected();
    void disabledWhenPrefOff();
    void trailingPunctuationStripped();
    void ctrlClickOpensUrl();
};
