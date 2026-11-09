#pragma once

#include <QDialog>
#include <QString>

class AboutDlg : public QDialog {
    Q_OBJECT
public:
    explicit AboutDlg(QWidget* parent = nullptr);

    QString versionString() const { return _versionStr; }
    QString buildTime() const { return _buildTimeStr; }

private:
    void setupUi();

    QString _versionStr;
    QString _buildTimeStr;
};
