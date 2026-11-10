#include <QApplication>
#include "MainWindow.h"
#include "StyleManager.h"
#include "utils/StartupNotify.h"
#include "utils/SingleInstance.h"
#include "utils/LocalizationManager.h"
#include "utils/ConfigMigration.h"
#include <QDir>
#include <QIcon>
#include <QStandardPaths>
#include <QMessageBox>
#include <QString>
#include <iostream>

int main(int argc, char* argv[]) {
    // 抢在 QApplication 之前把 DESKTOP_STARTUP_ID 读走：Qt 的 xcb 平台层
    // 会在构造时消费它（只写到自己那个隐藏 leader 窗口上），事后再读已是空串。
    StartupNotify::captureStartupId();

    // 收集命令行参数与文件路径（在初始化繁重资源前快速提取）
    bool multiInst = false;
    QStringList filePaths;
    for (int i = 1; i < argc; ++i) {
        QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg == QStringLiteral("-multiInst") || arg == QStringLiteral("--multi-instance")) {
            multiInst = true;
        } else if (!arg.startsWith('-')) {
            QFileInfo fi(arg);
            filePaths.append(fi.absoluteFilePath());
        }
    }

    // 单实例快速通道：若已有实例在运行，直接把文件列表传递过去，本进程秒级退出。
    // 彻底消除右键打开文件时启动新进程导致的资源浪费与文件管理器投影缩影延迟。
    if (!multiInst && SingleInstance::trySendFilesToRunningInstance(filePaths)) {
        StartupNotify::notifyComplete();
        return 0;
    }

    // 强制禁用平台全局菜单，确保菜单栏稳定显示在主窗口内部 (防止 GNOME/Wayland 下菜单栏消失)
    QApplication::setAttribute(Qt::AA_DontUseNativeMenuBar);

    // 启用高清屏幕支持（针对 4K/2K HiDPI）
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Noda"));
    app.setOrganizationName(QStringLiteral("Noda"));

    // 改名后的一次性配置迁移：必须在任何 QSettings 读取（MainWindow 构造）之前。
    ConfigMigration::migrateLegacy(
        QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation));

    // 上游 nativeLang（中文/英文）资源 + 全局控件翻译过滤器。
    LocalizationManager::instance().initialize(&app);

    // 应用图标：由 tools/gen_icons.py 从设计稿生成透明背景多尺寸 PNG
    // 并内嵌到可执行文件（qrc），
    // 因此不依赖系统 QtSvg 开发包，也不会因为换机器运行而丢图标。
    QIcon appIcon;
    // 20/40 是为标题栏 1x / 2x 专门提供的原生尺寸：请求 20 或 40 时精确命中，
    // 不再由 24/48 缩放而来，避免左上角图标发糊。
    for (int size : {16, 20, 24, 32, 40, 48, 64, 128, 256})
        appIcon.addFile(QStringLiteral(":/app/icons/app_%1.png").arg(size), QSize(size, size));
    if (!appIcon.availableSizes().isEmpty()) {
        app.setWindowIcon(appIcon);       // 对所有顶层窗口（含对话框）生效
    }
    // Wayland/X11 任务栏靠 .desktop 文件识别应用；名字必须与安装的 noda.desktop 一致，
    // 否则 GNOME 会把窗口归到“匿名应用”而显示通用图标。
    app.setDesktopFileName(QStringLiteral("noda"));

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
        std::cerr << "[Noda] " << detail.toStdString() << std::endl;
        QMessageBox::critical(nullptr, QStringLiteral("语法配置载入失败"), detail);
    }

    MainWindow window;

    // 单实例监听：作为主实例接收后续启动进程转交的文件打开请求
    SingleInstance singleInstance;
    if (!multiInst) {
        singleInstance.listen();
        QObject::connect(&singleInstance, &SingleInstance::openFilesRequested,
                         &window, &MainWindow::openFilesFromIpc);
    }

    // 启动前先载入命令行传入的文件，避免先画空白窗口再载入导致的闪烁与投影撕裂
    for (const QString& path : filePaths) {
        if (QFile::exists(path)) {
            window.openFile(path);
        }
    }

    // X11 下必须赶在映射前把 _NET_STARTUP_ID 写到主窗口上（Qt 自己只写在了它的 SM leader 隐藏窗口上）。
    StartupNotify::announceBeforeShow(&window);
    window.show();
    // 映射后主动告诉启动器“这次启动好了”。
    StartupNotify::notifyComplete();

    return app.exec();
}
