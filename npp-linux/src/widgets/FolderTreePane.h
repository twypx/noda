#pragma once

#include <QDockWidget>
#include <QFileInfo>
#include <QHash>
#include <QIcon>
#include <QModelIndex>
#include <QString>
#include <QtGlobal>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QAbstractFileIconProvider>
using FolderTreeIconProviderBase = QAbstractFileIconProvider;
#else
#include <QFileIconProvider>
using FolderTreeIconProviderBase = QFileIconProvider;
#endif

class QFileSystemModel;
class QTreeView;

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
    QFileSystemModel* _model{nullptr};
    QTreeView* _view{nullptr};
    FolderTreeIconProvider* _icons{nullptr};
    QString _rootPath;
};
