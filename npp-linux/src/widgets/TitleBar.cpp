#include "TitleBar.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QIcon>
#include <QApplication>
#include <QMouseEvent>
#include <QEvent>
#include <QWindow>
#include <QPixmap>

namespace {
QToolButton* makeButton(const QString& glyph, const QString& objName, QWidget* parent) {
    auto* b = new QToolButton(parent);
    b->setText(glyph);
    b->setObjectName(objName);
    b->setFocusPolicy(Qt::NoFocus);
    b->setCursor(Qt::ArrowCursor);
    b->setToolButtonStyle(Qt::ToolButtonTextOnly);
    return b;
}
} // namespace

TitleBar::TitleBar(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("TitleBar");
    // 高度收紧到 28：菜单栏嵌入本行后，行高直接决定标题栏与工具栏之间的视觉间距。
    setFixedHeight(28);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(4, 0, 6, 0);
    layout->setSpacing(2);

    // 应用图标放在标题栏最左侧（菜单栏“文件”的左边），充当程序标识与边界提示。
    // 20px 是权衡后的尺寸：再小则 iOS 图标里的纸面/变色龙细节会糊成一团。
    _iconLabel = new QLabel(this);
    _iconLabel->setObjectName("TitleBarIcon");
    _iconLabel->setFixedSize(20, 20);
    _iconLabel->setScaledContents(true);
    _iconLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    QIcon icon = window() ? window()->windowIcon() : QIcon();
    if (icon.isNull()) icon = QApplication::windowIcon(); // 兼容只设了 QApplication 级图标的情况
    if (!icon.isNull()) {
        // 图标集内嵌了原生 20px（1x）与 40px（2x）两套：按 DPR 请求对应物理像素即可精确命中，
        // 不再从 24/48 缩放，边缘保持锐利。setScaledContents(true) 负责铺满 20x20 逻辑盒。
        const qreal dpr = _iconLabel->devicePixelRatio();
        QPixmap p = icon.pixmap(QSize(qRound(20 * dpr), qRound(20 * dpr)));
        p.setDevicePixelRatio(dpr);
        _iconLabel->setPixmap(p);
    }

    _minBtn   = makeButton(QStringLiteral("﹣"), "TitleBtn", this); // ﹣
    _maxBtn   = makeButton(QStringLiteral("□"), "TitleBtn", this); // □
    _closeBtn = makeButton(QStringLiteral("✕"), "TitleCloseBtn", this); // ✕

    layout->addWidget(_iconLabel);
    // 中间不再放标题，用弹性空白撑开：既把窗口按钮推到最右，
    // 也保证这片区域属于 TitleBar 自身，按住就能 startSystemMove 拖动窗口。
    layout->addStretch(1);
    layout->addWidget(_minBtn);
    layout->addWidget(_maxBtn);
    layout->addWidget(_closeBtn);

    connect(_minBtn, &QToolButton::clicked, this, [this]() {
        if (auto* w = window()) w->showMinimized();
    });
    connect(_maxBtn, &QToolButton::clicked, this, &TitleBar::toggleMaximize);
    connect(_closeBtn, &QToolButton::clicked, this, [this]() {
        if (auto* w = window()) w->close();
    });

    // 监听顶层窗口状态变化（最大化字形）。
    if (auto* tl = window()) {
        tl->installEventFilter(this);
    }
    refreshMaxGlyph();
}

void TitleBar::insertMenuWidget(QWidget* w) {
    if (auto* hl = qobject_cast<QHBoxLayout*>(layout())) {
        // 索引 0 固定是应用图标，菜单从索引 1 开始，保证图标始终在“文件”左侧。
        hl->insertWidget(1, w);
    }
}

void TitleBar::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        if (auto* wh = window()->windowHandle()) {
            wh->startSystemMove();
        }
    }
    QWidget::mousePressEvent(event);
}

void TitleBar::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        toggleMaximize();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void TitleBar::toggleMaximize() {
    auto* w = window();
    if (!w) return;
    if (w->isMaximized()) w->showNormal();
    else w->showMaximized();
}

void TitleBar::refreshMaxGlyph() {
    auto* w = window();
    // 最大化时显示“还原”字形，否则显示“最大化”字形。
    _maxBtn->setText(w && w->isMaximized() ? QStringLiteral("❏")   // ❏
                                           : QStringLiteral("□")); // □
}

bool TitleBar::eventFilter(QObject* watched, QEvent* event) {
    if (watched == window() && event->type() == QEvent::WindowStateChange) {
        refreshMaxGlyph();
    }
    return QWidget::eventFilter(watched, event);
}
