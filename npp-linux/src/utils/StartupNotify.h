#pragma once

class QWidget;

// X11（EWMH/_NET_STARTUP_ID）启动通知补漏。
//
// 现象：从任务栏/应用列表点开程序后，图标上的转圈指示要等好几秒才消失，
// 但窗口其实 300ms 就出来了。
//
// 原因：桌面环境启动应用时会在子进程环境里放 DESKTOP_STARTUP_ID，并按规范期待
// 应用把它写进**自己每个顶层窗口**的 _NET_STARTUP_ID 属性；窗口管理器在窗口映射时
// 读到这个属性，就认为该启动序列已经完成，转圈随即结束。
// Qt6 的 xcb 平台层只把属性写在它内部那个 SM client leader 隐藏窗口上，并且会顺手
// 把环境变量消费掉（实测 LD_PRELOAD 拦 xcb_change_property：_NET_STARTUP_ID 落在
// leader 窗口 0x…08，真正的主窗口 0x…06 上始终没有），序列只能等到超时，
// 于是就有了一直转圈。这里补上缺失的一步。Wayland 会话下没有这套协议，直接跳过。
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

}
