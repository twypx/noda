#pragma once

#include <QHash>
#include <QKeySequence>
#include <QList>
#include <QString>

class QWidget;

// 一条快捷键映射记录。key 是规范化后的动作文本（稳定标识），
// nativeOnly 表示 Scintilla 原生手势，只能展示、不能改键。
struct ShortcutEntry {
    QString key;
    QString category;
    QString name;
    QString description;
    QString shortcutText;   // 原生手势的显示文本（如 "Alt + 鼠标拖动"）
    QKeySequence defaultSeq;
    QKeySequence currentSeq;
    bool nativeOnly{false};
};

class ShortcutManager {
public:
    static ShortcutManager& instance();

    // 从真实 QAction 树收集，杜绝「表里写一套、代码里注册另一套」。
    QList<ShortcutEntry> collectActions(QWidget* root) const;
    QList<ShortcutEntry> fullList(QWidget* root) const;

    // 校验并把用户编辑结果写入 QSettings；冲突时返回 false 且不落盘。
    bool applyOverrides(const QList<ShortcutEntry>& entries, QString* conflict);

    // 首次调用记录默认键，之后把持久化覆盖套到 QAction 上。
    void applyTo(QWidget* root);

    void resetAll();

    static QString cleanActionText(const QString& text);
    static QList<ShortcutEntry> nativeEntries();

private:
    ShortcutManager() = default;
    QHash<QString, QKeySequence> _defaults;
};
