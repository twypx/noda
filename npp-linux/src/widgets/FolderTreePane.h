#pragma once

#include <QDockWidget>
#include <QFileInfo>
#include <QHash>
#include <QIcon>
#include <QModelIndex>
#include <QString>
#include <QTreeView>
#include <QtGlobal>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QAbstractFileIconProvider>
using FolderTreeIconProviderBase = QAbstractFileIconProvider;
#else
#include <QFileIconProvider>
using FolderTreeIconProviderBase = QFileIconProvider;
#endif

class QFileSystemModel;
class QPainter;

// 自绘分支区的树视图：
//   - QSS 一旦接管 QTreeView::branch，Qt 就不再画默认的展开指示器，因此箭头与连接线全部自己画；
//   - 自绘缩进区的 hover/选中底色，避免只有文字那一截变色（看起来像节点被缩进了一格）；
//   - 行内容单击即发出 rowClicked，由 FolderTreePane 决定是展开目录还是打开文件。
class FolderTreeView : public QTreeView {
    Q_OBJECT
public:
    explicit FolderTreeView(QWidget* parent = nullptr);

    // 切换根目录后旧 hover 索引已失效，必须清掉，否则可能给新根里的同号行误上色。
    void resetHover();

signals:
    // 单击一行。目录的展开切换已在 mousePressEvent 里完成，这里只用于打开文件。
    void rowClicked(const QModelIndex& index);

protected:
    void drawBranches(QPainter* painter, const QRect& rect, const QModelIndex& index) const override;
    bool viewportEvent(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    // 悬停只重绘旧/新两行；整屏重绘在快速划过目录时会白白重画几十行。
    QRect fullRowRect(const QModelIndex& index) const;
    QModelIndex _hovered;
};

// 轻量图标提供者：目录/文件各用一个 QStyle 标准图标，并按文件后缀缓存。
// QFileSystemModel 默认的 QFileIconProvider 会对每个条目走 MIME/主题图标查询；
// 目录树只需要区分「目录 / 文件」，这里把逐项查询降为一次哈希命中。
class FolderTreeIconProvider : public FolderTreeIconProviderBase {
public:
    QIcon icon(IconType type) const override;
    QIcon icon(const QFileInfo& info) const override;

    // 已缓存的后缀数量，供回归测试验证缓存确实生效。
    int cacheSize() const { return _cache.size(); }

private:
    mutable QHash<QString, QIcon> _cache;
};

// 左侧文件夹树停靠窗。数据完全交给 QFileSystemModel：
//   - setRootPath() 只建立当前根的监视，子目录内容在用户展开时才 fetchMore；
//   - 不做任何递归遍历、不建全量索引、不自建线程；
//   - 文件激活通过信号交给 MainWindow 复用 openFile() 的编码/语言/监控链路。
class FolderTreePane : public QDockWidget {
    Q_OBJECT
public:
    explicit FolderTreePane(QWidget* parent = nullptr);
    ~FolderTreePane() override;

    // 失败（空路径 / 不存在 / 不是目录 / model 无法建立索引）时返回 false 且不改变当前根。
    bool setRootPath(const QString& path);
    QString rootPath() const { return _rootPath; }
    QModelIndex rootIndex() const;
    QFileSystemModel* model() const { return _model; }
    QTreeView* view() const { return _view; }

    // 从鼠标事件里抽出的激活逻辑：目录直接忽略，文件发出 fileActivated。
    void activateIndex(const QModelIndex& index);

signals:
    void fileActivated(const QString& absolutePath);

private:
    void onRowClicked(const QModelIndex& index);

    QFileSystemModel* _model{nullptr};
    FolderTreeView* _view{nullptr};
    FolderTreeIconProvider* _icons{nullptr};
    QString _rootPath;
};
