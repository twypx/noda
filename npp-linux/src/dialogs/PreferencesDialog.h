#pragma once

#include <QDialog>
#include <QSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QStringList>
#include <QTreeWidget>
#include <QStackedWidget>

// 三个字体下拉共享同一份系统字体清单。QFontComboBox 每实例化一次都会重新枚举
// Fontconfig 全量字体并构建上千项模型，重复三次会拖慢首选项的弹出。
namespace FontFamilies {
const QStringList& all();   // 首次调用枚举一次，之后常驻内存
int buildCount();           // 实际枚举次数，供回归测试与诊断
void invalidate();          // 仅供测试/字体数据库热更新使用
}

struct AppPreferences {
    // 1. 外观与行为 -> 外观
    QString uiLanguage{"zh_CN"};             // "zh_CN" (简体中文), "en_US" (English)
    bool darkMode{false};                    // 亮色/暗色
    QString uiFontZh{""};                    // 主界面中文字体
    QString uiFontEn{""};                    // 主界面英文字体

    // 1. 外观与行为 -> 标签栏与工具栏
    bool hideToolBar{false};                 // 工具栏（上游 Toolbar 子页）

    // 标签栏
    bool tabHide{false};                     // 始终隐藏标签栏
    bool tabVertical{false};                 // 垂直标签
    bool tabLock{false};                     // 禁止拖拽
    bool tabDoubleClickClose{false};         // 双击关闭
    bool tabQuitOnEmpty{false};              // 关闭最后一个标签后退出
    int  tabCompactLabelLen{0};              // 0 = 不截断标签名

    bool tabReduce{false};                   // 紧凑标签
    bool tabInactiveColor{false};            // 非当前标签底色
    bool tabActiveTopBar{true};              // 当前标签颜色条
    bool tabShowCloseButton{true};           // 关闭按钮

    // 2. 编辑器 -> 代码编辑
    int tabWidth{4};                         // 制表符宽度
    bool tabToSpaces{false};                 // 将 Tab 替换为空格
    bool wordWrap{false};                    // 默认启用自动换行
    bool showLineNumbers{true};              // 显示行号列
    bool showFoldMargin{true};               // 显示代码折叠列
    bool showIndentGuides{false};            // 显示缩进参考线
    bool showWhitespace{false};              // 显示空格与制表符符号
    bool showEOL{false};                     // 显示换行符符号

    // 2. 编辑器 -> 字体
    QString fontName{"Monospace"};           // 字体
    int fontSize{10};                        // 字号
    bool fontSmooth{true};                   // 是否启用平滑字体

    // 2. 编辑器 -> 配色方案
    QString themeName{"Default"};            // 主题选择

    // 2. 编辑器 -> 新建文档
    QString defaultEOL{"Unix (LF)"};         // 默认换行格式
    QString defaultEncoding{"UTF-8"};        // 默认文件编码
    QString defaultLanguage{"Normal Text"};  // 默认高亮语言

    // 2. 编辑器 -> 自动补全
    bool autoCompletion{true};               // 是否启用自动补全
    int autoCompletionMinChars{2};           // 自动触发字符数 (1-9)
    int autoCompletionType{0};               // 0: 单词与关键字, 1: 仅单词
    bool autoCompletionIgnoreNumbers{true};  // 忽略数字

    // 2. 编辑器 -> 高亮匹配
    bool highlightMatchingWords{true};       // 高亮匹配单词
    bool highlightMatchingBraces{true};      // 高亮匹配括号
    bool highlightMatchCase{true};           // 区分大小写
    bool highlightMatchWholeWord{false};     // 全词匹配

    // 3. 会话与备份
    bool rememberSession{true};              // 记住上次会话
    bool snapshotBackup{true};               // 启用会话快照与周期性备份
    int backupIntervalSec{7};                // 备份间隔秒数
};

class PreferencesDialog : public QDialog {
    Q_OBJECT
public:
    explicit PreferencesDialog(const AppPreferences& current, QWidget* parent = nullptr);
    AppPreferences getPreferences() const;

public slots:
    void onAccept();

private slots:
    void onTreeItemChanged(QTreeWidgetItem* current, QTreeWidgetItem* previous);

private:
    void setupUI(const AppPreferences& current);

    // 页面构建辅助函数
    QWidget* createPageAppearance(const AppPreferences& current);
    QWidget* createPageTabBar(const AppPreferences& current);
    QWidget* createPageCodeEdit(const AppPreferences& current);
    QWidget* createPageFont(const AppPreferences& current);
    QWidget* createPageTheme(const AppPreferences& current);
    QWidget* createPageNewDoc(const AppPreferences& current);
    QWidget* createPageAutoCompletion(const AppPreferences& current);
    QWidget* createPageHighlight(const AppPreferences& current);
    QWidget* createPageSessionBackup(const AppPreferences& current);

    QTreeWidget* _treeWidget{nullptr};
    QStackedWidget* _stackedWidget{nullptr};

    // 外观
    QComboBox* _langCombo{nullptr};
    QCheckBox* _darkModeCheck{nullptr};
    QComboBox* _uiFontZhCombo{nullptr};
    QComboBox* _uiFontEnCombo{nullptr};

    // 标签栏与工具栏
    QCheckBox* _hideToolBarCheck{nullptr};

    QCheckBox* _tabHideCheck{nullptr};
    QCheckBox* _tabVerticalCheck{nullptr};
    QCheckBox* _tabLockCheck{nullptr};
    QCheckBox* _tabDoubleClickCloseCheck{nullptr};
    QCheckBox* _tabQuitOnEmptyCheck{nullptr};
    QSpinBox*  _tabCompactLabelLenSpin{nullptr};

    QCheckBox* _tabReduceCheck{nullptr};
    QCheckBox* _tabInactiveColorCheck{nullptr};
    QCheckBox* _tabActiveTopBarCheck{nullptr};
    QCheckBox* _tabShowCloseBtnCheck{nullptr};

    // 代码编辑
    QSpinBox* _tabWidthSpin{nullptr};
    QCheckBox* _tabToSpacesCheck{nullptr};
    QCheckBox* _wordWrapCheck{nullptr};
    QCheckBox* _lineNumbersCheck{nullptr};
    QCheckBox* _foldMarginCheck{nullptr};
    QCheckBox* _indentGuidesCheck{nullptr};
    QCheckBox* _whitespaceCheck{nullptr};
    QCheckBox* _eolCheck{nullptr};

    // 字体
    QComboBox* _fontCombo{nullptr};
    QSpinBox* _fontSizeSpin{nullptr};
    QCheckBox* _fontSmoothCheck{nullptr};

    // 配色方案
    QComboBox* _themeCombo{nullptr};

    // 新建文档
    QComboBox* _defaultEOLCombo{nullptr};
    QComboBox* _defaultEncodingCombo{nullptr};
    QComboBox* _defaultLanguageCombo{nullptr};

    // 自动补全
    QCheckBox* _autoCompletionCheck{nullptr};
    QSpinBox* _autoCompMinCharsSpin{nullptr};
    QComboBox* _autoCompTypeCombo{nullptr};
    QCheckBox* _autoCompIgnoreNumCheck{nullptr};

    // 高亮匹配
    QCheckBox* _hlMatchingWordsCheck{nullptr};
    QCheckBox* _hlMatchingBracesCheck{nullptr};
    QCheckBox* _hlMatchCaseCheck{nullptr};
    QCheckBox* _hlMatchWholeWordCheck{nullptr};

    // 会话与备份
    QCheckBox* _rememberSessionCheck{nullptr};
    QCheckBox* _snapshotBackupCheck{nullptr};
    QSpinBox* _backupIntervalSpin{nullptr};

    AppPreferences _prefs;
};
