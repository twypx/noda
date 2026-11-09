#include <QApplication>
#include "MainWindow.h"
#include "StyleManager.h"
#include "utils/StartupNotify.h"
#include <QDir>
#include <QIcon>
#include <QMessageBox>
#include <QString>
#include <iostream>

int main(int argc, char* argv[]) {
    // 抢在 QApplication 之前把 DESKTOP_STARTUP_ID 读走：Qt 的 xcb 平台层
    // 会在构造时消费它（只写到自己那个隐藏 leader 窗口上），事后再读已是空串。
    StartupNotify::captureStartupId();

    // 强制禁用平台全局菜单，确保菜单栏稳定显示在主窗口内部 (防止 GNOME/Wayland 下菜单栏消失)
    QApplication::setAttribute(Qt::AA_DontUseNativeMenuBar);

    // 启用高清屏幕支持（针对 4K/2K HiDPI）
    QApplication app(argc, argv);
    app.setApplicationName("Notepad++");
    app.setOrganizationName("Notepad++");

    // 应用图标：由 notepad++_cutout.png 经 tools/gen_icons.py 生成透明背景多尺寸 PNG
    // 并内嵌到可执行文件（qrc），
    // 因此不依赖系统 QtSvg 开发包，也不会因为换机器运行而丢图标。
    QIcon appIcon;
    for (int size : {16, 24, 32, 48, 64, 128, 256})
        appIcon.addFile(QStringLiteral(":/app/icons/app_%1.png").arg(size), QSize(size, size));
    if (!appIcon.availableSizes().isEmpty()) {
        app.setWindowIcon(appIcon);       // 对所有顶层窗口（含对话框）生效
    }
    // Wayland/X11 任务栏靠 .desktop 文件识别应用；名字必须与安装的 notepad-plus-plus-linux.desktop 一致，
    // 否则 GNOME 会把窗口归到“匿名应用”而显示通用图标。
    app.setDesktopFileName(QStringLiteral("notepad-plus-plus-linux"));

    // 载入语法与高亮样式配置。路径探测统一交给 StyleManager，结果带完整诊断信息。
    // 缺文件 / 解析失败必须显式告知：旧实现跳过 loadConfigs 后静默启动，
    // 表现是「语言菜单和高亮全没了」却没有任何解释。
    const ConfigPaths configPaths = StyleManager::locateConfigFiles(
        QApplication::applicationDirPath().toStdString());
    ConfigLoadReport report;
    if (configPaths.complete()) {
        report = StyleManager::instance().loadConfigs(configPaths.langs, configPaths.stylers);
    } else {
        report.ok = false;
        if (configPaths.langs.empty()) report.errors.push_back("langs.model.xml 未找到");
        if (configPaths.stylers.empty()) report.errors.push_back("stylers.model.xml 未找到");
    }
    if (!report.ok) {
        const QString detail = QString::fromStdString(
            StyleManager::buildDiagnostic(configPaths, report));
        std::cerr << "[Notepad++ Linux] " << detail.toStdString() << std::endl;
        QMessageBox::critical(nullptr, QStringLiteral("语法配置载入失败"), detail);
    }

    MainWindow window;
    // X11 下必须赶在映射前把 _NET_STARTUP_ID 写到主窗口上（Qt 自己只写在了它的 SM leader 隐藏窗口上）。
    StartupNotify::announceBeforeShow(&window);
    window.show();
    // 映射后主动告诉启动器“这次启动好了”。不发这一步，任务栏/应用列表那里建的启动序列
    // 一直没人接，转圈要等到 15 秒超时才停（实测），而且转圈期间再点图标也开不了。
    StartupNotify::notifyComplete();

    // 如果命令行传入了文件路径，自动打开
    QStringList args = app.arguments();
    for (int i = 1; i < args.size(); ++i) {
        if (!args[i].startsWith("-")) {
            QFileInfo fi(args[i]);
            if (fi.exists()) {
                window.openFile(fi.absoluteFilePath());
            }
        }
    }

    return app.exec();
}
