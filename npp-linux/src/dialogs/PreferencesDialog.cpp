#include "PreferencesDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QTabWidget>
#include <QDialogButtonBox>
#include <QGroupBox>

PreferencesDialog::PreferencesDialog(const AppPreferences& current, QWidget* parent)
    : QDialog(parent)
    , _prefs(current)
{
    setWindowTitle("首选项 (Preferences)");
    setMinimumWidth(450);
    setupUI(current);
}

void PreferencesDialog::setupUI(const AppPreferences& current) {
    auto* mainLayout = new QVBoxLayout(this);
    auto* tabWidget = new QTabWidget(this);

    // Tab 1: 编辑器常规
    auto* pageEdit = new QWidget();
    auto* editLayout = new QFormLayout(pageEdit);

    _fontCombo = new QFontComboBox(this);
    int fontIdx = _fontCombo->findText(current.fontName);
    if (fontIdx >= 0) {
        _fontCombo->setCurrentIndex(fontIdx);
    } else {
        _fontCombo->setCurrentFont(QFont(current.fontName));
    }
    _fontSizeSpin = new QSpinBox(this);
    _fontSizeSpin->setRange(6, 48);
    _fontSizeSpin->setValue(current.fontSize);

    auto* fontRow = new QHBoxLayout();
    fontRow->addWidget(_fontCombo);
    fontRow->addWidget(_fontSizeSpin);
    editLayout->addRow("默认字体与字号:", fontRow);

    _tabWidthSpin = new QSpinBox(this);
    _tabWidthSpin->setRange(1, 16);
    _tabWidthSpin->setValue(current.tabWidth);
    editLayout->addRow("制表符宽度 (Tab Width):", _tabWidthSpin);

    _tabToSpacesCheck = new QCheckBox("将 Tab 替换为空格 (Expand Tabs)", this);
    _tabToSpacesCheck->setChecked(current.tabToSpaces);
    editLayout->addRow("", _tabToSpacesCheck);

    _wordWrapCheck = new QCheckBox("默认启用自动换行 (Word Wrap)", this);
    _wordWrapCheck->setChecked(current.wordWrap);
    editLayout->addRow("", _wordWrapCheck);

    tabWidget->addTab(pageEdit, "常规与编辑");

    // Tab 2: 界面与显示
    auto* pageView = new QWidget();
    auto* viewLayout = new QVBoxLayout(pageView);

    auto* groupDisplay = new QGroupBox("显示组件", pageView);
    auto* groupDispLayout = new QVBoxLayout(groupDisplay);

    _darkModeCheck = new QCheckBox("启用深色模式 (Dark Mode)", this);
    _darkModeCheck->setChecked(current.darkMode);
    groupDispLayout->addWidget(_darkModeCheck);

    _lineNumbersCheck = new QCheckBox("显示行号列 (Line Number Margin)", this);
    _lineNumbersCheck->setChecked(current.showLineNumbers);
    groupDispLayout->addWidget(_lineNumbersCheck);

    _foldMarginCheck = new QCheckBox("显示代码折叠列 (Folding Margin)", this);
    _foldMarginCheck->setChecked(current.showFoldMargin);
    groupDispLayout->addWidget(_foldMarginCheck);

    _indentGuidesCheck = new QCheckBox("显示缩进参考线 (Indentation Guides)", this);
    _indentGuidesCheck->setChecked(current.showIndentGuides);
    groupDispLayout->addWidget(_indentGuidesCheck);

    _whitespaceCheck = new QCheckBox("显示空格与制表符符号", this);
    _whitespaceCheck->setChecked(current.showWhitespace);
    groupDispLayout->addWidget(_whitespaceCheck);

    _eolCheck = new QCheckBox("显示换行符符号 (CR/LF)", this);
    _eolCheck->setChecked(current.showEOL);
    groupDispLayout->addWidget(_eolCheck);

    viewLayout->addWidget(groupDisplay);
    tabWidget->addTab(pageView, "界面与显示");

    // Tab 3: 新建文档默认值
    auto* pageNewDoc = new QWidget();
    auto* newDocLayout = new QFormLayout(pageNewDoc);

    _defaultEOLCombo = new QComboBox(this);
    _defaultEOLCombo->addItems({"Unix (LF)", "Windows (CR LF)", "Macintosh (CR)"});
    _defaultEOLCombo->setCurrentText(current.defaultEOL);
    newDocLayout->addRow("默认换行格式:", _defaultEOLCombo);

    _defaultEncodingCombo = new QComboBox(this);
    _defaultEncodingCombo->addItems({"UTF-8", "UTF-8 BOM", "ANSI", "GB18030", "UTF-16LE", "UTF-16BE"});
    _defaultEncodingCombo->setCurrentText(current.defaultEncoding);
    newDocLayout->addRow("默认文件编码:", _defaultEncodingCombo);

    tabWidget->addTab(pageNewDoc, "新建文档格式");

    mainLayout->addWidget(tabWidget);

    auto* btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(btnBox, &QDialogButtonBox::accepted, this, &PreferencesDialog::onAccept);
    connect(btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(btnBox);
}

void PreferencesDialog::onAccept() {
    QString chosenFont = _fontCombo->currentText();
    if (chosenFont.isEmpty()) {
        chosenFont = _fontCombo->currentFont().family();
    }
    _prefs.fontName = chosenFont;
    _prefs.fontSize = _fontSizeSpin->value();
    _prefs.tabWidth = _tabWidthSpin->value();
    _prefs.tabToSpaces = _tabToSpacesCheck->isChecked();
    _prefs.wordWrap = _wordWrapCheck->isChecked();
    _prefs.darkMode = _darkModeCheck->isChecked();
    _prefs.showLineNumbers = _lineNumbersCheck->isChecked();
    _prefs.showFoldMargin = _foldMarginCheck->isChecked();
    _prefs.showIndentGuides = _indentGuidesCheck->isChecked();
    _prefs.showWhitespace = _whitespaceCheck->isChecked();
    _prefs.showEOL = _eolCheck->isChecked();
    _prefs.defaultEOL = _defaultEOLCombo->currentText();
    _prefs.defaultEncoding = _defaultEncodingCombo->currentText();

    accept();
}

AppPreferences PreferencesDialog::getPreferences() const {
    return _prefs;
}
