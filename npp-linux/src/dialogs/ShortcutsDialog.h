#pragma once

#include <QDialog>
#include <QTableWidget>

class ShortcutsDialog : public QDialog {
    Q_OBJECT
public:
    explicit ShortcutsDialog(QWidget* parent = nullptr);
};
