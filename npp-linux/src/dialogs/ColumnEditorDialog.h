#pragma once

#include "EditorView.h" // ColumnPad
#include <QDialog>
#include <QString>

class QButtonGroup;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;
class QWidget;

// 列编辑器的参数。数字类的字段一律以文本保存：它们要按当前所选进制解析，
// 解析失败时对话框要能原样留下用户输入的内容以便改正。
struct ColumnEditorParams {
    bool textMode = false;                 // 默认停在“插入数字”，与 上游 一致
    QString text;                          // “插入文本”的内容
    QString initial{QStringLiteral("1")};  // 初始值
    QString increment{QStringLiteral("1")}; // 增量
    QString repeat{QStringLiteral("1")};    // 重复次数（固定按十进制理解）
    int base = 10;                         // 2 / 8 / 10 / 16
    bool hexUpper = false;                 // 十六进制用 A-F 还是 a-f
    ColumnPad pad = ColumnPad::none;       // 开头补齐方式（上游 默认“无”）

    // 下面三个是上面三个字段按当前进制解析后的结果，只在对话框通过校验（accept）后被填写。
    // 解析放在这里是因为只有对话框知道自己用的是什么进制，调用方不必再推一遍。
    qint64 initialValue = 1;
    qint64 incrementValue = 1;
    int repeatValue = 1;
};

// 上游 的“列编辑器”(Alt+C)：把文本或递增数字序列写进列选区的每一行。
// 这里只负责收集与校验参数，真正的改写交给 EditorView::columnInsertText / columnInsertNumber。
class ColumnEditorDialog : public QDialog {
    Q_OBJECT
public:
    // hasColumnSel 为 false 时给出提示：本功能会退化为“从光标行写到文档结尾”。
    explicit ColumnEditorDialog(const ColumnEditorParams& in, bool hasColumnSel,
                                QWidget* parent = nullptr);

    // 只在 exec() 返回 Accepted 之后有意义。
    const ColumnEditorParams& params() const { return _params; }

private slots:
    void onModeToggled();
    void onOk();

private:
    void applyParamsToWidgets();
    void collectParamsFromWidgets();
    void updateEnabledState();
    void showError(const QString& msg);

    ColumnEditorParams _params;

    QRadioButton* _textRadio = nullptr;
    QRadioButton* _numberRadio = nullptr;
    QLineEdit* _textEdit = nullptr;

    QLineEdit* _initialEdit = nullptr;
    QLineEdit* _incrementEdit = nullptr;
    QLineEdit* _repeatEdit = nullptr;
    QComboBox* _padCombo = nullptr;

    QButtonGroup* _baseGroup = nullptr;
    QRadioButton* _decRadio = nullptr;
    QRadioButton* _octRadio = nullptr;
    QRadioButton* _hexRadio = nullptr;
    QRadioButton* _binRadio = nullptr;
    QComboBox* _hexCaseCombo = nullptr;

    QWidget* _numberArea = nullptr; // 数字模式独有的一整块控件
    QLabel* _hintLabel = nullptr;   // “没有列选区”时的退化说明
    QLabel* _errorLabel = nullptr;  // 校验失败提示（红色）

    QPushButton* _okBtn = nullptr;
};
