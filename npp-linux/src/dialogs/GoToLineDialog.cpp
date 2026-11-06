#include "GoToLineDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QIntValidator>

GoToLineDialog::GoToLineDialog(int currentLine, int maxLine, QWidget* parent)
    : QDialog(parent), _maxLine(maxLine)
{
    setWindowTitle("跳转到行");
    setMinimumWidth(280);

    auto* mainLayout = new QVBoxLayout(this);
    auto* tipLabel = new QLabel(QString("当前行: %1\n目标行号 (1 - %2):").arg(currentLine).arg(maxLine));
    mainLayout->addWidget(tipLabel);

    _lineInput = new QLineEdit(this);
    _lineInput->setValidator(new QIntValidator(1, maxLine, this));
    _lineInput->setText(QString::number(currentLine));
    _lineInput->selectAll();
    mainLayout->addWidget(_lineInput);

    auto* btnLayout = new QHBoxLayout();
    auto* btnOk = new QPushButton("跳转", this);
    auto* btnCancel = new QPushButton("取消", this);
    btnOk->setDefault(true);

    btnLayout->addWidget(btnOk);
    btnLayout->addWidget(btnCancel);
    mainLayout->addLayout(btnLayout);

    connect(btnOk, &QPushButton::clicked, this, &GoToLineDialog::onGoClicked);
    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(_lineInput, &QLineEdit::returnPressed, this, &GoToLineDialog::onGoClicked);
}

void GoToLineDialog::onGoClicked() {
    bool ok = false;
    int line = _lineInput->text().toInt(&ok);
    if (ok && line >= 1 && line <= _maxLine) {
        _targetLine = line;
        accept();
    }
}

int GoToLineDialog::getTargetLine() const {
    return _targetLine;
}
