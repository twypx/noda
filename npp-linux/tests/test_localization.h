#pragma once

#include <QObject>

// 英文模式下界面不应残留中文（语言选择等“原生名称”除外）。
class TestLocalization : public QObject {
    Q_OBJECT
private slots:
    void englishModeHasNoChineseInMainWindow();
    void englishModeHasNoChineseInPreferences();
};
