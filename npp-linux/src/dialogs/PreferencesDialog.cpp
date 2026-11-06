#include "PreferencesDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QTabWidget>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QLabel>

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

    // Tab 4: 会话与备份（对应 Notepad++ 的两个独立开关，默认都开）
    auto* pageSession = new QWidget();
    auto* sessionLayout = new QVBoxLayout(pageSession);

    _rememberSessionCheck = new QCheckBox("记住上次会话（下次启动恢复所有标签页）", this);
    _rememberSessionCheck->setChecked(current.rememberSession);
    sessionLayout->addWidget(_rememberSessionCheck);

    _snapshotBackupCheck = new QCheckBox("启用会话快照与周期性备份（未保存的内容也能恢复）", this);
    _snapshotBackupCheck->setChecked(current.snapshotBackup);
    sessionLayout->addWidget(_snapshotBackupCheck);

    auto* sessionHint = new QLabel(
        "开启快照后退出时不再询问是否保存：未保存内容会写进\n"
        "~/.config/Notepad++/backup/，下次启动连同光标位置一起恢复。", this);
    sessionHint->setWordWrap(true);
    sessionLayout->addWidget(sessionHint);

    auto* intervalRow = new QHBoxLayout();
    intervalRow->addWidget(new QLabel("备份间隔:", this));
    _backupIntervalSpin = new QSpinBox(this);
    _backupIntervalSpin->setRange(1, 600);
    _backupIntervalSpin->setSuffix(" 秒");
    _backupIntervalSpin->setValue(current.backupIntervalSec);
    intervalRow->addWidget(_backupIntervalSpin);
    intervalRow->addStretch();
    sessionLayout->addLayout(intervalRow);
    sessionLayout->addStretch();

    // 没开快照时备份间隔无意义，直接置灰，避免用户以为调它就能生效。
    _backupIntervalSpin->setEnabled(current.snapshotBackup);
    connect(_snapshotBackupCheck, &QCheckBox::toggled,
            _backupIntervalSpin, &QSpinBox::setEnabled);

    tabWidget->addTab(pageSession, "会话与备份");

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
    _prefs.rememberSession = _rememberSessionCheck->isChecked();
    _prefs.snapshotBackup = _snapshotBackupCheck->isChecked();
    _prefs.backupIntervalSec = _backupIntervalSpin->value();

    accept();
}

AppPreferences PreferencesDialog::getPreferences() const {
    return _prefs;
}
