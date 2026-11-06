#pragma once

#include <QDialog>
#include <QFontComboBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QSettings>

struct AppPreferences {
    QString fontName{"Monospace"};
    int fontSize{10};
    int tabWidth{4};
    bool tabToSpaces{false};
    bool wordWrap{false};
    bool darkMode{false};
    bool showLineNumbers{true};
    bool showFoldMargin{true};
    bool showIndentGuides{false};
    bool showWhitespace{false};
    bool showEOL{false};
    QString defaultEOL{"Unix (LF)"};
    QString defaultEncoding{"UTF-8"};

    // 会话快照：默认开启，行为对齐 Notepad++ 的“记住上次会话 + 启用会话快照与周期性备份”。
    bool rememberSession{true};
    bool snapshotBackup{true};
    int backupIntervalSec{7};
};

class PreferencesDialog : public QDialog {
    Q_OBJECT
public:
    explicit PreferencesDialog(const AppPreferences& current, QWidget* parent = nullptr);
    AppPreferences getPreferences() const;

private slots:
    void onAccept();

private:
    void setupUI(const AppPreferences& current);

    QFontComboBox* _fontCombo;
    QSpinBox* _fontSizeSpin;
    QSpinBox* _tabWidthSpin;
    QCheckBox* _tabToSpacesCheck;
    QCheckBox* _wordWrapCheck;
    QCheckBox* _darkModeCheck;
    QCheckBox* _lineNumbersCheck;
    QCheckBox* _foldMarginCheck;
    QCheckBox* _indentGuidesCheck;
    QCheckBox* _whitespaceCheck;
    QCheckBox* _eolCheck;
    QComboBox* _defaultEOLCombo;
    QComboBox* _defaultEncodingCombo;
    QCheckBox* _rememberSessionCheck;
    QCheckBox* _snapshotBackupCheck;
    QSpinBox* _backupIntervalSpin;

    AppPreferences _prefs;
};
