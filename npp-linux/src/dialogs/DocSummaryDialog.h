#pragma once

#include <QDialog>
#include "EditorView.h"

class DocSummaryDialog : public QDialog {
    Q_OBJECT
public:
    explicit DocSummaryDialog(const QString& filePath, const DocSummary& summary, QWidget* parent = nullptr);
};
