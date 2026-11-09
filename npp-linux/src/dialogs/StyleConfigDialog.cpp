#include "StyleConfigDialog.h"
#include "../StyleManager.h"
#include "StyleOverride.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QColorDialog>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QListWidgetItem>
#include <QSettings>
#include <QSignalBlocker>
#include <QFont>

namespace {

inline int toBgr(const QColor& c) {
    return (c.blue() << 16) | (c.green() << 8) | c.red();
}
inline QColor fromBgr(int bgr) {
    return QColor::fromRgb(bgr & 0xFF, (bgr >> 8) & 0xFF, (bgr >> 16) & 0xFF);
}

const QLatin1String kGlobalId("global");

} // namespace

StyleConfigDialog::StyleConfigDialog(QWidget* parent)
    : QDialog(parent)
    , _fgColor(Qt::black)
    , _bgColor(Qt::white)
{
    setWindowTitle("语言样式配置器 (Style Configurator)");
    resize(590, 440);
    setupUI();
}

void StyleConfigDialog::setupUI() {
    auto* mainLayout = new QHBoxLayout(this);

    _langList = new QListWidget(this);
    auto* globalItem = new QListWidgetItem("Global Styles (全局默认样式)", _langList);
    globalItem->setData(Qt::UserRole, QString(kGlobalId));
    for (const auto& name : StyleManager::instance().languageNames()) {
        auto* item = new QListWidgetItem(QString::fromStdString(name), _langList);
        item->setData(Qt::UserRole, QString::fromStdString(name));
    }
    mainLayout->addWidget(_langList, 1);

    auto* rightWidget = new QWidget(this);
    auto* rightLayout = new QVBoxLayout(rightWidget);

    auto* fontGroup = new QGroupBox("样式槽位与字体", rightWidget);
    auto* fontLayout = new QFormLayout(fontGroup);
    _styleCombo = new QComboBox(this);
    _styleCombo->setObjectName(QStringLiteral("styleSlotCombo"));
    fontLayout->addRow("样式槽位:", _styleCombo);
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
        const QColor col = QColorDialog::getColor(_fgColor, this, "选择前景色");
        if (col.isValid()) { _fgColor = col; updateColorButtons(); }
    });
    connect(_btnBgColor, &QPushButton::clicked, this, [this]() {
        const QColor col = QColorDialog::getColor(_bgColor, this, "选择背景色");
        if (col.isValid()) { _bgColor = col; updateColorButtons(); }
    });
    colorLayout->addRow("前景色:", _btnFgColor);
    colorLayout->addRow("背景色:", _btnBgColor);
    rightLayout->addWidget(colorGroup);

    _hintLabel = new QLabel(this);
    _hintLabel->setWordWrap(true);
    _hintLabel->setStyleSheet("color: gray;");
    rightLayout->addWidget(_hintLabel);

    auto* btnBox = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Close, this);
    connect(btnBox->button(QDialogButtonBox::Apply), &QPushButton::clicked,
            this, &StyleConfigDialog::onApply);
    connect(btnBox->button(QDialogButtonBox::Close), &QPushButton::clicked,
            this, &QDialog::accept);
    rightLayout->addWidget(btnBox);

    mainLayout->addWidget(rightWidget, 2);

    connect(_langList, &QListWidget::currentRowChanged,
            this, &StyleConfigDialog::onLanguageSelected);
    connect(_styleCombo, &QComboBox::currentIndexChanged,
            this, &StyleConfigDialog::onStyleSlotChanged);
    _langList->setCurrentRow(0);
}

QString StyleConfigDialog::currentLangId() const {
    auto* item = _langList->currentItem();
    return item ? item->data(Qt::UserRole).toString() : QString(kGlobalId);
}

void StyleConfigDialog::updateColorButtons() {
    auto paint = [](QPushButton* button, const QColor& color) {
        button->setStyleSheet(QStringLiteral("background-color: %1; color: %2;")
            .arg(color.name(),
                 color.lightness() < 128 ? QStringLiteral("white") : QStringLiteral("black")));
    };
    paint(_btnFgColor, _fgColor);
    paint(_btnBgColor, _bgColor);
}

void StyleConfigDialog::onLanguageSelected(int row) {
    Q_UNUSED(row);
    const QString id = currentLangId();

    _styleCombo->blockSignals(true);
    _styleCombo->clear();
    if (id == kGlobalId) {
        _styleCombo->addItem(QStringLiteral("全局默认文本"), 0);
    } else {
        const auto* styles = StyleManager::instance().getStyles(id.toStdString());
        if (styles) {
            for (const auto& s : *styles) {
                const QString label = QStringLiteral("%1  [styleID %2]")
                    .arg(s.name.empty() ? QStringLiteral("未命名样式")
                                        : QString::fromStdString(s.name))
                    .arg(s.styleID);
                _styleCombo->addItem(label, s.styleID);
            }
        }
        if (_styleCombo->count() == 0)
            _styleCombo->addItem(QStringLiteral("默认文本"), 0);
    }
    _styleCombo->setCurrentIndex(0);
    _styleCombo->blockSignals(false);
    onStyleSlotChanged(_styleCombo->currentIndex());
}

void StyleConfigDialog::onStyleSlotChanged(int index) {
    if (index < 0 || !_styleCombo) return;
    const QString id = currentLangId();
    const int styleId = _styleCombo->itemData(index).toInt();

    QColor fg = Qt::black;
    QColor bg = Qt::white;
    bool bold = false;
    bool italic = false;
    QString family = QStringLiteral("Monospace");
    int size = 10;

    if (id == kGlobalId) {
        const GlobalStyles& g = StyleManager::instance().getGlobalStyles();
        if (g.defaultFg >= 0) fg = fromBgr(g.defaultFg);
        if (g.defaultBg >= 0) bg = fromBgr(g.defaultBg);
        family = QString::fromStdString(g.defaultFont);
        size = g.defaultFontSize > 0 ? g.defaultFontSize : 10;
    } else if (const auto* styles = StyleManager::instance().getStyles(id.toStdString())) {
        for (const auto& s : *styles) {
            if (s.styleID != styleId) continue;
            if (s.fgColor >= 0) fg = fromBgr(s.fgColor);
            if (s.bgColor >= 0) bg = fromBgr(s.bgColor);
            bold = s.bold;
            italic = s.italic;
            if (!s.fontName.empty()) family = QString::fromStdString(s.fontName);
            if (s.fontSize > 0) size = s.fontSize;
            break;
        }
    }

    QSettings settings;
    settings.beginGroup("styleOverride");
    const QString key = StyleOverride::settingsKey(id, styleId);
    QString value = settings.value(key).toString();
    if (value.isEmpty() && styleId == 0) value = settings.value(id).toString();
    settings.endGroup();
    const auto override = StyleOverride::deserialize(key, value);
    if (override) {
        if (override->fg >= 0) fg = fromBgr(override->fg);
        if (override->bg >= 0) bg = fromBgr(override->bg);
        bold = override->bold;
        italic = override->italic;
        if (!override->fontName.isEmpty()) family = override->fontName;
        if (override->fontSize > 0) size = override->fontSize;
    }

    {
        QSignalBlocker blockFont(_fontCombo);
        _fontCombo->setCurrentFont(QFont(family));
        QSignalBlocker blockSize(_fontSizeSpin);
        _fontSizeSpin->setValue(qBound(_fontSizeSpin->minimum(), size, _fontSizeSpin->maximum()));
    }
    _boldCheck->setChecked(bold);
    _italicCheck->setChecked(italic);
    _fgColor = fg;
    _bgColor = bg;
    updateColorButtons();

    _hintLabel->setText(QStringLiteral("正在编辑：%1 / %2。字体、字号、颜色与粗斜体在“应用”后对所有文档生效并持久化。")
        .arg(id == kGlobalId ? QStringLiteral("全局默认样式") : id,
             _styleCombo->itemText(index)));
}

void StyleConfigDialog::onApply() {
    const QString id = currentLangId();
    const int styleId = _styleCombo->currentData().toInt();

    StyleOverride o;
    o.langId = id;
    o.styleId = styleId;
    o.fg = toBgr(_fgColor);
    o.bg = toBgr(_bgColor);
    o.bold = _boldCheck->isChecked();
    o.italic = _italicCheck->isChecked();
    o.fontName = _fontCombo->currentFont().family();
    o.fontSize = _fontSizeSpin->value();

    QSettings settings;
    settings.beginGroup("styleOverride");
    // styleId 0 与旧格式键等价，写新键前删旧键，避免两份覆盖按不确定顺序互相覆盖。
    if (styleId == 0) settings.remove(id);
    settings.setValue(StyleOverride::settingsKey(id, styleId), o.serialize());
    settings.endGroup();

    emit styleUpdated();
    _hintLabel->setText(QStringLiteral("已应用并保存：%1 / %2（字体 %3 %4pt）")
        .arg(id, _styleCombo->currentText(), o.fontName).arg(o.fontSize));
}
