#pragma once

class QWidget;

// X11（EWMH + startup-notification）启动通知补漏。
//
// 现象：从任务栏/应用列表点开程序后，指针变成忙光标（转圈）一直不停，窗口其实 300ms
// 就出来了；转圈期间再点图标也打不开，要等转圈结束。实测这一圈是 15.6 秒 —— 正好是
// 启动序列的超时值，说明序列从来没被“完成”过。
//
// 为什么需要补：桌面环境启动应用时会在子进程环境里放 DESKTOP_STARTUP_ID，并建立一条
// “启动序列”。转圈的光标和 STARTING 状态都属于**启动器**（这里是 gnome-shell，它链了
// libstartup-notification），不属于窗口管理器。启动器在等应用侧的完成通知。
//
// Qt 其实自己发过一条完成通知，但它把 id 写成不带引号的 `ID=<id>`：我们的 id 是
// `gnome-shell/Notepad++ (Linux)/...`（中间那段取自 .desktop 的 Name，**带空格**），
// 解析器读到空格就截断，于是这条通知指向一个不存在的序列，等于没发。
// GTK 程序（对照实测 207ms 就结束转圈的 gedit）是带引号发的，所以没事。
//
// 完成有两条通道，这里两条都走（线上行为是用 build/snwatch.c 在 root 上抓 gedit 和
// gnome-shell 自己的消息逐字节对出来的，工具见 build/xtrace.c）：
//   1) 映射前把 DESKTOP_STARTUP_ID 写进主窗口的 _NET_STARTUP_ID 属性 —— 规范里这是
//      窗口与启动序列配对的依据；Qt6 只把它写在自己那个 SM client leader 隐藏窗口上
//      （20x20、未映射），主窗口上没有。注意本机实测：光把属性写对，转圈照样走满 15.6
//      秒，所以这一条只是补规范要求的状态，真正结束序列靠第 2 条。
//   2) 映射后向 root 发 _NET_STARTUP_INFO_BEGIN/_NET_STARTUP_INFO 的 format 8
//      ClientMessage，载荷 "remove: ID=\"<序列id>\""，每 20 字节一条 —— 带引号才能
//      容得下 id 里的空格。发完必须强制一次往返再断开连接（见 .cpp 里的说明）。
//      实测生效的样子：我们这条消息上线后 10~30ms，gnome-shell 自己的客户端就广播了
//      一条 remove（= 序列结束），忙光标随之停止。
//
// Wayland 会话下没有这套协议（那边用 XDG_ACTIVATION_TOKEN），全部直接跳过。
namespace StartupNotify {

// 必须在构造 QApplication 之前调用：Qt 的 xcb 平台层会消费 DESKTOP_STARTUP_ID，
// 实测 QApplication 建好之后再 qgetenv() 拿到的是空串，事后无从取回。
// 非 X11 会话 / 变量为空时什么都不做。
void captureStartupId();

// 在 window 的原生窗口上写 _NET_STARTUP_ID。必须在窗口**映射之前**调用：
// 窗口管理器是在窗口映射时去读这个属性的，属性晚于映射才写上的话等于没写。
// 所以这里靠 winId() 先把原生窗口建出来（只建不映射），再写属性。
// 可以重复调用：只有第一次真正写入，后续窗口不会带着过期的启动 id。
void announceBeforeShow(QWidget* window);

// 窗口 show() 之后调用，发 "remove" 通知告诉启动器序列已经完成（转圈到此为止）。
// 只在 X11、且这个进程确实带着启动 id 时做事，一个进程只发一次。
void notifyComplete();

}
