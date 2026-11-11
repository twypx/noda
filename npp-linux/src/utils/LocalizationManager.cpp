#include "LocalizationManager.h"

#include "pugixml.hpp"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QEvent>
#include <QFile>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMetaType>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QSpinBox>
#include <QTableWidget>
#include <QTextEdit>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVariant>
#include <QWidget>
#include <cstring>

namespace {

constexpr int kOriginalTextRole = Qt::UserRole + 77;
const char* kSourcePropertyPrefix = "npp_l10n_source_";

bool hasChinese(const QString& text) {
    for (QChar ch : text) {
        if (ch.unicode() >= 0x4E00 && ch.unicode() <= 0x9FFF) return true;
    }
    return false;
}

// 中英文 nativeLang XML 结构一致，用「节点路径 + ID 属性 + 同级序号」做配对键。
QString nodeSignature(const pugi::xml_node& node) {
    QStringList parts;
    for (pugi::xml_node current = node; current; current = current.parent()) {
        QString part = QString::fromUtf8(current.name());
        for (const pugi::xml_attribute attr : current.attributes()) {
            const QString name = QString::fromUtf8(attr.name()).toLower();
            if (name == QStringLiteral("name") || name == QStringLiteral("version")
                || name == QStringLiteral("filename")) {
                continue;
            }
            if (name.contains(QStringLiteral("id"))) {
                part += QLatin1Char('@') + name + QLatin1Char('=')
                      + QString::fromUtf8(attr.value());
            }
        }
        int siblingIndex = 0;
        for (pugi::xml_node prev = current.previous_sibling(); prev;
             prev = prev.previous_sibling()) {
            if (std::strcmp(prev.name(), current.name()) == 0) ++siblingIndex;
        }
        part += QLatin1Char('#') + QString::number(siblingIndex);
        parts.prepend(part);
    }
    return parts.join(QLatin1Char('/'));
}

void collectNames(const pugi::xml_node& node, QHash<QString, QString>& out) {
    if (node.type() != pugi::node_element) return;
    const QString signature = nodeSignature(node);
    if (const pugi::xml_attribute name = node.attribute("name"))
        out.insert(signature, QString::fromUtf8(name.value()));
    if (const pugi::xml_attribute title = node.attribute("title"))
        out.insert(signature + QStringLiteral("@title"), QString::fromUtf8(title.value()));
    for (const pugi::xml_node child : node.children()) collectNames(child, out);
}

bool loadXml(const QString& path, QHash<QString, QString>& out) {
    pugi::xml_document doc;
    if (!doc.load_file(path.toUtf8().constData())) return false;
    collectNames(doc.document_element(), out);
    return !out.isEmpty();
}

// 本移植 Linux 专属文案（上游 nativeLang 未覆盖，或中文表述不同）。
const QHash<QString, QString>& extraDictionary() {
    static const QHash<QString, QString> map = {
        {QStringLiteral("首选项"), QStringLiteral("Preferences")},
        {QStringLiteral("简体中文"), QStringLiteral("Simplified Chinese")},
        {QStringLiteral("启用深色模式"), QStringLiteral("Enable dark mode")},
        {QStringLiteral("标签栏与工具栏"), QStringLiteral("Tab Bar && Toolbar")},
        {QStringLiteral("<h3>标签栏与工具栏</h3>"), QStringLiteral("<h3>Tab Bar && Toolbar</h3>")},
        {QStringLiteral("隐藏工具栏"), QStringLiteral("Hide toolbar")},
        {QStringLiteral("禁止拖拽调整标签顺序"), QStringLiteral("Lock tab order")},
        {QStringLiteral("0 表示不截断；超出长度时以省略号显示。"), QStringLiteral("0 disables truncation; longer labels end with an ellipsis.")},
        {QStringLiteral("外观和体验"), QStringLiteral("Look && feel")},
        {QStringLiteral("用空格替代制表符"), QStringLiteral("Replace tabs with spaces")},
        {QStringLiteral("默认启用自动换行"), QStringLiteral("Enable word wrap by default")},
        {QStringLiteral("显示行号列"), QStringLiteral("Show line number margin")},
        {QStringLiteral("显示代码折叠列"), QStringLiteral("Show folding margin")},
        {QStringLiteral("显示缩进参考线"), QStringLiteral("Show indentation guides")},
        {QStringLiteral("显示非打印字符"), QStringLiteral("Show Non-Printing Characters")},
        // 与 nativeLang 词典行为一致：英文原文先去掉助记符 '&amp;' 再入表。
        {QStringLiteral("显示控制字符和 Unicode 换行符"), QStringLiteral("Show Control Characters  Unicode EOL")},
        {QStringLiteral("显示换行符符号"), QStringLiteral("Show end-of-line symbols")},
        {QStringLiteral("启用字体平滑与亚像素抗锯齿"), QStringLiteral("Enable font smoothing and subpixel antialiasing")},
        {QStringLiteral("<h3>配色方案</h3>"), QStringLiteral("<h3>Theme</h3>")},
        {QStringLiteral("默认配色"), QStringLiteral("Default Theme")},
        {QStringLiteral("主题:"), QStringLiteral("Theme:")},
        {QStringLiteral("普通文本"), QStringLiteral("Plain Text")},
        {QStringLiteral("<h3>自动补全</h3>"), QStringLiteral("<h3>Auto-Completion</h3>")},
        {QStringLiteral("补全设置"), QStringLiteral("Completion Settings")},
        {QStringLiteral("输入时自动弹出补全候选"), QStringLiteral("Show completion candidates while typing")},
        {QStringLiteral("单词与关键字补全"), QStringLiteral("Word and keyword completion")},
        {QStringLiteral("仅单词补全"), QStringLiteral("Word completion only")},
        {QStringLiteral("<h3>高亮匹配</h3>"), QStringLiteral("<h3>Highlighting</h3>")},
        {QStringLiteral("标记所选文本的所有匹配项"), QStringLiteral("Highlight all occurrences of the selected text")},
        {QStringLiteral("区分大小写"), QStringLiteral("Match case")},
        {QStringLiteral("全词匹配"), QStringLiteral("Whole word only")},
        {QStringLiteral("忽略空白"), QStringLiteral("Ignore whitespace")},
        {QStringLiteral("忽略大小写"), QStringLiteral("Ignore case")},
        {QStringLiteral("符号高亮"), QStringLiteral("Brace Highlighting")},
        {QStringLiteral("高亮匹配括号"), QStringLiteral("Highlight matching braces")},
        {QStringLiteral("开启快照后退出时不再询问是否保存：未保存内容会定期写入备份目录，\n下次启动连同光标位置一起恢复。"),
         QStringLiteral("With snapshots enabled, unsaved content is periodically written to the backup folder;\nit is restored with the caret position on the next launch.")},
        {QStringLiteral("界面语言:"), QStringLiteral("UI Language:")},
        {QStringLiteral("主界面字体"), QStringLiteral("Interface Font")},
        // 外观页：主题色
        {QStringLiteral("主题色"), QStringLiteral("Accent Color")},
        {QStringLiteral("主题色将影响选中、链接、按钮高亮等元素。"),
         QStringLiteral("The accent color affects selection, links, button highlights and more.")},
        {QStringLiteral("自定义…"), QStringLiteral("Custom…")},
        {QStringLiteral("选择主题色"), QStringLiteral("Choose Accent Color")},
        {QStringLiteral("默认紫"), QStringLiteral("Default Purple")},
        {QStringLiteral("macOS 蓝"), QStringLiteral("macOS Blue")},
        {QStringLiteral("Ubuntu 橙"), QStringLiteral("Ubuntu Orange")},
        {QStringLiteral("GNOME 蓝"), QStringLiteral("GNOME Blue")},
        {QStringLiteral("翠绿"), QStringLiteral("Green")},
        {QStringLiteral("玫瑰红"), QStringLiteral("Rose")},
        {QStringLiteral("琥珀"), QStringLiteral("Amber")},
        {QStringLiteral("中文字体:"), QStringLiteral("CJK Font:")},
        {QStringLiteral("英文字体:"), QStringLiteral("Latin Font:")},
        {QStringLiteral("（系统默认）"), QStringLiteral("(System Default)")},

        {QStringLiteral("外观与行为"), QStringLiteral("Appearance && Behavior")},
        {QStringLiteral("外观"), QStringLiteral("Appearance")},
        {QStringLiteral("编辑器"), QStringLiteral("Editor")},
        {QStringLiteral("代码编辑"), QStringLiteral("Editing")},
        {QStringLiteral("字体"), QStringLiteral("Font")},
        {QStringLiteral("配色方案"), QStringLiteral("Theme")},
        {QStringLiteral("新建文档"), QStringLiteral("New Document")},
        {QStringLiteral("自动补全"), QStringLiteral("Auto-Completion")},
        {QStringLiteral("高亮匹配"), QStringLiteral("Highlighting")},
        {QStringLiteral("会话与备份"), QStringLiteral("Session && Backup")},
        {QStringLiteral("<h3>外观</h3>"), QStringLiteral("<h3>Appearance</h3>")},
        {QStringLiteral("<h3>代码编辑</h3>"), QStringLiteral("<h3>Editing</h3>")},
        {QStringLiteral("<h3>字体</h3>"), QStringLiteral("<h3>Font</h3>")},
        {QStringLiteral("<h3>新建文档格式</h3>"), QStringLiteral("<h3>New Document</h3>")},
        {QStringLiteral("<h3>会话与备份</h3>"), QStringLiteral("<h3>Session && Backup</h3>")},

        {QStringLiteral("工具栏"), QStringLiteral("Toolbar")},
        {QStringLiteral("隐藏标签栏"), QStringLiteral("Hide tab bar")},
        {QStringLiteral("行为"), QStringLiteral("Behavior")},
        {QStringLiteral("垂直显示"), QStringLiteral("Vertical tabs")},
        {QStringLiteral("双击关闭标签"), QStringLiteral("Double-click to close tab")},
        {QStringLiteral("关闭最后一个标签时退出"), QStringLiteral("Exit after closing the last tab")},
        {QStringLiteral("标签最大长度："), QStringLiteral("Max. tab label length:")},
        {QStringLiteral("缩小标签栏"), QStringLiteral("Reduce tab height")},
        {QStringLiteral("非当前标签变暗"), QStringLiteral("Dim inactive tabs")},
        {QStringLiteral("为当前标签绘制颜色条"), QStringLiteral("Draw a colored bar on the active tab")},
        {QStringLiteral("标签显示关闭按钮"), QStringLiteral("Show close button on tabs")},

        {QStringLiteral("制表符与缩进"), QStringLiteral("Tabs && Indentation")},
        {QStringLiteral("制表符宽度:"), QStringLiteral("Tab width:")},
        {QStringLiteral("辅助显示与符号"), QStringLiteral("Display && Symbols")},
        {QStringLiteral("显示空格与制表符符号"), QStringLiteral("Show spaces and tabs")},
        {QStringLiteral("字体:"), QStringLiteral("Font:")},
        {QStringLiteral("字体大小:"), QStringLiteral("Font size:")},

        {QStringLiteral("换行格式:"), QStringLiteral("Line ending:")},
        {QStringLiteral("默认编码:"), QStringLiteral("Default encoding:")},
        {QStringLiteral("默认语言:"), QStringLiteral("Default language:")},
        {QStringLiteral("触发字符数:"), QStringLiteral("Trigger after:")},
        {QStringLiteral("补全类型:"), QStringLiteral("Completion type:")},
        {QStringLiteral("忽略数字"), QStringLiteral("Ignore numbers")},
        {QStringLiteral("选中词高亮"), QStringLiteral("Smart Highlighting")},

        {QStringLiteral("会话快照与周期性备份"), QStringLiteral("Session Snapshot && Periodic Backup")},
        {QStringLiteral("记住上次会话（下次启动恢复所有标签页）"), QStringLiteral("Remember last session (restore all tabs on startup)")},
        {QStringLiteral("启用会话快照与周期性备份（未保存的内容也能恢复）"), QStringLiteral("Enable session snapshot and periodic backup")},
        {QStringLiteral("备份间隔:"), QStringLiteral("Backup interval:")},
        {QStringLiteral(" 秒"), QStringLiteral(" s")},

        {QStringLiteral("深色模式"), QStringLiteral("Dark Mode")},
        {QStringLiteral("纯文本"), QStringLiteral("Plain Text")},
        {QStringLiteral("转为 Windows 格式"), QStringLiteral("Convert to Windows Format")},
        {QStringLiteral("转为 Unix 格式"), QStringLiteral("Convert to Unix Format")},
        {QStringLiteral("转为 Macintosh 格式"), QStringLiteral("Convert to Macintosh Format")},
        {QStringLiteral("增加缩进"), QStringLiteral("Increase Indent")},
        {QStringLiteral("减少缩进"), QStringLiteral("Decrease Indent")},
        {QStringLiteral("快捷键映射器"), QStringLiteral("Shortcut Mapper")},
        {QStringLiteral("粗体"), QStringLiteral("Bold")},
        {QStringLiteral("斜体"), QStringLiteral("Italic")},
        {QStringLiteral("总词数:"), QStringLiteral("Words:")},

        {QStringLiteral("确定(&O)"), QStringLiteral("&OK")},
        {QStringLiteral("取消(&C)"), QStringLiteral("&Cancel")},
        {QStringLiteral("保存失败"), QStringLiteral("Save failed")},
        {QStringLiteral("部分文件保存失败"), QStringLiteral("Some files were not saved")},
        {QStringLiteral("未能写入 %1。\n\n原因：%2\n\n磁盘文件未被改动，文档内容仍在编辑器中；可在「编码」菜单换用其他编码后重试。"),
         QStringLiteral("Could not write %1.\n\nReason: %2\n\nThe file on disk was not changed and the document is still open. Try another encoding from the Encoding menu.")},
        {QStringLiteral("以下文档未保存成功（磁盘文件未被改动）：\n\n%1\n\n可在「编码」菜单换用其他编码后重试。"),
         QStringLiteral("The following documents were not saved (disk files unchanged):\n\n%1\n\nTry another encoding from the Encoding menu.")},

        {QStringLiteral("新建(&N)"), QStringLiteral("&New")},
        {QStringLiteral("打开(&O)..."), QStringLiteral("&Open...")},
        {QStringLiteral("打开文件夹(&D)..."), QStringLiteral("Open &Folder...")},
        {QStringLiteral("打开文件夹"), QStringLiteral("Open Folder")},
        {QStringLiteral("打印(&P)..."), QStringLiteral("&Print...")},
        {QStringLiteral("退出(&X)"), QStringLiteral("E&xit")},
        {QStringLiteral("撤销(&U)"), QStringLiteral("&Undo")},
        {QStringLiteral("恢复"), QStringLiteral("Redo")},
        {QStringLiteral("剪切(&T)"), QStringLiteral("Cu&t")},
        {QStringLiteral("复制"), QStringLiteral("Copy")},
        {QStringLiteral("粘贴(&P)"), QStringLiteral("&Paste")},
        {QStringLiteral("全选(&A)"), QStringLiteral("Select &All")},
        {QStringLiteral("查找(&F)..."), QStringLiteral("&Find...")},
        {QStringLiteral("替换(&R)..."), QStringLiteral("&Replace...")},
        {QStringLiteral("查找下一个(&N)"), QStringLiteral("Find &Next")},
        {QStringLiteral("查找上一个(&P)"), QStringLiteral("Find &Previous")},
        {QStringLiteral("定位/跳转行(&G)..."), QStringLiteral("&Go to Line...")},
        {QStringLiteral("全屏模式"), QStringLiteral("Full Screen")},
        {QStringLiteral("自动换行"), QStringLiteral("Word Wrap")},
        {QStringLiteral("显示所有字符"), QStringLiteral("Show All Characters")},
        {QStringLiteral("文件夹树(&T)"), QStringLiteral("Folder &Tree")},
        {QStringLiteral("首选项(&P)..."), QStringLiteral("&Preferences...")},
        {QStringLiteral("语言样式配置器(&S)..."), QStringLiteral("Style &Configurator...")},
        {QStringLiteral("快捷键映射器(&K)..."), QStringLiteral("Shortcut &Mapper...")},
        {QStringLiteral("关于 Noda(&A)..."), QStringLiteral("&About Noda...")},
        {QStringLiteral("关于 Noda"), QStringLiteral("About Noda")},

        {QStringLiteral("保存"), QStringLiteral("Save")},
        {QStringLiteral("另存为"), QStringLiteral("Save As")},
        {QStringLiteral("复制完整路径"), QStringLiteral("Copy Full Path")},
        {QStringLiteral("复制目录路径"), QStringLiteral("Copy Folder Path")},
        {QStringLiteral("从磁盘删除"), QStringLiteral("Delete from Disk")},
        {QStringLiteral("从磁盘重新载入"), QStringLiteral("Reload from Disk")},
        {QStringLiteral("只读"), QStringLiteral("Read-Only")},
        {QStringLiteral("移到回收站"), QStringLiteral("Move to Recycle Bin")},
        {QStringLiteral("重命名..."), QStringLiteral("Rename...")},
        {QStringLiteral("用默认程序打开"), QStringLiteral("Open with Default App")},
        {QStringLiteral("在文件管理器中打开"), QStringLiteral("Open Containing Folder")},
        {QStringLiteral("在终端中打开"), QStringLiteral("Open in Terminal")},
        {QStringLiteral("关闭"), QStringLiteral("Close")},
        {QStringLiteral("关闭多个标签页"), QStringLiteral("Close Multiple Tabs")},
        {QStringLiteral("关闭其他标签页"), QStringLiteral("Close Other Tabs")},
        {QStringLiteral("关闭左侧标签页"), QStringLiteral("Close Tabs to the Left")},
        {QStringLiteral("关闭右侧标签页"), QStringLiteral("Close Tabs to the Right")},
        {QStringLiteral("关闭所有未修改的标签页"), QStringLiteral("Close All Unchanged Tabs")},
        {QStringLiteral("复制到剪贴板"), QStringLiteral("Copy to Clipboard")},
        {QStringLiteral("移动标签页"), QStringLiteral("Move Tab")},
        {QStringLiteral("移到最前"), QStringLiteral("Move to Start")},
        {QStringLiteral("移到最后"), QStringLiteral("Move to End")},

        {QStringLiteral("文件名"), QStringLiteral("Name")},
        {QStringLiteral("功能名称"), QStringLiteral("Command")},
        {QStringLiteral("分类"), QStringLiteral("Category")},
        {QStringLiteral("快捷键"), QStringLiteral("Shortcut")},
        {QStringLiteral("当前快捷键；点击单元格可修改，留空表示禁用。"), QStringLiteral("Current shortcut; click a cell to edit, leave empty to disable.")},
        {QStringLiteral("语言样式配置器"), QStringLiteral("Style Configurator")},
        {QStringLiteral("摘要信息"), QStringLiteral("Document Summary")},
        {QStringLiteral("列编辑器"), QStringLiteral("Column Editor")},
        {QStringLiteral("快捷键冲突"), QStringLiteral("Shortcut Conflict")},
        {QStringLiteral("文件已被外部修改"), QStringLiteral("File Changed on Disk")},
        {QStringLiteral("当前文件为只读，无法格式化"), QStringLiteral("Current file is read-only; cannot format")},
        {QStringLiteral("所有文件 (*.*)"), QStringLiteral("All files (*.*)")},
        {QStringLiteral("文件夹"), QStringLiteral("Folder")},
        {QStringLiteral("是否保存对文件 %1 的更改？"), QStringLiteral("Save changes to %1?")},
        {QStringLiteral("是(&Y)"), QStringLiteral("&Yes")},
        {QStringLiteral("否(&N)"), QStringLiteral("&No")},

        // ---- 工具栏 / 菜单动作（图标模式下文字不可见，但 tooltip 会用到）----
        {QStringLiteral("新建"), QStringLiteral("New")},
        {QStringLiteral("打开"), QStringLiteral("Open")},
        {QStringLiteral("保存"), QStringLiteral("Save")},
        {QStringLiteral("全部保存"), QStringLiteral("Save All")},
        {QStringLiteral("关闭"), QStringLiteral("Close")},
        {QStringLiteral("全部关闭"), QStringLiteral("Close All")},
        {QStringLiteral("打印"), QStringLiteral("Print")},
        {QStringLiteral("剪切"), QStringLiteral("Cut")},
        {QStringLiteral("复制"), QStringLiteral("Copy")},
        {QStringLiteral("粘贴"), QStringLiteral("Paste")},
        {QStringLiteral("撤销"), QStringLiteral("Undo")},
        {QStringLiteral("恢复"), QStringLiteral("Redo")},
        {QStringLiteral("查找"), QStringLiteral("Find")},
        {QStringLiteral("替换"), QStringLiteral("Replace")},
        {QStringLiteral("代码格式化"), QStringLiteral("Format Code")},
        {QStringLiteral("放大"), QStringLiteral("Zoom In")},
        {QStringLiteral("缩小"), QStringLiteral("Zoom Out")},
        {QStringLiteral("以十六进制查看"), QStringLiteral("View as Hex")},
        {QStringLiteral("文件对比"), QStringLiteral("Compare Files")},
        {QStringLiteral("关闭对比"), QStringLiteral("Close Compare")},
        {QStringLiteral("下一个差异"), QStringLiteral("Next Difference")},
        {QStringLiteral("上一个差异"), QStringLiteral("Previous Difference")},
        {QStringLiteral("切换书签"), QStringLiteral("Toggle Bookmark")},
        {QStringLiteral("清除近期文件列表"), QStringLiteral("Clear Recent Files List")},
        {QStringLiteral("主工具栏"), QStringLiteral("Main toolbar")},

        // ---- 菜单动作（带助记符与无助记符两种形态）----
        {QStringLiteral("帮助(&H)"), QStringLiteral("&Help")},
        {QStringLiteral("帮助(H)"), QStringLiteral("Help")},
        {QStringLiteral("重新载入文件(&L)"), QStringLiteral("Re&load from Disk")},
        {QStringLiteral("重新载入文件(L)"), QStringLiteral("Reload from Disk")},
        {QStringLiteral("代码格式化(&F)"), QStringLiteral("&Format Code")},
        {QStringLiteral("代码格式化(F)"), QStringLiteral("Format Code")},
        {QStringLiteral("放大(&I)"), QStringLiteral("Zoom &In")},
        {QStringLiteral("放大(I)"), QStringLiteral("Zoom In")},
        {QStringLiteral("缩小(&O)"), QStringLiteral("Zoom &Out")},
        {QStringLiteral("缩小(O)"), QStringLiteral("Zoom Out")},
        {QStringLiteral("每词首字母大写(&P)"), QStringLiteral("Title &Case")},
        {QStringLiteral("每词首字母大写(P)"), QStringLiteral("Title Case")},
        {QStringLiteral("以十六进制查看(&H)"), QStringLiteral("View as &Hex")},
        {QStringLiteral("以十六进制查看(H)"), QStringLiteral("View as Hex")},
        {QStringLiteral("文件比对(&M)"), QStringLiteral("&Compare")}, // 视图下的比对子菜单标题
        {QStringLiteral("文件对比(&C)"), QStringLiteral("&Compare Files")},
        {QStringLiteral("文件对比(C)"), QStringLiteral("Compare Files")},
        {QStringLiteral("关闭对比(&X)"), QStringLiteral("Close Compare(&X)")},
        {QStringLiteral("关闭对比(X)"), QStringLiteral("Close Compare")},
        {QStringLiteral("下一个差异(&N)"), QStringLiteral("&Next Difference")},
        {QStringLiteral("下一个差异(N)"), QStringLiteral("Next Difference")},
        {QStringLiteral("上一个差异(&P)"), QStringLiteral("&Previous Difference")},
        {QStringLiteral("上一个差异(P)"), QStringLiteral("Previous Difference")},
        {QStringLiteral("比对选区(&S)"), QStringLiteral("&Compare Selection...")},
        {QStringLiteral("与 Git HEAD 比对(&G)"), QStringLiteral("Compare with &Git HEAD")},
        {QStringLiteral("与 SVN BASE 比对(&V)"), QStringLiteral("Compare with S&VN BASE")},
        // 工具栏可切换按钮的 tooltip：整串收录，避免 translateTooltip 只译主体、残留中文后缀。
        {QStringLiteral("文件对比 (Ctrl+Alt+C，再次点击退出)"),
         QStringLiteral("Compare Files (Ctrl+Alt+C, click again to exit)")},
        {QStringLiteral("选择文件比对(&F)"), QStringLiteral("Compare with &File...")},
        {QStringLiteral("选择要比对的文件"), QStringLiteral("Select File to Compare")},
        {QStringLiteral("所选文件过大，暂不支持比对（上限 16MB）"),
         QStringLiteral("Selected file is too large to compare (16MB limit)")},
        {QStringLiteral("无法读取所选文件"), QStringLiteral("Cannot read the selected file")},

        // ---- 比对状态栏（运行期拼接，模板与静态串都要收录）----
        {QStringLiteral("比对功能至少需要打开两个标签页"), QStringLiteral("Comparison requires at least two open tabs")},
        {QStringLiteral("选区比对至少需要打开两个标签页"), QStringLiteral("Selection comparison requires at least two open tabs")},
        {QStringLiteral("请先在任一侧选中一段文本，再执行选区比对"), QStringLiteral("Select text in at least one side before comparing selections")},
        {QStringLiteral("请先保存文件，再与版本库内容比对"), QStringLiteral("Save the file before comparing with version control")},
        {QStringLiteral("无法读取版本内容（命令不可用或文件不在版本库中）"), QStringLiteral("Cannot read version content (command unavailable or file not in the repository)")},
        {QStringLiteral("文件过大，暂不支持比对（单侧上限 16MB）"), QStringLiteral("File too large to compare (16MB limit per side)")},
        {QStringLiteral("文件完全一致，未发现差异"), QStringLiteral("Files are identical; no differences found")},
        {QStringLiteral("正在比对…"), QStringLiteral("Comparing…")},
        {QStringLiteral("比对完成：%1 处新增，%2 处删除，%3 处修改，%4 处移动 | 差异 %5/%6"),
         QStringLiteral("Compare done: %1 added, %2 removed, %3 modified, %4 moved | difference %5/%6")},
        {QStringLiteral("差异分布：点击跳转"), QStringLiteral("Difference map: click to navigate")},
        {QStringLiteral("用格式标记选中文本(&A)"), QStringLiteral("Mark &Selected Text")},
        {QStringLiteral("用格式标记选中文本(A)"), QStringLiteral("Mark Selected Text")},
        {QStringLiteral("展开层次(&U)"), QStringLiteral("Expand &Levels")},
        {QStringLiteral("展开层次(U)"), QStringLiteral("Expand Levels")},

        // ---- 行操作 / 编辑动作 ----
        {QStringLiteral("拆分行"), QStringLiteral("Split Lines")},
        {QStringLiteral("单行注释/取消注释"), QStringLiteral("Toggle Single-Line Comment")},
        {QStringLiteral("删除当前行"), QStringLiteral("Delete Current Line")},
        {QStringLiteral("向上移动当前行"), QStringLiteral("Move Current Line Up")},
        {QStringLiteral("向下移动当前行"), QStringLiteral("Move Current Line Down")},
        {QStringLiteral("升序排列文本行(忽略大小写)"), QStringLiteral("Sort Lines Ascending (Ignore Case)")},
        {QStringLiteral("降序排列文本行(忽略大小写)"), QStringLiteral("Sort Lines Descending (Ignore Case)")},
        {QStringLiteral("空格转 TAB"), QStringLiteral("Spaces to TAB")},
        {QStringLiteral("日期时间 (长)"), QStringLiteral("Date && Time (long)")},
        {QStringLiteral("日期时间 (短)"), QStringLiteral("Date && Time (short)")},
        {QStringLiteral("摘要信息 / 统计信息"), QStringLiteral("Summary / Statistics")},
        {QStringLiteral("摘要信息 / 统计信息..."), QStringLiteral("Summary / Statistics...")},

        // ---- 折叠层次（1~8）----
        {QStringLiteral("展开层次 1"), QStringLiteral("Expand Level 1")},
        {QStringLiteral("展开层次 2"), QStringLiteral("Expand Level 2")},
        {QStringLiteral("展开层次 3"), QStringLiteral("Expand Level 3")},
        {QStringLiteral("展开层次 4"), QStringLiteral("Expand Level 4")},
        {QStringLiteral("展开层次 5"), QStringLiteral("Expand Level 5")},
        {QStringLiteral("展开层次 6"), QStringLiteral("Expand Level 6")},
        {QStringLiteral("展开层次 7"), QStringLiteral("Expand Level 7")},
        {QStringLiteral("展开层次 8"), QStringLiteral("Expand Level 8")},
        {QStringLiteral("折叠层次 1"), QStringLiteral("Collapse Level 1")},
        {QStringLiteral("折叠层次 2"), QStringLiteral("Collapse Level 2")},
        {QStringLiteral("折叠层次 3"), QStringLiteral("Collapse Level 3")},
        {QStringLiteral("折叠层次 4"), QStringLiteral("Collapse Level 4")},
        {QStringLiteral("折叠层次 5"), QStringLiteral("Collapse Level 5")},
        {QStringLiteral("折叠层次 6"), QStringLiteral("Collapse Level 6")},
        {QStringLiteral("折叠层次 7"), QStringLiteral("Collapse Level 7")},
        {QStringLiteral("折叠层次 8"), QStringLiteral("Collapse Level 8")},

        // ---- 格式标记（1~5）----
        {QStringLiteral("使用格式 1"), QStringLiteral("Apply Style 1")},
        {QStringLiteral("使用格式 2"), QStringLiteral("Apply Style 2")},
        {QStringLiteral("使用格式 3"), QStringLiteral("Apply Style 3")},
        {QStringLiteral("使用格式 4"), QStringLiteral("Apply Style 4")},
        {QStringLiteral("使用格式 5"), QStringLiteral("Apply Style 5")},

        // ---- 编码 / 换行 ----
        {QStringLiteral("转为编码"), QStringLiteral("Convert to Encoding")},
        {QStringLiteral("字符集"), QStringLiteral("Character Set")},
        {QStringLiteral("西欧"), QStringLiteral("Western European")},
        {QStringLiteral("文档格式转换 (换行符)"), QStringLiteral("Document Format Conversion (Line Endings)")},
        {QStringLiteral("Batch (批处理)"), QStringLiteral("Batch")},

        // ---- 查找替换对话框 ----
        {QStringLiteral("查找与替换"), QStringLiteral("Find & Replace")},
        {QStringLiteral("查找目标:"), QStringLiteral("Find what:")},
        {QStringLiteral("替换为:"), QStringLiteral("Replace with:")},
        {QStringLiteral("正则表达式"), QStringLiteral("Regular expression")},

        // ---- 状态栏（模板，参数在运行期填充）----
        {QStringLiteral("行: %1  列: %2"), QStringLiteral("Line: %1  Col: %2")},
        {QStringLiteral("长度: %1  行数: %2"), QStringLiteral("Length: %1  Lines: %2")},

        // ---- URL 可点击链接（模块 L）----
        {QStringLiteral("URL 可点击链接"), QStringLiteral("URL Clickable Links")},
        {QStringLiteral("自动识别 URL 并加下划线，按住 Ctrl 点击打开"),
         QStringLiteral("Auto-detect URLs and underline them; Ctrl+Click to open")},
    };
    return map;
}

} // namespace

LocalizationManager::LocalizationManager(QObject* parent) : QObject(parent) {}

LocalizationManager& LocalizationManager::instance() {
    static LocalizationManager manager;
    return manager;
}

void LocalizationManager::initialize(QCoreApplication* app) {
    if (!_initialized && app) {
        _initialized = true;
        app->installEventFilter(this);
    }
    ensureLoaded();
}

void LocalizationManager::ensureLoaded() {
    if (_dictionariesLoaded) return;
    _dictionariesLoaded = true;
    loadDictionaries();
}

void LocalizationManager::loadDictionaries() {
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList dirs = {
        appDir + QStringLiteral("/nativeLang"),
        appDir + QStringLiteral("/../share/noda/nativeLang"),
        appDir + QStringLiteral("/../nativeLang"),
        QStringLiteral("./nativeLang"),
    };

    for (const QString& dir : dirs) {
        const QString zhPath = dir + QStringLiteral("/chineseSimplified.xml");
        const QString enPath = dir + QStringLiteral("/english.xml");
        if (!QFile::exists(zhPath) || !QFile::exists(enPath)) continue;

        QHash<QString, QString> zh;
        QHash<QString, QString> en;
        if (!loadXml(zhPath, zh) || !loadXml(enPath, en)) continue;

        for (auto it = zh.constBegin(); it != zh.constEnd(); ++it) {
            const auto english = en.constFind(it.key());
            if (english == en.constEnd()) continue;
            if (!hasChinese(it.value()) || hasChinese(english.value())) continue;
            _zhToEn.insert(it.value(), english.value());
            // 某些控件会用去掉 & 助记符的同一串文案。
            QString zhPlain = it.value();
            QString enPlain = english.value();
            zhPlain.remove(QLatin1Char('&'));
            enPlain.remove(QLatin1Char('&'));
            if (!zhPlain.isEmpty()) _zhToEn.insert(zhPlain, enPlain);
        }
        break;
    }

    _extra = extraDictionary();
}

QString LocalizationManager::translate(const QString& source) const {
    if (_language != QLatin1String("en_US") || source.isEmpty()) return source;
    const auto upstream = _zhToEn.constFind(source);
    if (upstream != _zhToEn.constEnd()) return upstream.value();
    const auto extra = _extra.constFind(source);
    if (extra != _extra.constEnd()) return extra.value();
    return source;
}

QString LocalizationManager::sourceText(QObject* object, const char* property,
                                        const QString& current) {
    const QByteArray key = QByteArray(kSourcePropertyPrefix) + property;
    const QVariant stored = object->property(key.constData());
    if (stored.isValid()) return stored.toString();
    if (!hasChinese(current)) return QString();
    object->setProperty(key.constData(), current);
    return current;
}

void LocalizationManager::translateObject(QObject* object) {
    if (!object) return;
    // 由运行期自行拼接的文本（状态栏等）不参与「记录原文 → 回滚」机制，
    // 否则带数字的整串会被当成原文，切语言后的动态刷新会被 applyToAll 撤回。
    if (object->property("nppNoL10n").toBool()) return;
    // 文档/输入框内容不能当界面文案翻译。
    if (object->inherits("QLineEdit") || object->inherits("QTextEdit")
        || object->inherits("QPlainTextEdit") || object->inherits("ScintillaEditBase")) {
        return;
    }

    static const QList<QByteArray> properties = {
        "text", "title", "windowTitle", "toolTip", "statusTip",
        "prefix", "suffix", "informativeText", "detailedText"
    };
    for (const QByteArray& prop : properties) {
        const QVariant value = object->property(prop.constData());
        if (!value.isValid() || value.userType() != QMetaType::QString) continue;
        const QString current = value.toString();
        const QString source = sourceText(object, prop.constData(), current);
        if (source.isEmpty()) continue;
        QString translated = translate(source);
        // tooltip 常被生成成 "名称 (Ctrl+S)" 或 "名称(X)"：整串查不到时翻译主体、保留后缀。
        if (translated == source && prop == QByteArray("toolTip")) {
            translated = translateTooltip(source);
        }
        if (translated != current) object->setProperty(prop.constData(), translated);
    }
}

QString LocalizationManager::translateTooltip(const QString& source) const {
    const int lp = source.lastIndexOf(QLatin1Char('('));
    if (lp <= 0 || !source.endsWith(QLatin1Char(')'))) return source;
    const bool spaced = source.at(lp - 1) == QLatin1Char(' ');
    const QString base = source.left(spaced ? lp - 1 : lp);
    if (base.isEmpty()) return source;
    const QString translatedBase = translate(base);
    if (translatedBase == base) return source;
    return translatedBase + source.mid(spaced ? lp - 1 : lp);
}

void LocalizationManager::translateTreeItem(QTreeWidgetItem* item) {
    if (!item) return;
    for (int column = 0; column < item->columnCount(); ++column) {
        const QVariant stored = item->data(column, kOriginalTextRole);
        QString source;
        if (stored.isValid()) {
            source = stored.toString();
        } else {
            source = item->text(column);
            if (!hasChinese(source)) continue;
            item->setData(column, kOriginalTextRole, source);
        }
        const QString translated = translate(source);
        if (translated != item->text(column)) item->setText(column, translated);
    }
    for (int i = 0; i < item->childCount(); ++i) translateTreeItem(item->child(i));
}

void LocalizationManager::translateComboBox(QComboBox* combo) {
    if (!combo) return;
    // 语言选择等“原生名称”下拉框不翻译：中文项应始终显示「简体中文」，英文项显示「English」。
    if (combo->property("nppNoL10n").toBool()) return;
    for (int i = 0; i < combo->count(); ++i) {
        const QString data = combo->itemData(i).toString();
        if (data == QLatin1String("zh_CN") || data == QLatin1String("en_US")) return;
    }
    for (int i = 0; i < combo->count(); ++i) {
        const QVariant stored = combo->itemData(i, kOriginalTextRole);
        QString source;
        if (stored.isValid()) {
            source = stored.toString();
        } else {
            source = combo->itemText(i);
            if (!hasChinese(source)) continue;
            combo->setItemData(i, source, kOriginalTextRole);
        }
        const QString translated = translate(source);
        if (translated != combo->itemText(i)) combo->setItemText(i, translated);
    }
}

void LocalizationManager::applyToWidget(QWidget* root) {
    if (!root) return;
    ensureLoaded();
    _knownWidgets.insert(root);

    translateObject(root);
    const QList<QObject*> children = root->findChildren<QObject*>();
    for (QObject* child : children) translateObject(child);

    for (QTreeWidget* tree : root->findChildren<QTreeWidget*>()) {
        for (int i = 0; i < tree->topLevelItemCount(); ++i) translateTreeItem(tree->topLevelItem(i));
    }
    for (QComboBox* combo : root->findChildren<QComboBox*>()) translateComboBox(combo);
    for (QTableWidget* table : root->findChildren<QTableWidget*>()) {
        for (int row = 0; row < table->rowCount(); ++row) {
            for (int col = 0; col < table->columnCount(); ++col) {
                QTableWidgetItem* item = table->item(row, col);
                if (!item) continue;
                QVariant stored = item->data(kOriginalTextRole);
                QString source;
                if (stored.isValid()) source = stored.toString();
                else {
                    source = item->text();
                    if (!hasChinese(source)) continue;
                    item->setData(kOriginalTextRole, source);
                }
                const QString translated = translate(source);
                if (translated != item->text()) item->setText(translated);
            }
        }
    }
}

void LocalizationManager::applyToAll() {
    ensureLoaded();
    const QWidgetList widgets = QApplication::allWidgets();
    for (QWidget* widget : widgets) applyToWidget(widget);
    // 清掉已销毁窗口的指针，避免集合无限增长。
    for (auto it = _knownWidgets.begin(); it != _knownWidgets.end();) {
        if (!*it) it = _knownWidgets.erase(it);
        else ++it;
    }
}

void LocalizationManager::setLanguage(const QString& language) {
    ensureLoaded();
    const QString normalized = (language == QLatin1String("en_US")) ? language
                                                                     : QStringLiteral("zh_CN");
    if (_language == normalized) return;
    _language = normalized;
    applyToAll();
    emit languageChanged();
}

bool LocalizationManager::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::Show && watched->isWidgetType()) {
        // 中文就是源码里的原文：既不需要翻译，也不必为每个新控件做递归反射、
        // 9 个属性的探测和动态属性写入。将来切到 en_US 时 setLanguage() 会走
        // applyToAll() 遍历所有现存控件，中文模式下新建的窗口不会被漏翻译。
        if (_language != QLatin1String("zh_CN"))
            applyToWidget(qobject_cast<QWidget*>(watched));
    } else if (event->type() == QEvent::LanguageChange) {
        applyToAll();
    }
    return QObject::eventFilter(watched, event);
}
