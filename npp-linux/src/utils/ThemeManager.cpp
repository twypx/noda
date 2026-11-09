#include "ThemeManager.h"
#include <QApplication>
#include <QStyleFactory>
#include <QPalette>

namespace {

// 目录树行的 hover / 选中底色：QSS 与自绘缩进区共用，改一处即可。
constexpr const char* kTreeHoverDark = "#333333";
constexpr const char* kTreeHoverLight = "#f0edf8";
constexpr const char* kTreeSelectedDark = "#3d3550";
constexpr const char* kTreeSelectedLight = "#ddd3f5";

bool s_darkTheme = false;

// QSS 的 image: 只认固定像素的 PNG，没有 DPR 概念。标签关闭按钮原来直接写死 x_16.png，
// 2x 屏上它被按逻辑 16px（= 物理 32px）绘制，等于把 16px 原图拉大一倍，看着就是糊的；
// 工具栏里同一枚 x 走的是 toolBarIcon() 的 QIcon + setDevicePixelRatio(2)，一直清晰，
// 两边不一致。
// gen_toolbar_icons.py 本来就同时产出 _16/_32 两套，这里按屏幕 DPR 选一张，
// 并把逻辑盒锁在 16px，这样 32px 源图正好 1:1 落在物理像素上。
QString closeButtonImage(bool isDark) {
    const QString dir = isDark ? QStringLiteral("dark") : QStringLiteral("light");
    const qreal dpr = qApp ? qApp->devicePixelRatio() : 1.0;
    if (dpr <= 1.01)
        return QStringLiteral("image: url(:/toolbar/%1/x_16.png);").arg(dir);
    return QStringLiteral(
        "image: url(:/toolbar/%1/x_32.png);\n"
        "                width: 16px;\n"
        "                height: 16px;").arg(dir);
}

// Dock 标题栏的关闭按钮同样用 lucide x。titlebar-close-icon 只接受 url()，
// 这里按屏幕 DPR 选 16/32 源图，2x 屏才不会发糊。
QString dockCloseIcon(bool isDark) {
    const QString dir = isDark ? QStringLiteral("dark") : QStringLiteral("light");
    const qreal dpr = qApp ? qApp->devicePixelRatio() : 1.0;
    const QString file = dpr <= 1.01 ? QStringLiteral("x_16.png") : QStringLiteral("x_32.png");
    return QStringLiteral("url(:/toolbar/%1/%2)").arg(dir, file);
}

// 样式表里带 DPR / 主题色的占位符在这里落地。必须在拿到 isDark 之后、setStyleSheet 之前做。
QString finishStyleSheet(const QString& css, bool isDark) {
    QString out = css;
    out.replace(QStringLiteral("@CLOSE_BUTTON_IMAGE@"), closeButtonImage(isDark));
    out.replace(QStringLiteral("@DOCK_CLOSE_ICON@"), dockCloseIcon(isDark));
    out.replace(QStringLiteral("@TREE_HOVER_COLOR@"),
                QLatin1String(isDark ? kTreeHoverDark : kTreeHoverLight));
    out.replace(QStringLiteral("@TREE_SELECTED_COLOR@"),
                QLatin1String(isDark ? kTreeSelectedDark : kTreeSelectedLight));
    return out;
}

}   // namespace

FolderTreeColors ThemeManager::folderTreeColors() {
    // drawBranches 每行都会取一次颜色；静态构造，避免逐行解析颜色字符串。
    static const FolderTreeColors dark{QColor(kTreeHoverDark), QColor(kTreeSelectedDark)};
    static const FolderTreeColors light{QColor(kTreeHoverLight), QColor(kTreeSelectedLight)};
    return s_darkTheme ? dark : light;
}

QString ThemeManager::getStyleSheet(bool isDark) {
    if (isDark) {
        // Obsidian Dark Mode (CDP Probed Design Tokens)
        // Background primary: #1C1C1C, secondary: #282828, border: #333333
        // Text normal: #dadada, muted: #b3b3b3, faint: #666666
        // Accent: #a882ff (Obsidian iconic purple)
        return finishStyleSheet(R"(
            /* 全局主窗口外边框 (叠在其他窗口上时可看清程序边界) */
            QMainWindow {
                background-color: #282828;
                border: 1px solid #4a4a4a;
            }

            /* 全局基底 */
            QWidget {
                background-color: #282828;
                color: #dadada;
                font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", "Ubuntu", Roboto, "Inter", sans-serif;
                font-size: 13px;
                selection-background-color: #7852ee;
                selection-color: #ffffff;
            }

            /* 菜单栏 (已嵌入标题栏同行左侧，因此背景透明、无下边线) */
            QMenuBar {
                background-color: transparent;
                color: #dadada;
                border: none;
                font-size: 12px;
            }
            QMenuBar::item {
                background: transparent;
                padding: 4px 8px;
                margin: 0px;
                border-radius: 3px;
            }
            QMenuBar::item:selected {
                background-color: rgba(255, 255, 255, 0.08);
                color: #ffffff;
            }

            /* 自绘标题栏 (无边框窗口顶部) */
            #TitleBar {
                background-color: #1e1e1e;
            }
            /* 标题栏中间不再绘制文件标题（只在任务栏悬停提示里出现），
               最左侧的 20px 应用图标由 TitleBar 直接 setPixmap，无需样式。 */
            #TitleBtn, #TitleCloseBtn {
                background: transparent;
                color: #b3b3b3;
                border: none;
                border-radius: 4px;
                min-width: 26px;
                max-width: 26px;
                min-height: 22px;
                max-height: 22px;
                font-size: 13px;
            }
            #TitleBtn:hover {
                background-color: rgba(255, 255, 255, 0.10);
                color: #ffffff;
            }
            #TitleCloseBtn:hover {
                background-color: #c42b1c;
                color: #ffffff;
            }

            /* 下拉菜单 */
            QMenu {
                background-color: #282828;
                color: #dadada;
                border: 1px solid #383838;
                border-radius: 6px;
                padding: 3px;
                font-size: 12px;
            }
            QMenu::item {
                padding: 4px 20px 4px 10px;
                border-radius: 3px;
            }
            QMenu::item:selected {
                background-color: #7852ee;
                color: #ffffff;
            }
            QMenu::separator {
                height: 1px;
                background-color: #383838;
                margin: 3px 6px;
            }

            /* 工具栏 (图标模式：按钮尺寸由 16px 图标 + 3/5px 内边距决定) */
            QToolBar {
                background-color: #282828;
                border-bottom: 1px solid #333333;
                spacing: 2px;
                padding: 1px 4px;
            }
            QToolButton {
                background-color: transparent;
                color: #b3b3b3;
                border: 1px solid transparent;
                border-radius: 3px;
                padding: 1px 5px;
                margin: 0px;
                font-size: 11px;
            }
            QToolButton:hover {
                background-color: rgba(255, 255, 255, 0.08);
                color: #ffffff;
                border: 1px solid #3f3f3f;
            }
            QToolButton:checked {
                /* 开关型按钮的持续状态必须单独写：样式表一旦接管了 QToolButton，
                   就不会再回退原生外观，缺了 :checked 时选中与否看起来完全一样。
                   放在 :hover 之后，保证鼠标经过时仍看得出它是开着的。 */
                background-color: rgba(168, 130, 255, 0.22);
                border: 1px solid #a882ff;
                color: #ffffff;
            }
            QToolButton:pressed {
                background-color: rgba(168, 130, 255, 0.2);
            }
            QToolBar::separator {
                width: 1px;
                background-color: #383838;
                margin: 4px 4px;
            }

            /* 标签页组件 (对齐 Notepad++，标签与内容区域零缝隙贴合) */
            QTabWidget::pane {
                border: 1px solid #383838;
                border-top: 1px solid #333333;
                top: -1px;
                margin: 0px;
                padding: 0px;
                background-color: #1C1C1C;
            }
            QTabBar {
                background-color: #282828;
                qproperty-drawBase: 0;
                margin: 0px;
                padding: 0px;
            }
            QTabBar::tab {
                background-color: transparent;
                color: #8c8c8c;
                border: 1px solid transparent;
                border-bottom: 1px solid #333333;
                padding: 3px 10px 3px 8px;
                margin-right: 1px;
                font-size: 12px;
            }
            QTabBar::tab:hover {
                background-color: rgba(255, 255, 255, 0.05);
                color: #dadada;
            }
            QTabBar::tab:selected {
                background-color: #1C1C1C;
                color: #dadada;
                border: 1px solid #333333;
                border-bottom: 1px solid #1C1C1C;
                border-top: 2px solid #a882ff;
                margin-bottom: -1px;
                padding-bottom: 4px;
                font-weight: bold;
            }
            QTabBar::close-button {
                /* 必须显式给 image：QSS 里一旦出现 close-button 规则，Qt 就接管这个
                   子控件的绘制，不再回退到平台默认的关闭图标，没有 image 时 X 整块空白。
                   具体引哪张 PNG 由 closeButtonImage() 按屏幕 DPR 填，写死 _16 会在 HiDPI 下发糊。 */
                @CLOSE_BUTTON_IMAGE@
                subcontrol-position: center right;
                padding: 2px;
            }
            QTabBar::close-button:hover {
                background-color: rgba(255, 255, 255, 0.12);
            }

            /* 状态栏 (底栏) */
            QStatusBar {
                background-color: #282828;
                color: #b3b3b3;
                border-top: 1px solid #333333;
                font-size: 12px;
                min-height: 24px;
            }
            QStatusBar QLabel {
                color: #b3b3b3;
                padding: 2px 8px;
            }

            /* 左侧文件夹树停靠窗 */
            QDockWidget {
                background-color: #282828;
                color: #d4d4d4;
                border: 0px;
                /* 与标签页关闭按钮同一枚 lucide x，不再回退到平台默认的灰色 X。 */
                titlebar-close-icon: @DOCK_CLOSE_ICON@;
            }
            QDockWidget::title {
                background-color: #2f2f2f;
                color: #cfcfcf;
                padding: 5px 8px;
                border-bottom: 1px solid #3a3a3a;
                text-align: left;
            }
            QTreeView {
                background-color: #252525;
                color: #d4d4d4;
                border: 0px;
                outline: 0;
                show-decoration-selected: 1;
            }
            /* 三种状态的 padding/margin/border 必须完全一致：只要 hover 态少写一项，
               Qt 就会按另一套内容矩形绘制，鼠标移上去文字会横向抖一下。 */
            QTreeView::item {
                padding: 2px 4px;
                margin: 0px;
                border: 0px;
                min-height: 20px;
            }
            QTreeView::item:hover {
                padding: 2px 4px;
                margin: 0px;
                border: 0px;
                background-color: @TREE_HOVER_COLOR@;
            }
            QTreeView::item:selected {
                padding: 2px 4px;
                margin: 0px;
                border: 0px;
                background-color: @TREE_SELECTED_COLOR@;
                color: #ffffff;
            }
            QTreeView::branch {
                background-color: transparent;
            }

            /* 滚动条 (Obsidian 纤细流线风格) */
            QScrollBar:vertical {
                background: transparent;
                width: 8px;
                margin: 0px;
            }
            QScrollBar::handle:vertical {
                background: #444444;
                min-height: 24px;
                border-radius: 4px;
            }
            QScrollBar::handle:vertical:hover {
                background: #666666;
            }
            QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
                height: 0px;
            }
            QScrollBar:horizontal {
                background: transparent;
                height: 8px;
                margin: 0px;
            }
            QScrollBar::handle:horizontal {
                background: #444444;
                min-width: 24px;
                border-radius: 4px;
            }
            QScrollBar::handle:horizontal:hover {
                background: #666666;
            }
            QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {
                width: 0px;
            }

            /* 输入控件与对话框 */
            QLineEdit, QSpinBox, QComboBox {
                background-color: #232323;
                color: #dadada;
                border: 1px solid #383838;
                border-radius: 4px;
                padding: 4px 8px;
            }
            QLineEdit:focus, QSpinBox:focus, QComboBox:focus {
                border: 1.5px solid #a882ff;
            }
            QPushButton {
                background-color: #333333;
                color: #dadada;
                border: 1px solid #444444;
                border-radius: 4px;
                padding: 5px 14px;
            }
            QPushButton:hover {
                background-color: #3f3f3f;
                border-color: #555555;
            }
            QPushButton:pressed {
                background-color: #2a2a2a;
            }
            QPushButton:default {
                background-color: #7852ee;
                color: #ffffff;
                border: none;
            }
            QPushButton:default:hover {
                background-color: #8b64f8;
            }
            QGroupBox {
                border: 1px solid #383838;
                border-radius: 6px;
                margin-top: 10px;
                padding-top: 12px;
                font-weight: bold;
            }
            QGroupBox::title {
                subcontrol-origin: margin;
                subcontrol-position: top left;
                padding: 0 6px;
                color: #a882ff;
            }
        )", isDark);
    } else {
        // Obsidian Light Mode (CDP Probed Design Tokens)
        // Background primary: #ffffff, secondary: #f6f6f6, border: #e4e4e4
        // Text normal: #222222, muted: #5c5c5c, faint: #ababab
        // Accent: #7852ee (Obsidian purple)
        return finishStyleSheet(R"(
            /* 全局主窗口外边框 (叠在其他窗口上时可看清程序边界) */
            QMainWindow {
                background-color: #f6f6f6;
                border: 1px solid #b0b0b0;
            }

            /* 全局基底 */
            QWidget {
                background-color: #f6f6f6;
                color: #222222;
                font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", "Ubuntu", Roboto, "Inter", sans-serif;
                font-size: 13px;
                selection-background-color: #ede9fe;
                selection-color: #222222;
            }

            /* 菜单栏 (已嵌入标题栏同行左侧，因此背景透明、无下边线) */
            QMenuBar {
                background-color: transparent;
                color: #222222;
                border: none;
                font-size: 12px;
            }
            QMenuBar::item {
                background: transparent;
                padding: 4px 8px;
                margin: 0px;
                border-radius: 3px;
            }
            QMenuBar::item:selected {
                background-color: rgba(0, 0, 0, 0.06);
                color: #222222;
            }

            /* 自绘标题栏 (无边框窗口顶部) */
            #TitleBar {
                background-color: #ececec;
            }
            /* 标题栏中间不再绘制文件标题（只在任务栏悬停提示里出现），
               最左侧的 20px 应用图标由 TitleBar 直接 setPixmap，无需样式。 */
            #TitleBtn, #TitleCloseBtn {
                background: transparent;
                color: #5c5c5c;
                border: none;
                border-radius: 4px;
                min-width: 26px;
                max-width: 26px;
                min-height: 22px;
                max-height: 22px;
                font-size: 13px;
            }
            #TitleBtn:hover {
                background-color: rgba(0, 0, 0, 0.08);
                color: #222222;
            }
            #TitleCloseBtn:hover {
                background-color: #e81123;
                color: #ffffff;
            }

            /* 下拉菜单 */
            QMenu {
                background-color: #ffffff;
                color: #222222;
                border: 1px solid #e4e4e4;
                border-radius: 6px;
                padding: 3px;
                font-size: 12px;
            }
            QMenu::item {
                padding: 4px 20px 4px 10px;
                border-radius: 3px;
            }
            QMenu::item:selected {
                background-color: #7852ee;
                color: #ffffff;
            }
            QMenu::separator {
                height: 1px;
                background-color: #e4e4e4;
                margin: 3px 6px;
            }

            /* 工具栏 (图标模式：按钮尺寸由 16px 图标 + 3/5px 内边距决定) */
            QToolBar {
                background-color: #f6f6f6;
                border-bottom: 1px solid #e4e4e4;
                spacing: 2px;
                padding: 1px 4px;
            }
            QToolButton {
                background-color: transparent;
                color: #5c5c5c;
                border: 1px solid transparent;
                border-radius: 3px;
                padding: 1px 5px;
                margin: 0px;
                font-size: 11px;
            }
            QToolButton:hover {
                background-color: rgba(0, 0, 0, 0.06);
                color: #222222;
                border: 1px solid #dadada;
            }
            QToolButton:checked {
                /* 同深色一套：没有 :checked 规则时，开关按钮看不出是开还是关。 */
                background-color: rgba(120, 82, 238, 0.14);
                border: 1px solid #7852ee;
                color: #1a1a1a;
            }
            QToolButton:pressed {
                background-color: rgba(120, 82, 238, 0.15);
            }
            QToolBar::separator {
                width: 1px;
                background-color: #e4e4e4;
                margin: 4px 4px;
            }

            /* 标签页组件 (对齐 Notepad++，标签与内容区域零缝隙贴合) */
            QTabWidget::pane {
                border: 1px solid #d4d4d4;
                border-top: 1px solid #e4e4e4;
                top: -1px;
                margin: 0px;
                padding: 0px;
                background-color: #ffffff;
            }
            QTabBar {
                background-color: #f6f6f6;
                qproperty-drawBase: 0;
                margin: 0px;
                padding: 0px;
            }
            QTabBar::tab {
                background-color: transparent;
                color: #5c5c5c;
                border: 1px solid transparent;
                border-bottom: 1px solid #e4e4e4;
                padding: 3px 10px 3px 8px;
                margin-right: 1px;
                font-size: 12px;
            }
            QTabBar::tab:hover {
                background-color: rgba(0, 0, 0, 0.05);
                color: #222222;
            }
            QTabBar::tab:selected {
                background-color: #ffffff;
                color: #222222;
                border: 1px solid #e4e4e4;
                border-bottom: 1px solid #ffffff;
                border-top: 2px solid #7852ee;
                margin-bottom: -1px;
                padding-bottom: 4px;
                font-weight: bold;
            }
            QTabBar::close-button {
                /* 同上：没有 image 的 close-button 规则会把 X 画成空白；
                   带 DPR 的图由 closeButtonImage() 填入。 */
                @CLOSE_BUTTON_IMAGE@
                subcontrol-position: center right;
                padding: 2px;
            }
            QTabBar::close-button:hover {
                background-color: rgba(0, 0, 0, 0.08);
            }

            /* 状态栏 (底栏) */
            QStatusBar {
                background-color: #f6f6f6;
                color: #5c5c5c;
                border-top: 1px solid #e4e4e4;
                font-size: 12px;
                min-height: 24px;
            }
            QStatusBar QLabel {
                color: #5c5c5c;
                padding: 2px 8px;
            }

            /* 左侧文件夹树停靠窗 */
            QDockWidget {
                background-color: #f6f6f6;
                color: #333333;
                border: 0px;
                /* 与标签页关闭按钮同一枚 lucide x，不再回退到平台默认的灰色 X。 */
                titlebar-close-icon: @DOCK_CLOSE_ICON@;
            }
            QDockWidget::title {
                background-color: #eeeeee;
                color: #444444;
                padding: 5px 8px;
                border-bottom: 1px solid #e0e0e0;
                text-align: left;
            }
            QTreeView {
                background-color: #ffffff;
                color: #333333;
                border: 0px;
                outline: 0;
                show-decoration-selected: 1;
            }
            /* 三种状态的 padding/margin/border 必须完全一致：只要 hover 态少写一项，
               Qt 就会按另一套内容矩形绘制，鼠标移上去文字会横向抖一下。 */
            QTreeView::item {
                padding: 2px 4px;
                margin: 0px;
                border: 0px;
                min-height: 20px;
            }
            QTreeView::item:hover {
                padding: 2px 4px;
                margin: 0px;
                border: 0px;
                background-color: @TREE_HOVER_COLOR@;
            }
            QTreeView::item:selected {
                padding: 2px 4px;
                margin: 0px;
                border: 0px;
                background-color: @TREE_SELECTED_COLOR@;
                color: #1f1f1f;
            }
            QTreeView::branch {
                background-color: transparent;
            }

            /* 滚动条 (Obsidian 纤细流线风格) */
            QScrollBar:vertical {
                background: transparent;
                width: 8px;
                margin: 0px;
            }
            QScrollBar::handle:vertical {
                background: #d4d4d4;
                min-height: 24px;
                border-radius: 4px;
            }
            QScrollBar::handle:vertical:hover {
                background: #b0b0b0;
            }
            QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
                height: 0px;
            }
            QScrollBar:horizontal {
                background: transparent;
                height: 8px;
                margin: 0px;
            }
            QScrollBar::handle:horizontal {
                background: #d4d4d4;
                min-width: 24px;
                border-radius: 4px;
            }
            QScrollBar::handle:horizontal:hover {
                background: #b0b0b0;
            }
            QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {
                width: 0px;
            }

            /* 输入控件与对话框 */
            QLineEdit, QSpinBox, QComboBox {
                background-color: #ffffff;
                color: #222222;
                border: 1px solid #e4e4e4;
                border-radius: 4px;
                padding: 4px 8px;
            }
            QLineEdit:focus, QSpinBox:focus, QComboBox:focus {
                border: 1.5px solid #7852ee;
            }
            QPushButton {
                background-color: #ffffff;
                color: #222222;
                border: 1px solid #dadada;
                border-radius: 4px;
                padding: 5px 14px;
            }
            QPushButton:hover {
                background-color: #f0f0f0;
                border-color: #bdbdbd;
            }
            QPushButton:pressed {
                background-color: #e4e4e4;
            }
            QPushButton:default {
                background-color: #7852ee;
                color: #ffffff;
                border: none;
            }
            QPushButton:default:hover {
                background-color: #6538e8;
            }
            QGroupBox {
                border: 1px solid #e4e4e4;
                border-radius: 6px;
                margin-top: 10px;
                padding-top: 12px;
                font-weight: bold;
            }
            QGroupBox::title {
                subcontrol-origin: margin;
                subcontrol-position: top left;
                padding: 0 6px;
                color: #7852ee;
            }
        )", isDark);
    }
}

void ThemeManager::applyTheme(bool isDark) {
    s_darkTheme = isDark;
    QApplication::setStyle(QStyleFactory::create("Fusion"));

    QPalette p;
    if (isDark) {
        // Obsidian Dark Palette
        p.setColor(QPalette::Window, QColor(0x28, 0x28, 0x28));
        p.setColor(QPalette::WindowText, QColor(0xda, 0xda, 0xda));
        p.setColor(QPalette::Base, QColor(0x1C, 0x1C, 0x1C));
        p.setColor(QPalette::AlternateBase, QColor(0x23, 0x23, 0x23));
        p.setColor(QPalette::ToolTipBase, QColor(0x1C, 0x1C, 0x1C));
        p.setColor(QPalette::ToolTipText, QColor(0xda, 0xda, 0xda));
        p.setColor(QPalette::Text, QColor(0xda, 0xda, 0xda));
        p.setColor(QPalette::Button, QColor(0x33, 0x33, 0x33));
        p.setColor(QPalette::ButtonText, QColor(0xda, 0xda, 0xda));
        p.setColor(QPalette::BrightText, QColor(0xfb, 0x46, 0x4c));
        p.setColor(QPalette::Link, QColor(0xa8, 0x82, 0xff));
        p.setColor(QPalette::Highlight, QColor(0x78, 0x52, 0xee));
        p.setColor(QPalette::HighlightedText, Qt::white);
    } else {
        // Obsidian Light Palette
        p.setColor(QPalette::Window, QColor(0xf6, 0xf6, 0xf6));
        p.setColor(QPalette::WindowText, QColor(0x22, 0x22, 0x22));
        p.setColor(QPalette::Base, QColor(0xff, 0xff, 0xff));
        p.setColor(QPalette::AlternateBase, QColor(0xfa, 0xfa, 0xfa));
        p.setColor(QPalette::ToolTipBase, QColor(0xff, 0xff, 0xff));
        p.setColor(QPalette::ToolTipText, QColor(0x22, 0x22, 0x22));
        p.setColor(QPalette::Text, QColor(0x22, 0x22, 0x22));
        p.setColor(QPalette::Button, QColor(0xff, 0xff, 0xff));
        p.setColor(QPalette::ButtonText, QColor(0x22, 0x22, 0x22));
        p.setColor(QPalette::BrightText, QColor(0xe9, 0x31, 0x47));
        p.setColor(QPalette::Link, QColor(0x78, 0x52, 0xee));
        p.setColor(QPalette::Highlight, QColor(0x78, 0x52, 0xee));
        p.setColor(QPalette::HighlightedText, Qt::white);
    }

    QApplication::setPalette(p);
    qApp->setStyleSheet(getStyleSheet(isDark));
}
