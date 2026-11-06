#pragma once

#include <QDialog>
#include <QLineEdit>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>

class EditorView;

class FindReplaceDialog : public QDialog {
    Q_OBJECT
public:
    explicit FindReplaceDialog(QWidget* parent = nullptr);

    void setTargetEditor(EditorView* editor) { _targetEditor = editor; }
    void setFindText(const QString& text);
    void switchToReplaceMode(bool isReplace);

public slots:
    void onFindNext();
    void onFindPrev();
    void onReplace();
    void onReplaceAll();

private:
    void setupUI();

    EditorView* _targetEditor{nullptr};

    QLineEdit* _findInput;
    QLineEdit* _replaceInput;
    QLabel* _replaceLabel;
    QPushButton* _btnReplace;
    QPushButton* _btnReplaceAll;
    QCheckBox* _caseSensitiveCheck;
    QCheckBox* _wholeWordCheck;
    QCheckBox* _regexCheck;
    QLabel* _statusLabel;
};
