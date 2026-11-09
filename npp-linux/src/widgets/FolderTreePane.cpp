#include "FolderTreePane.h"

#include "utils/ThemeManager.h"

#include <QApplication>
#include <QDir>
#include <QEvent>
#include <QFileSystemModel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QStyle>
#include <QTreeView>
#include <QVarLengthArray>

FolderTreeView::FolderTreeView(QWidget* parent) : QTreeView(parent) {
    viewport()->setMouseTracking(true);
    // 展开切换由 mousePressEvent 统一负责；基类的双击展开会再切一次，必须关掉。
    setExpandsOnDoubleClick(false);
}

QRect FolderTreeView::fullRowRect(const QModelIndex& index) const {
    if (!index.isValid()) return QRect();
    const QRect row = visualRect(index);
    if (!row.isValid()) return QRect();
    return QRect(0, row.top(), viewport()->width(), row.height());
}

void FolderTreeView::resetHover() {
    if (!_hovered.isValid()) return;
    const QRect oldRect = fullRowRect(_hovered);
    _hovered = QModelIndex();
    if (oldRect.isValid()) viewport()->update(oldRect);
}

bool FolderTreeView::viewportEvent(QEvent* event) {
    // QAbstractItemView 给 viewport 开了 WA_Hover，鼠标移动收到的是 HoverMove 而不是 MouseMove，
    // 只处理后者会导致缩进区的 hover 底色永远不出现。
    if (event->type() == QEvent::HoverMove) {
        auto* hover = static_cast<QHoverEvent*>(event);
        const QModelIndex hovered = indexAt(hover->position().toPoint());
        if (hovered != _hovered) {
            // 只重绘旧行与新行。整屏 update() 会在快速划过时把几十行文本/图标全部重画一遍。
            const QRect oldRect = fullRowRect(_hovered);
            _hovered = hovered;
            if (oldRect.isValid()) viewport()->update(oldRect);
            const QRect newRect = fullRowRect(_hovered);
            if (newRect.isValid()) viewport()->update(newRect);
        }
    } else if (event->type() == QEvent::HoverLeave || event->type() == QEvent::Leave) {
        resetHover();
    }
    return QTreeView::viewportEvent(event);
}

void FolderTreeView::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        const QPoint pos = event->position().toPoint();
        const QModelIndex index = indexAt(pos);
        // 行内容区：按下即切换。双击的第二下会以 MouseButtonDblClick 形式到达，
        // 这里不会重复执行；箭头区（x < 行内容左边界）交给基类处理，避免切换两次。
        if (index.isValid() && model()->hasChildren(index)
            && pos.x() >= visualRect(index).left()) {
            setExpanded(index, !isExpanded(index));
        }
    }
    QTreeView::mousePressEvent(event);
}

void FolderTreeView::mouseReleaseEvent(QMouseEvent* event) {
    const QModelIndex index = indexAt(event->position().toPoint());
    QTreeView::mouseReleaseEvent(event);
    if (event->button() == Qt::LeftButton && index.isValid()) emit rowClicked(index);
}

void FolderTreeView::drawBranches(QPainter* painter, const QRect& rect,
                                  const QModelIndex& index) const {
    if (!model() || !index.isValid() || rect.isEmpty()) return;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    // QSS 的 ::item 背景只覆盖文字那一截，缩进区要在这里补齐同色，否则整行看起来是断开的。
    const FolderTreeColors colors = ThemeManager::folderTreeColors();
    const bool selected = selectionModel() && selectionModel()->isSelected(index);
    // 选中优先：QSS 里 ::item:selected 写在 ::item:hover 之后，选中行 hover 时仍是选中色。
    if (selected) {
        painter->fillRect(rect, colors.selected);
    } else if (_hovered.isValid() && _hovered == index) {
        painter->fillRect(rect, colors.hover);
    }

    const int indent = indentation();
    // 栈上前缀数组：常见深度（<32 层）零堆分配。ancestors[0] 是直接父级。
    QVarLengthArray<QModelIndex, 32> ancestors;
    const QModelIndex root = rootIndex();
    for (QModelIndex parent = index.parent(); parent.isValid() && parent != root;
         parent = parent.parent()) {
        ancestors.append(parent);
    }
    const int depth = ancestors.size();
    const int centerY = rect.top() + rect.height() / 2;
    const QColor textColor = palette().color(QPalette::Text);
    QColor lineColor = textColor;
    lineColor.setAlpha(60);
    QColor chevronColor = textColor;
    chevronColor.setAlpha(190);

    // 祖先层引导线：有后续兄弟就贯穿整行，否则只画到本行中线。
    QPen linePen(lineColor, 1.0);
    painter->setPen(linePen);
    for (int i = 0; i < depth; ++i) {
        const int level = depth - 1 - i;   // chain[i] 所在的层级
        const QModelIndex& ancestor = ancestors.at(i);
        const bool hasNextSibling =
            ancestor.row() + 1 < model()->rowCount(ancestor.parent());
        const int x = level * indent + indent / 2;
        const int bottom = (level == depth - 1 && !hasNextSibling) ? centerY : rect.bottom();
        painter->drawLine(x, rect.top(), x, bottom);
    }
    if (depth > 0) {
        const int parentX = (depth - 1) * indent + indent / 2;
        const int nodeX = depth * indent + indent / 2;
        painter->drawLine(parentX, centerY, nodeX, centerY);
    }

    // lucide 风格的 chevron：展开朝下、收起朝右。
    if (model()->hasChildren(index)) {
        const qreal cx = depth * indent + indent / 2.0;
        const qreal cy = centerY;
        const qreal half = 3.5;
        QPainterPath path;
        if (isExpanded(index)) {
            path.moveTo(cx - half, cy - half / 2.0);
            path.lineTo(cx, cy + half / 2.0);
            path.lineTo(cx + half, cy - half / 2.0);
        } else {
            path.moveTo(cx - half / 2.0, cy - half);
            path.lineTo(cx + half / 2.0, cy);
            path.lineTo(cx - half / 2.0, cy + half);
        }
        painter->setPen(QPen(chevronColor, 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(path);
    }

    painter->restore();
}

QIcon FolderTreeIconProvider::icon(IconType type) const {
    auto* style = QApplication::style();
    if (!style) return QIcon();
    if (type == Folder) return style->standardIcon(QStyle::SP_DirIcon);
    if (type == File) return style->standardIcon(QStyle::SP_FileIcon);
    return FolderTreeIconProviderBase::icon(type);
}

QIcon FolderTreeIconProvider::icon(const QFileInfo& info) const {
    if (info.isDir()) return icon(Folder);

    const QString key = info.suffix().toLower();
    auto it = _cache.constFind(key);
    if (it != _cache.constEnd()) return *it;

    const QIcon fileIcon = icon(File);
    _cache.insert(key, fileIcon);
    return fileIcon;
}

FolderTreePane::FolderTreePane(QWidget* parent)
    : QDockWidget(QStringLiteral("文件夹"), parent)
{
    setObjectName(QStringLiteral("folderTreePane"));
    setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable);

    // QFileSystemModel 不接管 iconProvider 的所有权，生命周期由本类显式管理。
    _icons = new FolderTreeIconProvider();
    _model = new QFileSystemModel(this);
    _model->setIconProvider(_icons);
    // 不显示隐藏文件：.git / node_modules 之外的隐藏目录往往是噪声，也会拖慢大目录展开。
    _model->setFilter(QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot);
    _model->setNameFilterDisables(false);

    _view = new FolderTreeView(this);
    _view->setModel(_model);
    _view->setHeaderHidden(true);
    // QFileSystemModel 有 4 列：Name / Size / Type / Date Modified，只保留 Name。
    for (int column = 1; column <= 3; ++column) _view->hideColumn(column);
    _view->setUniformRowHeights(true);   // 大目录滚动时跳过逐行高度计算
    _view->setSortingEnabled(true);
    _view->sortByColumn(0, Qt::AscendingOrder);
    _view->setSelectionMode(QAbstractItemView::SingleSelection);
    _view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    setWidget(_view);

    // 单击就处理：目录切换展开/收起，文件交给 activateIndex 打开。
    connect(_view, &FolderTreeView::rowClicked, this, &FolderTreePane::onRowClicked);
}

void FolderTreePane::onRowClicked(const QModelIndex& index) {
    if (!index.isValid() || !_model) return;
    // 目录的展开/收起已在 FolderTreeView::mousePressEvent 完成，这里只打开文件。
    if (_model->isDir(index)) return;
    activateIndex(index);
}

FolderTreePane::~FolderTreePane() {
    // 先让 view 松开 model，再销毁 model，最后销毁 provider；
    // 避免 QObject 子对象析构顺序导致 model 持有一个已销毁的 provider 指针。
    if (_view) _view->setModel(nullptr);
    delete _model;
    delete _icons;
    _model = nullptr;
    _icons = nullptr;
}

bool FolderTreePane::setRootPath(const QString& path) {
    if (path.isEmpty()) return false;
    const QFileInfo info(path);
    if (!info.exists() || !info.isDir()) return false;

    const QString absolute = info.absoluteFilePath();
    const QModelIndex index = _model->setRootPath(absolute);
    if (!index.isValid()) return false;

    _view->setRootIndex(index);
    // 旧根的 hover 索引已失效，先清掉，避免新根里同号行被误判为悬停。
    _view->resetHover();
    _rootPath = absolute;
    const QString title = info.fileName();
    setWindowTitle(title.isEmpty() ? absolute : title);
    return true;
}

QModelIndex FolderTreePane::rootIndex() const {
    return _view ? _view->rootIndex() : QModelIndex();
}

void FolderTreePane::activateIndex(const QModelIndex& index) {
    if (!index.isValid() || !_model || _model->isDir(index)) return;
    const QString path = _model->filePath(index);
    if (path.isEmpty()) return;
    emit fileActivated(path);
}
