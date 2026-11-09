#include "ShortcutsDialog.h"
#include "ShortcutManager.h"

#include <QDialogButtonBox>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidgetItem>
#include <QVBoxLayout>

ShortcutsDialog::ShortcutsDialog(QWidget* actionRoot, QWidget* parent)
    : QDialog(parent)
    , _actionRoot(actionRoot)
{
    setWindowTitle("快捷键映射器 (Shortcut Mapper)");
    resize(780, 560);

    auto* layout = new QVBoxLayout(this);
    _table = new QTableWidget(this);
    _table->setColumnCount(4);
    _table->setHorizontalHeaderLabels({"类别", "功能名称", "快捷键", "说明"});
    _table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    _table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    _table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    _table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    // 快捷键列用 QKeySequenceEdit 编辑，不走 QTableWidgetItem 的文本编辑。
    _table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    _table->setSelectionBehavior(QAbstractItemView::SelectRows);
    _table->verticalHeader()->setVisible(false);
    layout->addWidget(_table);

    auto* hint = new QLabel(
        "点击「快捷键」列的输入框录入组合键，按「应用」保存；留空表示禁用该快捷键。"
        "Scintilla 原生手势（鼠标操作）不经过 QAction，仅展示、不可改键。", this);
    hint->setWordWrap(true);
    hint->setStyleSheet("color: gray;");
    layout->addWidget(hint);

    auto* btnBox = new QDialogButtonBox(
        QDialogButtonBox::Apply | QDialogButtonBox::RestoreDefaults | QDialogButtonBox::Close,
        this);
    connect(btnBox->button(QDialogButtonBox::Apply), &QPushButton::clicked,
            this, &ShortcutsDialog::onApply);
    connect(btnBox->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked,
            this, &ShortcutsDialog::onResetDefaults);
    connect(btnBox->button(QDialogButtonBox::Close), &QPushButton::clicked,
            this, &QDialog::accept);
    layout->addWidget(btnBox);

    rebuildTable();
}

void ShortcutsDialog::rebuildTable() {
    const auto oldEdits = _table->findChildren<QKeySequenceEdit*>();
    for (auto* edit : oldEdits) {
        edit->setParent(nullptr);
        edit->deleteLater();
    }
    _table->setRowCount(0);

    _entries = ShortcutManager::instance().fullList(_actionRoot);
    _table->setRowCount(_entries.size());
    for (int row = 0; row < _entries.size(); ++row) {
        const ShortcutEntry& entry = _entries.at(row);
        _table->setItem(row, 0, new QTableWidgetItem(entry.category));
        _table->setItem(row, 1, new QTableWidgetItem(entry.name));
        if (entry.nativeOnly) {
            auto* item = new QTableWidgetItem(entry.shortcutText);
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            _table->setItem(row, 2, item);
        } else {
            auto* edit = new QKeySequenceEdit(entry.currentSeq, _table);
            edit->setProperty("actionKey", entry.key);
            _table->setCellWidget(row, 2, edit);
        }
        _table->setItem(row, 3, new QTableWidgetItem(entry.description));
    }
}

void ShortcutsDialog::onApply() {
    QList<ShortcutEntry> desired = _entries;
    for (int row = 0; row < desired.size(); ++row) {
        if (desired[row].nativeOnly) continue;
        auto* edit = qobject_cast<QKeySequenceEdit*>(_table->cellWidget(row, 2));
        if (edit) desired[row].currentSeq = edit->keySequence();
    }

    QString conflict;
    if (!ShortcutManager::instance().applyOverrides(desired, &conflict)) {
        QMessageBox::warning(this, QStringLiteral("快捷键冲突"),
                             conflict + QStringLiteral("\n\n未保存任何修改。"));
        return;
    }
    ShortcutManager::instance().applyTo(_actionRoot);
    rebuildTable();
}

void ShortcutsDialog::onResetDefaults() {
    ShortcutManager::instance().resetAll();
    ShortcutManager::instance().applyTo(_actionRoot);
    rebuildTable();
}
