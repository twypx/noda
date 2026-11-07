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

// 属性只写一次：给后续窗口（对话框等）带上同一次启动的 id 反而会让窗口管理器
// 误认为它们也属于那次启动。
bool g_announced = false;

// 完成通知也只发一次：窗口后来隐藏再显示不该再动启动序列。
bool g_completed = false;

#ifdef NPP_HAVE_XCB
// 完成通知的事件里要填一个本进程的窗口，announce 时顺手记下来。
xcb_window_t g_mainWindow = XCB_NONE;

// 用一个数字 id 换原子名。失败返回 XCB_ATOM_NONE。
xcb_atom_t intern(xcb_connection_t* conn, const char* name) {
    xcb_atom_t out = XCB_ATOM_NONE;
    xcb_intern_atom_cookie_t ck =
        xcb_intern_atom(conn, 0, static_cast<uint16_t>(std::strlen(name)), name);
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
    // 依赖它会把构建搞成必须能找到 Qt 私有模块。X11 里任何客户端都能改任意窗口的属性，
    // 所以自己开一条短连接写完就断，反而更干净，也不会和 Qt 抢它自己的连接锁。
    // 实测这条连接上既能写属性（无 X 错误，xprop 可读到），也能发事件。
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
        xcb_void_cookie_t ck = xcb_change_property(conn, XCB_PROP_MODE_REPLACE,
                            static_cast<xcb_window_t>(id), propAtom, typeAtom, 8,
                            static_cast<uint32_t>(value.size()), value.constData());
        // 不检查这笔请求的错误是有意的：窗口此刻一定已经存在（winId() 里 Qt 已经建好并
        // flush 过，实测 xcb_request_check 返回无错误），属性晚到也不会造成错误行为，
        // 真正兜底的是 notifyComplete() 那条通道。
        std::free(xcb_request_check(conn, ck));
        g_mainWindow = static_cast<xcb_window_t>(id);
        g_announced = true;
    }

    xcb_flush(conn);
    xcb_disconnect(conn);
#endif
}

void StartupNotify::notifyComplete() {
#ifndef NPP_HAVE_XCB
    return;
#else
    if (g_completed || !g_announced || g_mainWindow == XCB_NONE) return;
    if (QGuiApplication::platformName() != QLatin1String("xcb")) return;
    g_completed = true;

    xcb_connection_t* conn = xcb_connect(nullptr, nullptr);
    if (!conn || xcb_connection_has_error(conn)) {
        if (conn) xcb_disconnect(conn);
        return;
    }

    const xcb_atom_t beginAtom = intern(conn, "_NET_STARTUP_INFO_BEGIN");
    const xcb_atom_t infoAtom = intern(conn, "_NET_STARTUP_INFO");
    if (beginAtom == XCB_ATOM_NONE || infoAtom == XCB_ATOM_NONE) {
        xcb_disconnect(conn);
        return;
    }

    xcb_window_t root = XCB_NONE;
    if (xcb_screen_t* screen = xcb_setup_roots_iterator(xcb_get_setup(conn)).data)
        root = screen->root;
    if (root == XCB_NONE) {
        xcb_disconnect(conn);
        return;
    }

    // 载荷：“remove: ID=\"<id>\"” 再加一个 NUL。引号不能省 —— 启动 id 中间那段是
    // .desktop 的 Name（我们的是 “Notepad++ (Linux)”，带空格），不带引号的写法会被解析器
    // 在空格处截断：Qt 自己发的那条就是不带引号的，实测启动器完全不认，序列还是挂到超时。
    // 带引号这一形状是用 gedit（207ms 就结束转圈）在线上抓到的。
    QByteArray payload = "remove: ID=\"";
    payload.append(g_startupId);
    payload.append("\"");
    payload.append('\0');

    // 一条 ClientMessage 的 data8 只有 20 字节，超出部分接着用 _NET_STARTUP_INFO 发，
    // 首条用 _NET_STARTUP_INFO_BEGIN 表示“新消息开始”（末尾不足 20 字节的部分补 NUL）。
    char raw[sizeof(xcb_client_message_event_t)];
    for (int off = 0; off < payload.size(); off += 20) {
        std::memset(raw, 0, sizeof raw);
        xcb_client_message_event_t* ev =
            reinterpret_cast<xcb_client_message_event_t*>(raw);
        ev->response_type = XCB_CLIENT_MESSAGE;
        ev->format = 8;
        ev->window = g_mainWindow;
        ev->type = (off == 0) ? beginAtom : infoAtom;
        const int n = qMin<int>(20, payload.size() - off);
        std::memcpy(ev->data.data8, payload.constData() + off, n);
        // 投递过滤位照 gedit：监听端（gnome-shell）在 root 上选的就是 PropertyChange。
        xcb_send_event(conn, 0, root, XCB_EVENT_MASK_PROPERTY_CHANGE, raw);
    }

    // 这里必须真做一次往返，光 xcb_flush() 再断开是不够的：本机实测（用 root 上的
    // 监听器对比“有往返/无往返”两轮）不带这一步时，四条 send_event 全部没能到达
    // 服务器，任务栏照样白等 15 秒超时。任何带应答的请求都能起到同样的作用。
    xcb_generic_error_t* err = nullptr;
    std::free(xcb_get_input_focus_reply(conn, xcb_get_input_focus(conn), &err));
    std::free(err);

    xcb_disconnect(conn);
#endif
}
