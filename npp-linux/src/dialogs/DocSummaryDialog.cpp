#include "DocSummaryDialog.h"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QFileInfo>

DocSummaryDialog::DocSummaryDialog(const QString& filePath, const DocSummary& summary, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("摘要信息");
    setMinimumWidth(380);

    auto* layout = new QVBoxLayout(this);

    auto* formLayout = new QFormLayout();
    formLayout->setLabelAlignment(Qt::AlignRight);

    QString fileName = filePath.isEmpty() ? "未命名文档" : QFileInfo(filePath).fileName();
    formLayout->addRow("文件名称:", new QLabel(fileName));
    formLayout->addRow("文件路径:", new QLabel(filePath.isEmpty() ? "尚未保存到磁盘" : filePath));
    formLayout->addRow("总大小:", new QLabel(QString("%1 字节").arg(summary.bytes)));
    formLayout->addRow("总行数:", new QLabel(QString::number(summary.lines)));
    formLayout->addRow("总字数 (含空白):", new QLabel(QString::number(summary.characters)));
    formLayout->addRow("总字数 (不含空白):", new QLabel(QString::number(summary.charactersNoSpaces)));
    formLayout->addRow("总词数:", new QLabel(QString::number(summary.words)));

    if (summary.selCharacters > 0) {
        formLayout->addRow("选区字符数:", new QLabel(QString::number(summary.selCharacters)));
        formLayout->addRow("选区行数:", new QLabel(QString::number(summary.selLines)));
    }

    layout->addLayout(formLayout);

    auto* btnBox = new QDialogButtonBox(QDialogButtonBox::Ok, this);
    connect(btnBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    layout->addWidget(btnBox);
}
