#pragma once

#include <QDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>

class GoToLineDialog : public QDialog {
    Q_OBJECT
public:
    explicit GoToLineDialog(int currentLine, int maxLine, QWidget* parent = nullptr);
    int getTargetLine() const;

private slots:
    void onGoClicked();

private:
    QLineEdit* _lineInput;
    int _targetLine{-1};
    int _maxLine{1};
};
