#include "FolderTreePane.h"

#include <QApplication>
#include <QDir>
#include <QFileSystemModel>
#include <QStyle>
#include <QTreeView>

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

    _view = new QTreeView(this);
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

    connect(_view, &QTreeView::clicked, this, &FolderTreePane::activateIndex);
    connect(_view, &QTreeView::doubleClicked, this, &FolderTreePane::activateIndex);
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
