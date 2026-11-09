#pragma once

#include <QDialog>
#include <QListWidget>
#include <QFontComboBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QComboBox>
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
    void onStyleSlotChanged(int index);
    void onApply();

private:
    void setupUI();
    void updateColorButtons();
    QString currentLangId() const;

    QListWidget* _langList;
    QComboBox* _styleCombo;             // 该语言的全部 WordsStyle 槽位，data = styleID
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
