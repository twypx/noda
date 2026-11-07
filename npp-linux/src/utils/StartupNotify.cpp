#include "StartupNotify.h"

#include <QByteArray>
#include <QGuiApplication>
#include <QWidget>

#include <cstdlib>
#include <cstring>

#ifdef NPP_HAVE_XCB
#include <xcb/xcb.h>
#endif

namespace {

// 在 QApplication 之前抢下的 DESKTOP_STARTUP_ID（Qt 会把它消费掉，不能事后重读）。
QByteArray g_startupId;

// 只在 X11 会话下做一次。写完之后这个启动 id 对这个进程就没用了，
// 再给后续窗口（对话框等）带上反而会让窗口管理器误认为它们也属于那次启动。
bool g_announced = false;

#ifdef NPP_HAVE_XCB
// 用一个数字 id 换原子名。失败返回 XCB_ATOM_NONE。
xcb_atom_t intern(xcb_connection_t* conn, const char* name) {
    xcb_atom_t out = XCB_ATOM_NONE;
    xcb_intern_atom_cookie_t ck =
        xcb_intern_atom(conn, 0, static_cast<uint8_t>(std::strlen(name)), name);
    xcb_generic_error_t* err = nullptr;
    if (xcb_intern_atom_reply_t* rp = xcb_intern_atom_reply(conn, ck, &err)) {
        out = rp->atom;
        std::free(rp);
    }
    std::free(err);
    return out;
}
#endif

}

void StartupNotify::captureStartupId() {
    if (g_startupId.isEmpty())
        g_startupId = qgetenv("DESKTOP_STARTUP_ID");
}

void StartupNotify::announceBeforeShow(QWidget* window) {
#ifndef NPP_HAVE_XCB
    Q_UNUSED(window)
#else
    if (g_announced || !window) return;
    // Wayland 下没有 _NET_STARTUP_ID；那边靠 app_id / desktopFileName 认应用。
    if (QGuiApplication::platformName() != QLatin1String("xcb")) return;

    // 不是从桌面入口启动的（命令行直接跑等），或者忘了在 QApplication 前抢存。
    if (g_startupId.isEmpty()) return;

    // winId() 会强制创建原生窗口但不会映射，正好满足“属性先于映射就位”。
    // X11 下 WId 就是 xcb_window_t。
    const WId id = window->winId();
    if (!id) return;

    // 不走 Qt 的 nativeProperty("xcb_connection")：那是 QtGui 的私有头（qpa/...），
    // 依赖它会把构建搞成必须能找到 Qt 私有模块。X11 里任何客户端都能改任意窗口的
    // 属性，所以自己开一条短连接写完就断，反而更干净，也不会和 Qt 抢它自己的连接锁。
    xcb_connection_t* conn = xcb_connect(nullptr, nullptr);
    if (!conn || xcb_connection_has_error(conn)) {
        if (conn) xcb_disconnect(conn);
        return;
    }

    const xcb_atom_t propAtom = intern(conn, "_NET_STARTUP_ID");
    xcb_atom_t typeAtom = intern(conn, "UTF8_STRING");
    if (typeAtom == XCB_ATOM_NONE) typeAtom = XCB_ATOM_STRING;

    if (propAtom != XCB_ATOM_NONE) {
        // 规范里这个属性是 format 8 的字符串，尾部带一个 NUL 更符合惯例（Qt 自己也是这么写的）。
        QByteArray value = g_startupId;
        value.append('\0');
        xcb_change_property(conn, XCB_PROP_MODE_REPLACE,
                            static_cast<xcb_window_t>(id), propAtom, typeAtom, 8,
                            static_cast<uint32_t>(value.size()), value.constData());
        xcb_flush(conn);
        g_announced = true;
    }

    xcb_disconnect(conn);
#endif
}
