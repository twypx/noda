#pragma once

#include <QDialog>
#include <QListWidget>
#include <QFontComboBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QColor>
#include <QString>

class StyleConfigDialog : public QDialog {
    Q_OBJECT
public:
    explicit StyleConfigDialog(QWidget* parent = nullptr);

signals:
    void styleUpdated();

private slots:
    void onLanguageSelected(int row);
    void onApply();

private:
    void setupUI();
    QString currentLangId() const;      // 当前选中行的语言 id（"global" 表示全局默认样式）

    QListWidget* _langList;
    QFontComboBox* _fontCombo;
    QSpinBox* _fontSizeSpin;
    QCheckBox* _boldCheck;
    QCheckBox* _italicCheck;
    QPushButton* _btnFgColor;
    QPushButton* _btnBgColor;
    QLabel* _hintLabel;

    QColor _fgColor;
    QColor _bgColor;
};
