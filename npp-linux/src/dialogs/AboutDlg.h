#pragma once

#include <QDialog>
#include <QString>

class AboutDlg : public QDialog {
    Q_OBJECT
public:
    explicit AboutDlg(QWidget* parent = nullptr);

    QString versionString() const { return _versionStr; }
    QString buildTime() const { return _buildTimeStr; }
    // 用户在“关于”里点了“检查更新”：由调用方在关闭后接着打开更新窗口。
    bool updatesRequested() const { return _updatesRequested; }

private:
    void setupUi();

    QString _versionStr;
    QString _buildTimeStr;
    bool _updatesRequested{false};
};
