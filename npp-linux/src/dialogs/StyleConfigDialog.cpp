#include "StyleConfigDialog.h"
#include "../StyleManager.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QColorDialog>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QSettings>
#include <QStringList>

namespace {

// QColor(RGB) <-> Scintilla/StyleManager 使用的 BGR 整数。
inline int toBgr(const QColor& c) {
    return (c.blue() << 16) | (c.green() << 8) | c.red();
}
inline QColor fromBgr(int bgr) {
    return QColor::fromRgb(bgr & 0xFF, (bgr >> 8) & 0xFF, (bgr >> 16) & 0xFF);
}

} // namespace

StyleConfigDialog::StyleConfigDialog(QWidget* parent)
    : QDialog(parent)
    , _fgColor(Qt::black)
    , _bgColor(Qt::white)
{
    setWindowTitle("语言样式配置器 (Style Configurator)");
    resize(560, 420);
    setupUI();
}

void StyleConfigDialog::setupUI() {
    auto* mainLayout = new QHBoxLayout(this);

    // 左侧：Global Styles + 全部已加载语言（真实 id 存于 UserRole）
    _langList = new QListWidget(this);
    auto* globalItem = new QListWidgetItem("Global Styles (全局默认样式)", _langList);
    globalItem->setData(Qt::UserRole, QStringLiteral("global"));
    for (const auto& name : StyleManager::instance().languageNames()) {
        auto* item = new QListWidgetItem(QString::fromStdString(name), _langList);
        item->setData(Qt::UserRole, QString::fromStdString(name));
    }
    mainLayout->addWidget(_langList, 1);

    // 右侧：样式设置
    auto* rightWidget = new QWidget(this);
    auto* rightLayout = new QVBoxLayout(rightWidget);

    auto* fontGroup = new QGroupBox("字体样式", rightWidget);
    auto* fontLayout = new QFormLayout(fontGroup);
    _fontCombo = new QFontComboBox(this);
    _fontCombo->setCurrentFont(QFont("Monospace"));
    _fontSizeSpin = new QSpinBox(this);
    _fontSizeSpin->setRange(6, 36);
    _fontSizeSpin->setValue(10);
    fontLayout->addRow("字体系列:", _fontCombo);
    fontLayout->addRow("字体大小:", _fontSizeSpin);
    _boldCheck = new QCheckBox("粗体 (Bold)", this);
    _italicCheck = new QCheckBox("斜体 (Italic)", this);
    fontLayout->addRow("", _boldCheck);
    fontLayout->addRow("", _italicCheck);
    rightLayout->addWidget(fontGroup);

    auto* colorGroup = new QGroupBox("色彩设置", rightWidget);
    auto* colorLayout = new QFormLayout(colorGroup);
    _btnFgColor = new QPushButton("选择前景色...", this);
    _btnBgColor = new QPushButton("选择背景色...", this);

    connect(_btnFgColor, &QPushButton::clicked, this, [this]() {
        QColor col = QColorDialog::getColor(_fgColor, this, "选择前景色");
        if (col.isValid()) {
            _fgColor = col;
            _btnFgColor->setStyleSheet(QString("background-color: %1; color: %2;")
                .arg(col.name(), col.lightness() < 128 ? "white" : "black"));
        }
    });
    connect(_btnBgColor, &QPushButton::clicked, this, [this]() {
        QColor col = QColorDialog::getColor(_bgColor, this, "选择背景色");
        if (col.isValid()) {
            _bgColor = col;
            _btnBgColor->setStyleSheet(QString("background-color: %1; color: %2;")
                .arg(col.name(), col.lightness() < 128 ? "white" : "black"));
        }
    });

    colorLayout->addRow("前景色:", _btnFgColor);
    colorLayout->addRow("背景色:", _btnBgColor);
    rightLayout->addWidget(colorGroup);

    _hintLabel = new QLabel(this);
    _hintLabel->setWordWrap(true);
    _hintLabel->setStyleSheet("color: gray;");
    rightLayout->addWidget(_hintLabel);

    auto* btnBox = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Close, this);
    connect(btnBox->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, &StyleConfigDialog::onApply);
    connect(btnBox->button(QDialogButtonBox::Close), &QPushButton::clicked, this, &QDialog::accept);
    rightLayout->addWidget(btnBox);

    mainLayout->addWidget(rightWidget, 2);

    connect(_langList, &QListWidget::currentRowChanged, this, &StyleConfigDialog::onLanguageSelected);
    _langList->setCurrentRow(0);
}

QString StyleConfigDialog::currentLangId() const {
    auto* item = _langList->currentItem();
    return item ? item->data(Qt::UserRole).toString() : QStringLiteral("global");
}

void StyleConfigDialog::onLanguageSelected(int row) {
    Q_UNUSED(row);
    const QString id = currentLangId();
    _hintLabel->setText(QString("正在编辑: %1 （样式槽位 0 / 默认文本）。点击“应用”后立即对所有打开文档生效，并持久化到用户配置。")
        .arg(id == "global" ? "Global Styles" : id));

    // 回填该语言此前保存的覆盖值（若有）
    QSettings s;
    s.beginGroup("styleOverride");
    if (s.contains(id)) {
        const QStringList parts = s.value(id).toString().split(';');
        if (parts.size() >= 2) {
            _fgColor = fromBgr(parts[0].toInt());
            _bgColor = fromBgr(parts[1].toInt());
            _boldCheck->setChecked(parts.size() >= 3 && parts[2].toInt() != 0);
            _italicCheck->setChecked(parts.size() >= 4 && parts[3].toInt() != 0);
            _btnFgColor->setStyleSheet(QString("background-color: %1").arg(_fgColor.name()));
            _btnBgColor->setStyleSheet(QString("background-color: %1").arg(_bgColor.name()));
        }
    }
    s.endGroup();
}

void StyleConfigDialog::onApply() {
    const QString id = currentLangId();
    // 约定值格式: "fgBgr;bgBgr;bold;italic"。字体族/字号沿用首选项的全局编辑器字体，
    // 这里聚焦于颜色与粗斜体覆盖，避免与 Preferences 的字体设置相互打架。
    const QString value = QString("%1;%2;%3;%4")
        .arg(toBgr(_fgColor))
        .arg(toBgr(_bgColor))
        .arg(_boldCheck->isChecked() ? 1 : 0)
        .arg(_italicCheck->isChecked() ? 1 : 0);

    QSettings s;
    s.beginGroup("styleOverride");
    s.setValue(id, value);
    s.endGroup();

    emit styleUpdated();
    _hintLabel->setText(QString("已应用并保存到配置：%1").arg(id == "global" ? "Global Styles" : id));
}
