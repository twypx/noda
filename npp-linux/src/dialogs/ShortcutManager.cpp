#include "ShortcutManager.h"

#include <QAction>
#include <QMenu>
#include <QRegularExpression>
#include <QSettings>
#include <QWidget>

namespace {

const char* kGroup = "shortcuts";

QString cleanMenuTitle(const QString& title) {
    QString t = title;
    t.remove(QLatin1Char('&'));
    t.remove(QRegularExpression(QStringLiteral("\\([^)]*\\)")));
    return t.trimmed();
}

// QMenu::addAction 不会把 QAction 认作自己的子对象（parent 常是 MainWindow），
// 所以分类要靠「哪个菜单的 actions() 里有它」反查。
QHash<const QAction*, QString> ownerMenus(QWidget* root) {
    QHash<const QAction*, QString> owner;
    const auto menus = root->findChildren<QMenu*>();
    for (QMenu* menu : menus) {
        const QString title = cleanMenuTitle(menu->title());
        if (title.isEmpty()) continue;
        for (QAction* action : menu->actions()) {
            if (action && !owner.contains(action)) owner.insert(action, title);
        }
    }
    return owner;
}

} // namespace

ShortcutManager& ShortcutManager::instance() {
    static ShortcutManager s_instance;
    return s_instance;
}

QString ShortcutManager::cleanActionText(const QString& text) {
    QString t = text;
    // "另存为(&A)..." -> "另存为"：先去助记符括号，再去 & 与末尾省略号。
    t.remove(QRegularExpression(QStringLiteral("\\([^)]*\\)")));
    t.remove(QLatin1Char('&'));
    while (t.endsWith(QLatin1Char('.')) || t.endsWith(QChar(0x2026))) t.chop(1);
    return t.trimmed();
}

QList<ShortcutEntry> ShortcutManager::collectActions(QWidget* root) const {
    QList<ShortcutEntry> result;
    if (!root) return result;

    const QHash<const QAction*, QString> owner = ownerMenus(root);
    const auto actions = root->findChildren<QAction*>();
    for (QAction* action : actions) {
        // 子菜单入口不是命令；工具栏里还有一份同名镜像动作，必须过滤，
        // 否则同一组合键会被两个 QAction 抢注。
        if (!action || action->isSeparator() || action->menu()
            || (action->parent() && action->parent()->inherits("QToolBar"))) continue;
        const QString name = cleanActionText(action->text());
        if (name.isEmpty()) continue;

        ShortcutEntry entry;
        entry.key = name;
        entry.name = name;
        entry.category = owner.value(action, QStringLiteral("其它"));
        entry.currentSeq = action->shortcut();
        entry.defaultSeq = _defaults.value(name, action->shortcut());
        entry.description = entry.currentSeq.isEmpty()
            ? QStringLiteral("点击快捷键单元格录入组合键；留空表示不设快捷键。")
            : QStringLiteral("当前快捷键；点击单元格可修改，留空表示禁用。");
        result.append(entry);
    }
    return result;
}

QList<ShortcutEntry> ShortcutManager::nativeEntries() {
    QList<ShortcutEntry> result;
    auto add = [&result](const QString& name, const QString& keys, const QString& desc) {
        ShortcutEntry e;
        e.key = name;
        e.name = name;
        e.category = QStringLiteral("操作");
        e.description = desc;
        e.shortcutText = keys;
        e.nativeOnly = true;
        result.append(e);
    };
    add(QStringLiteral("列编辑模式"), QStringLiteral("Alt + 鼠标拖动"),
        QStringLiteral("Scintilla 原生矩形选区手势，不经过 QAction，不可改键"));
    add(QStringLiteral("列选区扩展"), QStringLiteral("Alt + Shift + 方向键"),
        QStringLiteral("Scintilla 原生矩形选区扩展，不经过 QAction，不可改键"));
    add(QStringLiteral("多重光标编辑"), QStringLiteral("Ctrl + 鼠标左键点击"),
        QStringLiteral("Scintilla 原生多光标手势，不经过 QAction，不可改键"));
    return result;
}

QList<ShortcutEntry> ShortcutManager::fullList(QWidget* root) const {
    QList<ShortcutEntry> result = collectActions(root);
    result.append(nativeEntries());
    return result;
}

bool ShortcutManager::applyOverrides(const QList<ShortcutEntry>& entries, QString* conflict) {
    if (conflict) conflict->clear();

    // 先做全量冲突检测，再写盘：任何一处冲突都不能留下半套配置。
    QHash<QString, QString> used;
    for (const auto& entry : entries) {
        if (entry.nativeOnly) continue;
        const QString text = entry.currentSeq.toString();
        if (text.isEmpty()) continue;
        if (used.contains(text)) {
            if (conflict) {
                *conflict = QStringLiteral("快捷键 %1 被同时分配给「%2」和「%3」。")
                                .arg(text, used.value(text), entry.name);
            }
            return false;
        }
        used.insert(text, entry.key);
    }

    QSettings settings;
    settings.beginGroup(QLatin1String(kGroup));
    for (const auto& entry : entries) {
        if (entry.nativeOnly) continue;
        if (entry.currentSeq == entry.defaultSeq)
            settings.remove(entry.key);   // 与默认一致就不必持久化
        else
            settings.setValue(entry.key, entry.currentSeq.toString());
    }
    settings.endGroup();
    settings.sync();
    return true;
}

void ShortcutManager::applyTo(QWidget* root) {
    if (!root) return;

    QSettings settings;
    settings.beginGroup(QLatin1String(kGroup));
    QHash<QString, QString> overrides;
    const QStringList keys = settings.allKeys();
    for (const QString& key : keys)
        overrides.insert(key, settings.value(key).toString());
    settings.endGroup();

    for (QAction* action : root->findChildren<QAction*>()) {
        if (!action || action->isSeparator() || action->menu()
            || (action->parent() && action->parent()->inherits("QToolBar"))) continue;
        const QString name = cleanActionText(action->text());
        if (name.isEmpty()) continue;
        // 第一次调用发生在菜单建好、尚未套用任何覆盖时，此时记下的就是默认键。
        if (!_defaults.contains(name)) _defaults.insert(name, action->shortcut());
        if (overrides.contains(name))
            action->setShortcut(QKeySequence(overrides.value(name)));
        else
            action->setShortcut(_defaults.value(name));
    }
}

void ShortcutManager::resetAll() {
    QSettings settings;
    settings.beginGroup(QLatin1String(kGroup));
    settings.remove(QString());
    settings.endGroup();
    settings.sync();
}
