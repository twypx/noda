#include "FindReplaceDialog.h"
#include "../EditorView.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>

FindReplaceDialog::FindReplaceDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle("查找与替换");
    setMinimumWidth(420);
    setupUI();
}

void FindReplaceDialog::setupUI() {
    auto* mainLayout = new QVBoxLayout(this);
    auto* gridLayout = new QGridLayout();

    gridLayout->addWidget(new QLabel("查找目标:"), 0, 0);
    _findInput = new QLineEdit(this);
    gridLayout->addWidget(_findInput, 0, 1);

    _replaceLabel = new QLabel("替换为:", this);
    gridLayout->addWidget(_replaceLabel, 1, 0);
    _replaceInput = new QLineEdit(this);
    gridLayout->addWidget(_replaceInput, 1, 1);

    mainLayout->addLayout(gridLayout);

    auto* checkLayout = new QHBoxLayout();
    _caseSensitiveCheck = new QCheckBox("区分大小写", this);
    _wholeWordCheck = new QCheckBox("全词匹配", this);
    _regexCheck = new QCheckBox("正则表达式", this);

    checkLayout->addWidget(_caseSensitiveCheck);
    checkLayout->addWidget(_wholeWordCheck);
    checkLayout->addWidget(_regexCheck);
    mainLayout->addLayout(checkLayout);

    auto* btnLayout = new QHBoxLayout();
    auto* btnFindNext = new QPushButton("查找下一个(&N)", this);
    auto* btnFindPrev = new QPushButton("查找上一个(&P)", this);
    _btnReplace = new QPushButton("替换(&R)", this);
    _btnReplaceAll = new QPushButton("全部替换(&A)", this);

    btnLayout->addWidget(btnFindNext);
    btnLayout->addWidget(btnFindPrev);
    btnLayout->addWidget(_btnReplace);
    btnLayout->addWidget(_btnReplaceAll);
    mainLayout->addLayout(btnLayout);

    _statusLabel = new QLabel(this);
    _statusLabel->setStyleSheet("color: gray;");
    mainLayout->addWidget(_statusLabel);

    connect(btnFindNext, &QPushButton::clicked, this, &FindReplaceDialog::onFindNext);
    connect(btnFindPrev, &QPushButton::clicked, this, &FindReplaceDialog::onFindPrev);
    connect(_btnReplace, &QPushButton::clicked, this, &FindReplaceDialog::onReplace);
    connect(_btnReplaceAll, &QPushButton::clicked, this, &FindReplaceDialog::onReplaceAll);

    connect(_findInput, &QLineEdit::returnPressed, this, &FindReplaceDialog::onFindNext);
}

void FindReplaceDialog::setFindText(const QString& text) {
    if (!text.isEmpty()) {
        _findInput->setText(text);
        _findInput->selectAll();
    }
}

void FindReplaceDialog::switchToReplaceMode(bool isReplace) {
    _replaceLabel->setVisible(isReplace);
    _replaceInput->setVisible(isReplace);
    _btnReplace->setVisible(isReplace);
    _btnReplaceAll->setVisible(isReplace);
    setWindowTitle(isReplace ? "替换" : "查找");
}

void FindReplaceDialog::onFindNext() {
    if (!_targetEditor) return;
    bool found = _targetEditor->findNext(
        _findInput->text(),
        _caseSensitiveCheck->isChecked(),
        _wholeWordCheck->isChecked(),
        _regexCheck->isChecked()
    );
    _statusLabel->setText(found ? "已找到匹配项" : "未找到匹配文本");
}

void FindReplaceDialog::onFindPrev() {
    if (!_targetEditor) return;
    bool found = _targetEditor->findPrev(
        _findInput->text(),
        _caseSensitiveCheck->isChecked(),
        _wholeWordCheck->isChecked(),
        _regexCheck->isChecked()
    );
    _statusLabel->setText(found ? "已找到匹配项" : "未找到匹配文本");
}

void FindReplaceDialog::onReplace() {
    if (!_targetEditor) return;
    bool replaced = _targetEditor->replaceOne(
        _findInput->text(),
        _replaceInput->text(),
        _caseSensitiveCheck->isChecked(),
        _wholeWordCheck->isChecked(),
        _regexCheck->isChecked()
    );
    _statusLabel->setText(replaced ? "已替换 1 处" : "未找到匹配文本");
}

void FindReplaceDialog::onReplaceAll() {
    if (!_targetEditor) return;
    int count = _targetEditor->replaceAll(
        _findInput->text(),
        _replaceInput->text(),
        _caseSensitiveCheck->isChecked(),
        _wholeWordCheck->isChecked(),
        _regexCheck->isChecked()
    );
    _statusLabel->setText(QString("共替换了 %1 处匹配项").arg(count));
}
