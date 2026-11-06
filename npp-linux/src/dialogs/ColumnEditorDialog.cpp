#include "ColumnEditorDialog.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

namespace {

QString baseName(int base) {
    switch (base) {
    case 2:  return QStringLiteral("二进制");
    case 8:  return QStringLiteral("八进制");
    case 16: return QStringLiteral("十六进制");
    default: return QStringLiteral("十进制");
    }
}

// 按所选进制解析一个字段：空值、含非法字符、负数都算失败。out 允许为空，只要判断合法性。
bool parseInBase(const QString& raw, int base, qint64* out) {
    const QString trimmed = raw.trimmed();
    if (trimmed.isEmpty()) return false;
    bool ok = false;
    const qint64 v = trimmed.toLongLong(&ok, base);
    if (!ok || v < 0) return false;
    if (out) *out = v;
    return true;
}

} // namespace

ColumnEditorDialog::ColumnEditorDialog(const ColumnEditorParams& in, bool hasColumnSel, QWidget* parent)
    : QDialog(parent), _params(in)
{
    setWindowTitle("列编辑器");

    auto* mainLayout = new QVBoxLayout(this);

    // --- 两种模式：插入文本 / 插入数字 -------------------------------------
    _textRadio = new QRadioButton("插入文本(&T)", this);
    _numberRadio = new QRadioButton("插入数字(&N)", this);
    _textEdit = new QLineEdit(this);
    _textEdit->setPlaceholderText("要写入每一行的内容");

    auto* textRow = new QHBoxLayout;
    textRow->addWidget(_textRadio);
    textRow->addWidget(_textEdit, 1);
    mainLayout->addLayout(textRow);

    mainLayout->addWidget(_numberRadio);

    // --- 数字模式的参数区（整块随模式启用/禁用）-----------------------------
    _numberArea = new QWidget(this);
    auto* grid = new QGridLayout(_numberArea);
    grid->setContentsMargins(24, 0, 0, 0);

    _initialEdit = new QLineEdit(_numberArea);
    _incrementEdit = new QLineEdit(_numberArea);
    _repeatEdit = new QLineEdit(_numberArea);
    _padCombo = new QComboBox(_numberArea);
    _padCombo->addItems({"无", "补零", "补空格"});

    grid->addWidget(new QLabel("初始值:", _numberArea), 0, 0);
    grid->addWidget(_initialEdit, 0, 1);
    grid->addWidget(new QLabel("增量:", _numberArea), 0, 2);
    grid->addWidget(_incrementEdit, 0, 3);
    grid->addWidget(new QLabel("重复次数:", _numberArea), 1, 0);
    grid->addWidget(_repeatEdit, 1, 1);
    grid->addWidget(new QLabel("开头补齐:", _numberArea), 1, 2);
    grid->addWidget(_padCombo, 1, 3);

    // 进制选择；大小写只对十六进制有意义，所以放在同一行末尾。
    _decRadio = new QRadioButton("十进制(&D)", _numberArea);
    _octRadio = new QRadioButton("八进制(&O)", _numberArea);
    _hexRadio = new QRadioButton("十六进制(&H)", _numberArea);
    _binRadio = new QRadioButton("二进制(&B)", _numberArea);
    _baseGroup = new QButtonGroup(this);
    // 按钮 id 直接就是进制本身，collect 时不必再翻译一次。
    _baseGroup->addButton(_decRadio, 10);
    _baseGroup->addButton(_octRadio, 8);
    _baseGroup->addButton(_hexRadio, 16);
    _baseGroup->addButton(_binRadio, 2);
    _decRadio->setChecked(true);

    _hexCaseCombo = new QComboBox(_numberArea);
    _hexCaseCombo->addItems({"a-f", "A-F"});

    auto* formatRow = new QHBoxLayout;
    formatRow->addWidget(new QLabel("格式:", _numberArea));
    formatRow->addWidget(_decRadio);
    formatRow->addWidget(_octRadio);
    formatRow->addWidget(_hexRadio);
    formatRow->addWidget(_binRadio);
    formatRow->addWidget(_hexCaseCombo);
    formatRow->addStretch();
    grid->addLayout(formatRow, 2, 0, 1, 4);

    mainLayout->addWidget(_numberArea);

    // --- 提示与校验信息 ----------------------------------------------------
    _hintLabel = new QLabel(this);
    _hintLabel->setWordWrap(true);
    if (!hasColumnSel) {
        // 没有列选区时本功能会一路写到文档结尾，这个后果必须提前讲清楚。
        _hintLabel->setText("当前没有列选区：将从光标所在列一直写到文档最后一行。\n"
                            "要只改写某几列，请按住 Alt 拖动鼠标，或按 Alt+Shift+方向键，"
                            "或用“编辑 > 开始/结束 按列选取”两点定块后再打开本对话框。");
    }
    _hintLabel->setVisible(hasColumnSel ? false : true);
    mainLayout->addWidget(_hintLabel);

    _errorLabel = new QLabel(this);
    _errorLabel->setWordWrap(true);
    QPalette errPal = _errorLabel->palette();
    // 深浅两套主题下都要显眼，取一个中红而不是纯红。
    errPal.setColor(QPalette::WindowText, QColor(214, 60, 60));
    _errorLabel->setPalette(errPal);
    mainLayout->addWidget(_errorLabel);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    _okBtn = buttons->button(QDialogButtonBox::Ok);
    _okBtn->setText("确定");
    buttons->button(QDialogButtonBox::Cancel)->setText("取消");
    mainLayout->addWidget(buttons);

    connect(_textRadio, &QRadioButton::toggled, this, &ColumnEditorDialog::onModeToggled);
    connect(_baseGroup, &QButtonGroup::idClicked, this, [this]() {
        updateEnabledState();
        showError(QString());
    });
    connect(_textEdit, &QLineEdit::textChanged, this, &ColumnEditorDialog::updateEnabledState);
    connect(buttons, &QDialogButtonBox::accepted, this, &ColumnEditorDialog::onOk);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    applyParamsToWidgets();
    // 两个提示标签平时都是空文本，不隐藏会在对话框底部留出两块空白。
    showError(QString());
    setMinimumWidth(460);
    _textEdit->setFocus();
}

void ColumnEditorDialog::applyParamsToWidgets() {
    _textRadio->setChecked(_params.textMode);
    _numberRadio->setChecked(!_params.textMode);
    _textEdit->setText(_params.text);
    _initialEdit->setText(_params.initial);
    _incrementEdit->setText(_params.increment);
    _repeatEdit->setText(_params.repeat);
    _padCombo->setCurrentIndex(static_cast<int>(_params.pad));
    _hexCaseCombo->setCurrentIndex(_params.hexUpper ? 1 : 0);

    if (QAbstractButton* btn = _baseGroup->button(_params.base)) {
        btn->setChecked(true);
    } else {
        _decRadio->setChecked(true);
    }
    updateEnabledState();
}

void ColumnEditorDialog::collectParamsFromWidgets() {
    _params.textMode = _textRadio->isChecked();
    _params.text = _textEdit->text();
    _params.initial = _initialEdit->text();
    _params.increment = _incrementEdit->text();
    _params.repeat = _repeatEdit->text();
    _params.base = _baseGroup->checkedId();
    _params.hexUpper = _hexCaseCombo->currentIndex() == 1;
    _params.pad = static_cast<ColumnPad>(_padCombo->currentIndex());
}

void ColumnEditorDialog::onModeToggled() {
    updateEnabledState();
    showError(QString());
}

void ColumnEditorDialog::updateEnabledState() {
    const bool textMode = _textRadio->isChecked();
    _textEdit->setEnabled(textMode);
    _numberArea->setEnabled(!textMode);
    _hexCaseCombo->setEnabled(!textMode && _baseGroup->checkedId() == 16);
    // 同 Notepad++：文本模式下内容为空时确定键无效；数字模式始终可点，靠校验给反馈。
    _okBtn->setEnabled(!textMode || !_textEdit->text().isEmpty());
}

void ColumnEditorDialog::showError(const QString& msg) {
    _errorLabel->setText(msg);
    _errorLabel->setVisible(!msg.isEmpty());
}

void ColumnEditorDialog::onOk() {
    collectParamsFromWidgets();

    if (_params.textMode) {
        if (_params.text.isEmpty()) {
            showError("插入文本的内容不能为空。");
            return;
        }
        accept();
        return;
    }

    // 初始值与增量按所选进制解析；“重复次数”是“同一个数字占几行”的计数，固定按十进制理解，
    // 否则选中十六进制时填 10 会莫名其妙变成 16 行，难以直觉判断。
    const int base = _params.base;
    if (!parseInBase(_params.initial, base, &_params.initialValue)) {
        showError(QString("初始值 %1 不是合法的 %2 非负整数。").arg(_params.initial, baseName(base)));
        _initialEdit->setFocus();
        return;
    }
    if (!parseInBase(_params.increment, base, &_params.incrementValue)) {
        showError(QString("增量 %1 不是合法的 %2 非负整数。").arg(_params.increment, baseName(base)));
        _incrementEdit->setFocus();
        return;
    }
    if (!parseInBase(_params.repeat, 10, nullptr)) {
        showError(QString("重复次数 %1 不是合法的十进制非负整数。").arg(_params.repeat));
        _repeatEdit->setFocus();
        return;
    }
    qint64 repeat64 = 1;
    parseInBase(_params.repeat, 10, &repeat64);
    _params.repeatValue = static_cast<int>(qMax<qint64>(1, repeat64));

    accept();
}
