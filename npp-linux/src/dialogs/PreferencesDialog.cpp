#include "PreferencesDialog.h"
#include "StyleManager.h"

#include <QApplication>
#include <QFontDatabase>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QButtonGroup>
#include <QColorDialog>
#include <QColor>

namespace FontFamilies {
namespace {
struct Cache {
    QStringList list;
    bool built{false};
    int builds{0};
};
Cache& cache() {
    static Cache instance;
    return instance;
}
} // namespace

const QStringList& all() {
    Cache& c = cache();
    if (!c.built) {
        // Fontconfig 全量枚举只做一次，之后三个下拉共用这份只读清单。
        c.list = QFontDatabase::families();
        c.built = true;
        ++c.builds;
    }
    return c.list;
}

int buildCount() { return cache().builds; }

void invalidate() {
    Cache& c = cache();
    c.list.clear();
    c.built = false;
}
} // namespace FontFamilies

namespace {

const QString kSystemFontLabel = QStringLiteral("（系统默认）");

// “系统默认”项的 item data 是空串，其余字体项没有 data，据此区分标签文字。
void addSystemFontItem(QComboBox* combo) {
    combo->insertItem(0, kSystemFontLabel, QString());
}

// 存储的字体可能已被卸载：找不到就临时补进列表，绝不能悄悄退回第一项后写回配置。
void selectFontFamily(QComboBox* combo, const QString& family) {
    if (family.isEmpty()) {
        combo->setCurrentIndex(0);
        return;
    }
    int index = combo->findText(family);
    if (index < 0) {
        combo->addItem(family);
        index = combo->count() - 1;
    }
    combo->setCurrentIndex(index);
}

QString fontComboValue(const QComboBox* combo) {
    if (combo->currentData().isValid() && combo->currentData().toString().isEmpty())
        return QString();
    const QString text = combo->currentText();
    return text == kSystemFontLabel ? QString() : text;
}

// 普通文本下拉：存储值不在列表里时补进去，避免“打开就点确定”把配置改成首项。
void selectTextValue(QComboBox* combo, const QString& value) {
    int index = combo->findText(value);
    if (index < 0 && !value.isEmpty()) {
        combo->addItem(value);
        index = combo->count() - 1;
    }
    if (index >= 0) combo->setCurrentIndex(index);
}

// 旧版本/首选项里可能存的是显示名（C++、Shell），统一交给 StyleManager 折算。
QString canonicalLanguageId(const QString& value) {
    return QString::fromStdString(
        StyleManager::instance().resolveLanguageId(value.toStdString()));
}

void selectLanguageValue(QComboBox* combo, const QString& value) {
    if (value.isEmpty()) {
        combo->setCurrentIndex(0);
        return;
    }
    // 已经是有效 id 直接命中。
    int index = combo->findData(value);
    if (index >= 0) {
        combo->setCurrentIndex(index);
        return;
    }

    const QString id = canonicalLanguageId(value);
    if (id != QLatin1String("normal") || value == QLatin1String("normal")
        || value.compare(QStringLiteral("Normal Text"), Qt::CaseInsensitive) == 0) {
        index = combo->findData(id);
        if (index >= 0) {
            combo->setCurrentIndex(index);
            return;
        }
    }

    // 未知值保留为一项，避免点确定后静默改成 Normal Text。
    combo->addItem(value, value);
    combo->setCurrentIndex(combo->count() - 1);
}

} // namespace

PreferencesDialog::PreferencesDialog(const AppPreferences& current, QWidget* parent)
    : QDialog(parent)
    , _prefs(current)
{
    setWindowTitle(tr("首选项"));
    resize(720, 520);
    setMinimumSize(640, 460);
    setupUI(current);
}

void PreferencesDialog::setupUI(const AppPreferences& current) {
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(12, 12, 12, 12);
    rootLayout->setSpacing(10);

    // 左右分栏布局
    auto* contentLayout = new QHBoxLayout();
    contentLayout->setSpacing(12);

    // 左侧配置项目录树
    _treeWidget = new QTreeWidget(this);
    _treeWidget->setHeaderHidden(true);
    _treeWidget->setFixedWidth(190);
    _treeWidget->setIndentation(16);
    _treeWidget->setAnimated(true);

    // 右侧配置项堆栈
    _stackedWidget = new QStackedWidget(this);

    // 构建右侧页面
    auto* pageAppearance = createPageAppearance(current);
    auto* pageTabBar = createPageTabBar(current);
    auto* pageCodeEdit = createPageCodeEdit(current);
    auto* pageFont = createPageFont(current);
    auto* pageTheme = createPageTheme(current);
    auto* pageNewDoc = createPageNewDoc(current);
    auto* pageAutoComp = createPageAutoCompletion(current);
    auto* pageHighlight = createPageHighlight(current);
    auto* pageSession = createPageSessionBackup(current);

    _stackedWidget->addWidget(pageAppearance);    // Index 0
    _stackedWidget->addWidget(pageTabBar);        // Index 1
    _stackedWidget->addWidget(pageCodeEdit);      // Index 2
    _stackedWidget->addWidget(pageFont);          // Index 3
    _stackedWidget->addWidget(pageTheme);         // Index 4
    _stackedWidget->addWidget(pageNewDoc);        // Index 5
    _stackedWidget->addWidget(pageAutoComp);      // Index 6
    _stackedWidget->addWidget(pageHighlight);     // Index 7
    _stackedWidget->addWidget(pageSession);       // Index 8

    // 构建左侧树节点并关联右侧页面索引 (Qt::UserRole)
    // 1. 外观与行为
    auto* itemAppearance = new QTreeWidgetItem(_treeWidget, QStringList(tr("外观与行为")));
    itemAppearance->setData(0, Qt::UserRole, 0); // 默认子页

    auto* subAppearance = new QTreeWidgetItem(itemAppearance, QStringList(tr("外观")));
    subAppearance->setData(0, Qt::UserRole, 0);

    auto* subTabBar = new QTreeWidgetItem(itemAppearance, QStringList(tr("标签栏与工具栏")));
    subTabBar->setData(0, Qt::UserRole, 1);

    // 2. 编辑器
    auto* itemEditor = new QTreeWidgetItem(_treeWidget, QStringList(tr("编辑器")));
    itemEditor->setData(0, Qt::UserRole, 2);

    auto* subCode = new QTreeWidgetItem(itemEditor, QStringList(tr("代码编辑")));
    subCode->setData(0, Qt::UserRole, 2);

    auto* subFont = new QTreeWidgetItem(itemEditor, QStringList(tr("字体")));
    subFont->setData(0, Qt::UserRole, 3);

    auto* subTheme = new QTreeWidgetItem(itemEditor, QStringList(tr("配色方案")));
    subTheme->setData(0, Qt::UserRole, 4);

    auto* subNewDoc = new QTreeWidgetItem(itemEditor, QStringList(tr("新建文档")));
    subNewDoc->setData(0, Qt::UserRole, 5);

    auto* subAutoComp = new QTreeWidgetItem(itemEditor, QStringList(tr("自动补全")));
    subAutoComp->setData(0, Qt::UserRole, 6);

    auto* subHighlight = new QTreeWidgetItem(itemEditor, QStringList(tr("高亮匹配")));
    subHighlight->setData(0, Qt::UserRole, 7);

    // 3. 会话与备份
    auto* itemSession = new QTreeWidgetItem(_treeWidget, QStringList(tr("会话与备份")));
    itemSession->setData(0, Qt::UserRole, 8);

    _treeWidget->expandAll();
    _treeWidget->setCurrentItem(subAppearance);

    connect(_treeWidget, &QTreeWidget::currentItemChanged,
            this, &PreferencesDialog::onTreeItemChanged);

    contentLayout->addWidget(_treeWidget);
    contentLayout->addWidget(_stackedWidget, 1);
    rootLayout->addLayout(contentLayout, 1);

    // 底部确定/取消按钮
    auto* btnBox = new QDialogButtonBox(this);
    auto* btnOk = btnBox->addButton(tr("确定(&O)"), QDialogButtonBox::AcceptRole);
    auto* btnCancel = btnBox->addButton(tr("取消(&C)"), QDialogButtonBox::RejectRole);
    btnOk->setDefault(true);

    connect(btnBox, &QDialogButtonBox::accepted, this, &PreferencesDialog::onAccept);
    connect(btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    rootLayout->addWidget(btnBox);
}

void PreferencesDialog::onTreeItemChanged(QTreeWidgetItem* current, QTreeWidgetItem* /*previous*/) {
    if (!current) return;
    int pageIdx = current->data(0, Qt::UserRole).toInt();
    if (pageIdx >= 0 && pageIdx < _stackedWidget->count()) {
        _stackedWidget->setCurrentIndex(pageIdx);
    }
}

// ---------------- 页面 0: 外观 ----------------
QWidget* PreferencesDialog::createPageAppearance(const AppPreferences& current) {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(10, 0, 10, 0);

    auto* title = new QLabel(tr("<h3>外观</h3>"), page);
    layout->addWidget(title);

    auto* formLayout = new QFormLayout();
    formLayout->setLabelAlignment(Qt::AlignRight);

    _langCombo = new QComboBox(page);
    // 语言选项用各自的“原生名称”，不随界面语言翻译（否则英文模式下会变成 English / English）。
    _langCombo->setProperty("nppNoL10n", true);
    _langCombo->addItem(QStringLiteral("简体中文"), QStringLiteral("zh_CN"));
    _langCombo->addItem(QStringLiteral("English"), QStringLiteral("en_US"));
    const int languageIndex = _langCombo->findData(current.uiLanguage);
    if (languageIndex >= 0) _langCombo->setCurrentIndex(languageIndex);
    formLayout->addRow(tr("界面语言:"), _langCombo);

    _darkModeCheck = new QCheckBox(tr("启用深色模式"), page);
    _darkModeCheck->setChecked(current.darkMode);
    formLayout->addRow("", _darkModeCheck);

    auto* fontGroup = new QGroupBox(tr("主界面字体"), page);
    auto* fontGroupLayout = new QFormLayout(fontGroup);

    _uiFontZhCombo = new QComboBox(fontGroup);
    _uiFontZhCombo->setObjectName(QStringLiteral("uiFontZh"));
    _uiFontZhCombo->addItems(FontFamilies::all());
    addSystemFontItem(_uiFontZhCombo);
    selectFontFamily(_uiFontZhCombo, current.uiFontZh);
    fontGroupLayout->addRow(tr("中文字体:"), _uiFontZhCombo);

    _uiFontEnCombo = new QComboBox(fontGroup);
    _uiFontEnCombo->setObjectName(QStringLiteral("uiFontEn"));
    _uiFontEnCombo->addItems(FontFamilies::all());
    addSystemFontItem(_uiFontEnCombo);
    selectFontFamily(_uiFontEnCombo, current.uiFontEn);
    fontGroupLayout->addRow(tr("英文字体:"), _uiFontEnCombo);

    layout->addLayout(formLayout);

    // 主题色：常见预设 + 颜色选择器自定义。色板用互斥 QToolButton，
    // 自定义色不属于任何预设，由右侧预览块显示当前值。
    auto* accentGroup = new QGroupBox(tr("主题色"), page);
    auto* accentLayout = new QVBoxLayout(accentGroup);
    auto* accentHint = new QLabel(tr("主题色将影响选中、链接、按钮高亮等元素。"), accentGroup);
    accentHint->setWordWrap(true);
    accentLayout->addWidget(accentHint);

    _accentColor = QColor::isValidColor(current.accentColor)
                       ? current.accentColor : QStringLiteral("#A882FF");
    auto* swatchRow = new QHBoxLayout();
    swatchRow->setSpacing(8);
    _accentGroup = new QButtonGroup(accentGroup);
    _accentGroup->setExclusive(true);

    const QVector<QPair<QString, QString>> presets = {
        {tr("默认紫"),    QStringLiteral("#A882FF")},
        {tr("macOS 蓝"),  QStringLiteral("#0A84FF")},
        {tr("Ubuntu 橙"), QStringLiteral("#E95420")},
        {tr("GNOME 蓝"),  QStringLiteral("#3584E4")},
        {tr("翠绿"),      QStringLiteral("#26A269")},
        {tr("玫瑰红"),    QStringLiteral("#E5484D")},
        {tr("琥珀"),      QStringLiteral("#E5A50A")},
    };
    for (int i = 0; i < presets.size(); ++i) {
        const QString name = presets.at(i).first;
        const QString hex = presets.at(i).second;
        auto* swatch = new QToolButton(accentGroup);
        swatch->setObjectName(QStringLiteral("accentPreset_%1").arg(i));
        swatch->setProperty("accentHex", hex);
        swatch->setCheckable(true);
        swatch->setFixedSize(26, 26);
        swatch->setToolTip(name);
        swatch->setStyleSheet(QStringLiteral(
            "QToolButton { background-color: %1; border: 2px solid transparent; border-radius: 13px; }"
            "QToolButton:hover { border: 2px solid palette(highlight); }"
            "QToolButton:checked { border: 2px solid palette(window-text); }").arg(hex));
        swatch->setChecked(hex.compare(_accentColor, Qt::CaseInsensitive) == 0);
        _accentGroup->addButton(swatch, i);
        _accentPresets.append(swatch);
        swatchRow->addWidget(swatch);
        connect(swatch, &QToolButton::clicked, this, [this, swatch]() {
            _accentColor = swatch->property("accentHex").toString();
            updateAccentPreview();
        });
    }
    swatchRow->addStretch(1);

    _accentCustomButton = new QPushButton(tr("自定义…"), accentGroup);
    _accentCustomButton->setObjectName(QStringLiteral("accentCustomButton"));
    swatchRow->addWidget(_accentCustomButton);
    connect(_accentCustomButton, &QPushButton::clicked, this, [this, accentGroup]() {
        // 与文件对话框一致：强制 Qt 内建对话框，避免部分 Linux 桌面原生对话框弹不出。
        const QColor chosen = QColorDialog::getColor(
            QColor(_accentColor), accentGroup, tr("选择主题色"),
            QColorDialog::DontUseNativeDialog);
        if (!chosen.isValid()) return;
        _accentColor = chosen.name().toUpper();
        if (auto* checked = _accentGroup->checkedButton()) {
            _accentGroup->setExclusive(false);
            checked->setChecked(false);
            _accentGroup->setExclusive(true);
        }
        updateAccentPreview();
    });

    _accentValueLabel = new QLabel(accentGroup);
    _accentValueLabel->setObjectName(QStringLiteral("accentValueLabel"));
    swatchRow->addWidget(_accentValueLabel);
    accentLayout->addLayout(swatchRow);
    layout->addWidget(accentGroup);
    updateAccentPreview();

    layout->addWidget(fontGroup);
    layout->addStretch();
    return page;
}

// ---------------- 页面 1: 标签栏与工具栏 ----------------
QWidget* PreferencesDialog::createPageTabBar(const AppPreferences& current) {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(10, 0, 10, 0);
    layout->setSpacing(8);

    auto* title = new QLabel(tr("<h3>标签栏与工具栏</h3>"), page);
    layout->addWidget(title);

    auto* tbGroup = new QGroupBox(tr("工具栏"), page);
    auto* tbLayout = new QVBoxLayout(tbGroup);
    _hideToolBarCheck = new QCheckBox(tr("隐藏工具栏"), tbGroup);
    _hideToolBarCheck->setChecked(current.hideToolBar);
    tbLayout->addWidget(_hideToolBarCheck);
    layout->addWidget(tbGroup);

    _tabHideCheck = new QCheckBox(tr("隐藏标签栏"), page);
    _tabHideCheck->setChecked(current.tabHide);
    layout->addWidget(_tabHideCheck);

    auto* behavior = new QGroupBox(tr("行为"), page);
    auto* behaviorLayout = new QVBoxLayout(behavior);

    _tabVerticalCheck = new QCheckBox(tr("垂直显示"), behavior);
    _tabVerticalCheck->setChecked(current.tabVertical);
    behaviorLayout->addWidget(_tabVerticalCheck);

    _tabLockCheck = new QCheckBox(tr("禁止拖拽调整标签顺序"), behavior);
    _tabLockCheck->setChecked(current.tabLock);
    behaviorLayout->addWidget(_tabLockCheck);

    _tabDoubleClickCloseCheck = new QCheckBox(tr("双击关闭标签"), behavior);
    _tabDoubleClickCloseCheck->setChecked(current.tabDoubleClickClose);
    behaviorLayout->addWidget(_tabDoubleClickCloseCheck);

    _tabQuitOnEmptyCheck = new QCheckBox(tr("关闭最后一个标签时退出"), behavior);
    _tabQuitOnEmptyCheck->setChecked(current.tabQuitOnEmpty);
    behaviorLayout->addWidget(_tabQuitOnEmptyCheck);

    auto* labelRow = new QHBoxLayout();
    labelRow->addWidget(new QLabel(tr("标签最大长度："), behavior));
    _tabCompactLabelLenSpin = new QSpinBox(behavior);
    _tabCompactLabelLenSpin->setRange(0, 257);
    _tabCompactLabelLenSpin->setValue(current.tabCompactLabelLen);
    _tabCompactLabelLenSpin->setToolTip(tr("0 表示不截断；超出长度时以省略号显示。"));
    labelRow->addWidget(_tabCompactLabelLenSpin);
    labelRow->addStretch();
    behaviorLayout->addLayout(labelRow);
    layout->addWidget(behavior);

    auto* look = new QGroupBox(tr("外观和体验"), page);
    auto* lookLayout = new QVBoxLayout(look);

    _tabReduceCheck = new QCheckBox(tr("缩小标签栏"), look);
    _tabReduceCheck->setChecked(current.tabReduce);
    lookLayout->addWidget(_tabReduceCheck);

    _tabInactiveColorCheck = new QCheckBox(tr("非当前标签变暗"), look);
    _tabInactiveColorCheck->setChecked(current.tabInactiveColor);
    lookLayout->addWidget(_tabInactiveColorCheck);

    _tabActiveTopBarCheck = new QCheckBox(tr("为当前标签绘制颜色条"), look);
    _tabActiveTopBarCheck->setChecked(current.tabActiveTopBar);
    lookLayout->addWidget(_tabActiveTopBarCheck);

    _tabShowCloseBtnCheck = new QCheckBox(tr("标签显示关闭按钮"), look);
    _tabShowCloseBtnCheck->setChecked(current.tabShowCloseButton);
    lookLayout->addWidget(_tabShowCloseBtnCheck);

    layout->addWidget(look);
    layout->addStretch();
    return page;
}

// ---------------- 页面 2: 代码编辑 ----------------
QWidget* PreferencesDialog::createPageCodeEdit(const AppPreferences& current) {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(10, 0, 10, 0);

    auto* title = new QLabel(tr("<h3>代码编辑</h3>"), page);
    layout->addWidget(title);

    auto* editGroup = new QGroupBox(tr("制表符与缩进"), page);
    auto* editForm = new QFormLayout(editGroup);

    _tabWidthSpin = new QSpinBox(editGroup);
    _tabWidthSpin->setRange(1, 16);
    _tabWidthSpin->setValue(current.tabWidth);
    editForm->addRow(tr("制表符宽度:"), _tabWidthSpin);

    _tabToSpacesCheck = new QCheckBox(tr("用空格替代制表符"), editGroup);
    _tabToSpacesCheck->setChecked(current.tabToSpaces);
    editForm->addRow("", _tabToSpacesCheck);

    _wordWrapCheck = new QCheckBox(tr("默认启用自动换行"), editGroup);
    _wordWrapCheck->setChecked(current.wordWrap);
    editForm->addRow("", _wordWrapCheck);
    layout->addWidget(editGroup);

    auto* dispGroup = new QGroupBox(tr("辅助显示与符号"), page);
    auto* dispLayout = new QVBoxLayout(dispGroup);

    _lineNumbersCheck = new QCheckBox(tr("显示行号列"), dispGroup);
    _lineNumbersCheck->setChecked(current.showLineNumbers);
    dispLayout->addWidget(_lineNumbersCheck);

    _foldMarginCheck = new QCheckBox(tr("显示代码折叠列"), dispGroup);
    _foldMarginCheck->setChecked(current.showFoldMargin);
    dispLayout->addWidget(_foldMarginCheck);

    _indentGuidesCheck = new QCheckBox(tr("显示缩进参考线"), dispGroup);
    _indentGuidesCheck->setChecked(current.showIndentGuides);
    dispLayout->addWidget(_indentGuidesCheck);

    _whitespaceCheck = new QCheckBox(tr("显示空格与制表符符号"), dispGroup);
    _whitespaceCheck->setChecked(current.showWhitespace);
    dispLayout->addWidget(_whitespaceCheck);

    _eolCheck = new QCheckBox(tr("显示换行符符号"), dispGroup);
    _eolCheck->setChecked(current.showEOL);
    dispLayout->addWidget(_eolCheck);

    layout->addWidget(dispGroup);
    layout->addStretch();
    return page;
}

// ---------------- 页面 3: 字体 ----------------
QWidget* PreferencesDialog::createPageFont(const AppPreferences& current) {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(10, 0, 10, 0);

    auto* title = new QLabel(tr("<h3>字体</h3>"), page);
    layout->addWidget(title);

    auto* formLayout = new QFormLayout();

    _fontCombo = new QComboBox(page);
    _fontCombo->setObjectName(QStringLiteral("editorFont"));
    _fontCombo->addItems(FontFamilies::all());
    selectFontFamily(_fontCombo, current.fontName);
    formLayout->addRow(tr("字体:"), _fontCombo);

    _fontSizeSpin = new QSpinBox(page);
    _fontSizeSpin->setRange(6, 48);
    _fontSizeSpin->setValue(current.fontSize);
    formLayout->addRow(tr("字体大小:"), _fontSizeSpin);

    _fontSmoothCheck = new QCheckBox(tr("启用字体平滑与亚像素抗锯齿"), page);
    _fontSmoothCheck->setChecked(current.fontSmooth);
    formLayout->addRow("", _fontSmoothCheck);

    layout->addLayout(formLayout);
    layout->addStretch();
    return page;
}

// ---------------- 页面 4: 配色方案 ----------------
QWidget* PreferencesDialog::createPageTheme(const AppPreferences& current) {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(10, 0, 10, 0);

    auto* title = new QLabel(tr("<h3>配色方案</h3>"), page);
    layout->addWidget(title);

    auto* formLayout = new QFormLayout();

    _themeCombo = new QComboBox(page);
    _themeCombo->addItem(tr("默认配色"), QStringLiteral("Default"));
    for (const ThemeInfo& theme :
         StyleManager::locateThemes(QApplication::applicationDirPath().toStdString())) {
        const QString name = QString::fromStdString(theme.name);
        _themeCombo->addItem(name, name);
    }
    QString storedTheme = current.themeName;
    if (storedTheme.endsWith(QStringLiteral(".xml"), Qt::CaseInsensitive))
        storedTheme.chop(4);
    int themeIndex = _themeCombo->findData(storedTheme);
    if (themeIndex < 0 && !storedTheme.isEmpty()) {
        // 配置里保存的主题已不存在时保留一项，点确定不会被首项覆盖。
        _themeCombo->addItem(storedTheme, storedTheme);
        themeIndex = _themeCombo->count() - 1;
    }
    if (themeIndex >= 0) _themeCombo->setCurrentIndex(themeIndex);
    formLayout->addRow(tr("主题:"), _themeCombo);

    layout->addLayout(formLayout);
    layout->addStretch();
    return page;
}

// ---------------- 页面 5: 新建文档 ----------------
QWidget* PreferencesDialog::createPageNewDoc(const AppPreferences& current) {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(10, 0, 10, 0);

    auto* title = new QLabel(tr("<h3>新建文档格式</h3>"), page);
    layout->addWidget(title);

    auto* formLayout = new QFormLayout();

    _defaultEOLCombo = new QComboBox(page);
    _defaultEOLCombo->addItems({"Unix (LF)", "Windows (CR LF)", "Macintosh (CR)"});
    selectTextValue(_defaultEOLCombo, current.defaultEOL);
    formLayout->addRow(tr("换行格式:"), _defaultEOLCombo);

    _defaultEncodingCombo = new QComboBox(page);
    _defaultEncodingCombo->addItems({"UTF-8", "UTF-8 BOM", "ANSI", "GB18030", "UTF-16LE", "UTF-16BE"});
    selectTextValue(_defaultEncodingCombo, current.defaultEncoding);
    formLayout->addRow(tr("默认编码:"), _defaultEncodingCombo);

    _defaultLanguageCombo = new QComboBox(page);
    _defaultLanguageCombo->addItem(tr("普通文本"), QStringLiteral("normal"));
    for (const std::string& language : StyleManager::instance().languageNames()) {
        const QString id = QString::fromStdString(language);
        if (id != QLatin1String("normal"))
            _defaultLanguageCombo->addItem(id, id);
    }
    selectLanguageValue(_defaultLanguageCombo, current.defaultLanguage);
    formLayout->addRow(tr("默认语言:"), _defaultLanguageCombo);

    layout->addLayout(formLayout);
    layout->addStretch();
    return page;
}

// ---------------- 页面 6: 自动补全 ----------------
QWidget* PreferencesDialog::createPageAutoCompletion(const AppPreferences& current) {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(10, 0, 10, 0);

    auto* title = new QLabel(tr("<h3>自动补全</h3>"), page);
    layout->addWidget(title);

    auto* group = new QGroupBox(tr("补全设置"), page);
    auto* groupLayout = new QVBoxLayout(group);

    _autoCompletionCheck = new QCheckBox(tr("输入时自动弹出补全候选"), group);
    _autoCompletionCheck->setChecked(current.autoCompletion);
    groupLayout->addWidget(_autoCompletionCheck);

    auto* formLayout = new QFormLayout();
    _autoCompMinCharsSpin = new QSpinBox(group);
    _autoCompMinCharsSpin->setRange(1, 9);
    _autoCompMinCharsSpin->setValue(current.autoCompletionMinChars);
    formLayout->addRow(tr("触发字符数:"), _autoCompMinCharsSpin);

    _autoCompTypeCombo = new QComboBox(group);
    _autoCompTypeCombo->addItems({
        tr("单词与关键字补全"),
        tr("仅单词补全")
    });
    const int typeIndex = (current.autoCompletionType >= 0 && current.autoCompletionType <= 1)
                        ? current.autoCompletionType : 0;
    _autoCompTypeCombo->setCurrentIndex(typeIndex);
    formLayout->addRow(tr("补全类型:"), _autoCompTypeCombo);

    groupLayout->addLayout(formLayout);

    _autoCompIgnoreNumCheck = new QCheckBox(tr("忽略数字"), group);
    _autoCompIgnoreNumCheck->setChecked(current.autoCompletionIgnoreNumbers);
    groupLayout->addWidget(_autoCompIgnoreNumCheck);

    layout->addWidget(group);
    layout->addStretch();

    connect(_autoCompletionCheck, &QCheckBox::toggled,
            _autoCompMinCharsSpin, &QSpinBox::setEnabled);
    connect(_autoCompletionCheck, &QCheckBox::toggled,
            _autoCompTypeCombo, &QComboBox::setEnabled);
    _autoCompMinCharsSpin->setEnabled(current.autoCompletion);
    _autoCompTypeCombo->setEnabled(current.autoCompletion);

    return page;
}

// ---------------- 页面 7: 高亮匹配 ----------------
QWidget* PreferencesDialog::createPageHighlight(const AppPreferences& current) {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(10, 0, 10, 0);

    auto* title = new QLabel(tr("<h3>高亮匹配</h3>"), page);
    layout->addWidget(title);

    auto* wordGroup = new QGroupBox(tr("选中词高亮"), page);
    auto* wordLayout = new QVBoxLayout(wordGroup);

    _hlMatchingWordsCheck = new QCheckBox(tr("标记所选文本的所有匹配项"), wordGroup);
    _hlMatchingWordsCheck->setChecked(current.highlightMatchingWords);
    wordLayout->addWidget(_hlMatchingWordsCheck);

    _hlMatchCaseCheck = new QCheckBox(tr("区分大小写"), wordGroup);
    _hlMatchCaseCheck->setChecked(current.highlightMatchCase);
    wordLayout->addWidget(_hlMatchCaseCheck);

    _hlMatchWholeWordCheck = new QCheckBox(tr("全词匹配"), wordGroup);
    _hlMatchWholeWordCheck->setChecked(current.highlightMatchWholeWord);
    wordLayout->addWidget(_hlMatchWholeWordCheck);

    layout->addWidget(wordGroup);

    auto* braceGroup = new QGroupBox(tr("符号高亮"), page);
    auto* braceLayout = new QVBoxLayout(braceGroup);

    _hlMatchingBracesCheck = new QCheckBox(tr("高亮匹配括号"), braceGroup);
    _hlMatchingBracesCheck->setChecked(current.highlightMatchingBraces);
    braceLayout->addWidget(_hlMatchingBracesCheck);

    layout->addWidget(braceGroup);

    auto* urlGroup = new QGroupBox(tr("URL 可点击链接"), page);
    auto* urlLayout = new QVBoxLayout(urlGroup);
    _urlClickableCheck = new QCheckBox(tr("自动识别 URL 并加下划线，按住 Ctrl 点击打开"), urlGroup);
    _urlClickableCheck->setChecked(current.urlClickable);
    urlLayout->addWidget(_urlClickableCheck);
    layout->addWidget(urlGroup);

    layout->addStretch();
    return page;
}

// ---------------- 页面 8: 会话与备份 ----------------
QWidget* PreferencesDialog::createPageSessionBackup(const AppPreferences& current) {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(10, 0, 10, 0);

    auto* title = new QLabel(tr("<h3>会话与备份</h3>"), page);
    layout->addWidget(title);

    auto* group = new QGroupBox(tr("会话快照与周期性备份"), page);
    auto* groupLayout = new QVBoxLayout(group);

    _rememberSessionCheck = new QCheckBox(tr("记住上次会话（下次启动恢复所有标签页）"), group);
    _rememberSessionCheck->setChecked(current.rememberSession);
    groupLayout->addWidget(_rememberSessionCheck);

    _snapshotBackupCheck = new QCheckBox(tr("启用会话快照与周期性备份（未保存的内容也能恢复）"), group);
    _snapshotBackupCheck->setChecked(current.snapshotBackup);
    groupLayout->addWidget(_snapshotBackupCheck);

    auto* sessionHint = new QLabel(
        tr("开启快照后退出时不再询问是否保存：未保存内容会定期写入备份目录，\n"
           "下次启动连同光标位置一起恢复。"), group);
    sessionHint->setWordWrap(true);
    groupLayout->addWidget(sessionHint);

    auto* intervalRow = new QHBoxLayout();
    intervalRow->addWidget(new QLabel(tr("备份间隔:"), group));
    _backupIntervalSpin = new QSpinBox(group);
    _backupIntervalSpin->setRange(1, 600);
    _backupIntervalSpin->setSuffix(tr(" 秒"));
    _backupIntervalSpin->setValue(current.backupIntervalSec);
    intervalRow->addWidget(_backupIntervalSpin);
    intervalRow->addStretch();
    groupLayout->addLayout(intervalRow);

    _backupIntervalSpin->setEnabled(current.snapshotBackup);
    connect(_snapshotBackupCheck, &QCheckBox::toggled,
            _backupIntervalSpin, &QSpinBox::setEnabled);

    layout->addWidget(group);
    layout->addStretch();
    return page;
}

void PreferencesDialog::onAccept() {
    if (_langCombo) _prefs.uiLanguage = _langCombo->currentData().toString();
    if (_darkModeCheck) _prefs.darkMode = _darkModeCheck->isChecked();
    if (!_accentColor.isEmpty()) _prefs.accentColor = _accentColor;
    if (_uiFontZhCombo) _prefs.uiFontZh = fontComboValue(_uiFontZhCombo);
    if (_uiFontEnCombo) _prefs.uiFontEn = fontComboValue(_uiFontEnCombo);

    if (_hideToolBarCheck) _prefs.hideToolBar = _hideToolBarCheck->isChecked();
    if (_tabHideCheck) _prefs.tabHide = _tabHideCheck->isChecked();
    if (_tabVerticalCheck) _prefs.tabVertical = _tabVerticalCheck->isChecked();
    if (_tabLockCheck) _prefs.tabLock = _tabLockCheck->isChecked();
    if (_tabDoubleClickCloseCheck) _prefs.tabDoubleClickClose = _tabDoubleClickCloseCheck->isChecked();
    if (_tabQuitOnEmptyCheck) _prefs.tabQuitOnEmpty = _tabQuitOnEmptyCheck->isChecked();
    if (_tabCompactLabelLenSpin) _prefs.tabCompactLabelLen = _tabCompactLabelLenSpin->value();

    if (_tabReduceCheck) _prefs.tabReduce = _tabReduceCheck->isChecked();
    if (_tabInactiveColorCheck) _prefs.tabInactiveColor = _tabInactiveColorCheck->isChecked();
    if (_tabActiveTopBarCheck) _prefs.tabActiveTopBar = _tabActiveTopBarCheck->isChecked();
    if (_tabShowCloseBtnCheck) _prefs.tabShowCloseButton = _tabShowCloseBtnCheck->isChecked();

    if (_tabWidthSpin) _prefs.tabWidth = _tabWidthSpin->value();
    if (_tabToSpacesCheck) _prefs.tabToSpaces = _tabToSpacesCheck->isChecked();
    if (_wordWrapCheck) _prefs.wordWrap = _wordWrapCheck->isChecked();
    if (_lineNumbersCheck) _prefs.showLineNumbers = _lineNumbersCheck->isChecked();
    if (_foldMarginCheck) _prefs.showFoldMargin = _foldMarginCheck->isChecked();
    if (_indentGuidesCheck) _prefs.showIndentGuides = _indentGuidesCheck->isChecked();
    if (_whitespaceCheck) _prefs.showWhitespace = _whitespaceCheck->isChecked();
    if (_eolCheck) _prefs.showEOL = _eolCheck->isChecked();

    if (_fontCombo) {
        const QString family = fontComboValue(_fontCombo);
        if (!family.isEmpty()) _prefs.fontName = family;
    }
    if (_fontSizeSpin) _prefs.fontSize = _fontSizeSpin->value();
    if (_fontSmoothCheck) _prefs.fontSmooth = _fontSmoothCheck->isChecked();

    if (_themeCombo) {
        const QString theme = _themeCombo->currentData().toString();
        if (!theme.isEmpty()) _prefs.themeName = theme;
    }

    if (_defaultEOLCombo) _prefs.defaultEOL = _defaultEOLCombo->currentText();
    if (_defaultEncodingCombo) _prefs.defaultEncoding = _defaultEncodingCombo->currentText();
    if (_defaultLanguageCombo) {
        const QString language = _defaultLanguageCombo->currentData().toString();
        if (!language.isEmpty()) _prefs.defaultLanguage = language;
    }

    if (_autoCompletionCheck) _prefs.autoCompletion = _autoCompletionCheck->isChecked();
    if (_autoCompMinCharsSpin) _prefs.autoCompletionMinChars = _autoCompMinCharsSpin->value();
    if (_autoCompTypeCombo) {
        const int type = _autoCompTypeCombo->currentIndex();
        if (type == 0 || type == 1) _prefs.autoCompletionType = type;
    }
    if (_autoCompIgnoreNumCheck)
        _prefs.autoCompletionIgnoreNumbers = _autoCompIgnoreNumCheck->isChecked();

    if (_hlMatchingWordsCheck) _prefs.highlightMatchingWords = _hlMatchingWordsCheck->isChecked();
    if (_hlMatchCaseCheck) _prefs.highlightMatchCase = _hlMatchCaseCheck->isChecked();
    if (_hlMatchWholeWordCheck) _prefs.highlightMatchWholeWord = _hlMatchWholeWordCheck->isChecked();
    if (_hlMatchingBracesCheck) _prefs.highlightMatchingBraces = _hlMatchingBracesCheck->isChecked();
    if (_urlClickableCheck) _prefs.urlClickable = _urlClickableCheck->isChecked();

    if (_rememberSessionCheck) _prefs.rememberSession = _rememberSessionCheck->isChecked();
    if (_snapshotBackupCheck) _prefs.snapshotBackup = _snapshotBackupCheck->isChecked();
    if (_backupIntervalSpin) _prefs.backupIntervalSec = _backupIntervalSpin->value();

    accept();
}

void PreferencesDialog::updateAccentPreview() {
    if (!_accentValueLabel) return;
    const QColor c(_accentColor);
    const QString fg = c.lightness() > 140 ? QStringLiteral("#222222") : QStringLiteral("#ffffff");
    _accentValueLabel->setText(QStringLiteral(" %1 ").arg(_accentColor.toUpper()));
    _accentValueLabel->setStyleSheet(QStringLiteral(
        "QLabel { background-color: %1; color: %2; border-radius: 3px;"
        " padding: 3px 8px; font-weight: bold; }").arg(c.name(), fg));
}

AppPreferences PreferencesDialog::getPreferences() const {
    return _prefs;
}
