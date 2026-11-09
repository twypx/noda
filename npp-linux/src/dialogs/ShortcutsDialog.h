#pragma once

#include <QDialog>
#include <QList>
#include <QTableWidget>

#include "ShortcutManager.h"

class ShortcutsDialog : public QDialog {
    Q_OBJECT
public:
    // actionRoot 提供真实 QAction 树（通常是 MainWindow）。
    explicit ShortcutsDialog(QWidget* actionRoot, QWidget* parent = nullptr);

private slots:
    void onApply();
    void onResetDefaults();

private:
    void rebuildTable();

    QWidget* _actionRoot;
    QTableWidget* _table;
    QList<ShortcutEntry> _entries;
};
