#include "ToolbarIcon.h"

#include <QFile>
#include <QPixmap>

QIcon toolBarIcon(const QString& name, bool darkTheme) {
    const QString dir = darkTheme ? QStringLiteral("dark") : QStringLiteral("light");
    const QString base = QStringLiteral(":/toolbar/%1/%2").arg(dir, name);

    QIcon icon;
    // 常规 DPI：直接给 16x16 逻辑尺寸。
    const QString normal = base + QStringLiteral("_16.png");
    if (QFile::exists(normal)) {
        icon.addFile(normal, QSize(16, 16));
    }
    // HiDPI：32px 图按 DPR=2 注册，逻辑尺寸仍是 16，QIcon 会随窗口缩放自动择一。
    QPixmap hiDpi(base + QStringLiteral("_32.png"));
    if (!hiDpi.isNull()) {
        hiDpi.setDevicePixelRatio(2.0);
        icon.addPixmap(hiDpi);
    }
    return icon;
}
