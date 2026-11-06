#include <QApplication>
#include "MainWindow.h"
#include "StyleManager.h"
#include <QDir>
#include <QIcon>
#include <iostream>

int main(int argc, char* argv[]) {
    // 强制禁用平台全局菜单，确保菜单栏稳定显示在主窗口内部 (防止 GNOME/Wayland 下菜单栏消失)
    QApplication::setAttribute(Qt::AA_DontUseNativeMenuBar);

    // 启用高清屏幕支持（针对 4K/2K HiDPI）
    QApplication app(argc, argv);
    app.setApplicationName("Notepad++");
    app.setOrganizationName("Notepad++");

    // 应用图标：由 notepad++.png 经 tools/gen_icons.py 加 iOS 风格处理（超椭圆切角 + 绿色渐变底）
    // 后生成多尺寸 PNG 并内嵌到可执行文件（qrc），
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

    // 载入 Notepad++ 语法与高亮样式配置 (多级智能探测)
    QString appDir = QApplication::applicationDirPath();
    QStringList searchPaths = {
        appDir + "/langs.model.xml",
        appDir + "/../../PowerEditor/src/langs.model.xml",
        appDir + "/../PowerEditor/src/langs.model.xml",
        "./PowerEditor/src/langs.model.xml",
        "./langs.model.xml"
    };

    QString langsPath;
    for (const auto& path : searchPaths) {
        if (QFile::exists(path)) {
            langsPath = path;
            break;
        }
    }

    QString stylersPath;
    QStringList stylerSearchPaths = {
        appDir + "/stylers.model.xml",
        appDir + "/../../PowerEditor/src/stylers.model.xml",
        appDir + "/../PowerEditor/src/stylers.model.xml",
        "./PowerEditor/src/stylers.model.xml",
        "./stylers.model.xml"
    };
    for (const auto& path : stylerSearchPaths) {
        if (QFile::exists(path)) {
            stylersPath = path;
            break;
        }
    }

    if (!langsPath.isEmpty() && !stylersPath.isEmpty()) {
        StyleManager::instance().loadConfigs(langsPath.toStdString(), stylersPath.toStdString());
    }

    MainWindow window;
    window.show();

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
