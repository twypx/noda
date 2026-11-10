#pragma once

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>

class QComboBox;
class QCoreApplication;
class QEvent;
class QTreeWidgetItem;
class QWidget;

// 界面语言切换：直接复用 上游 的 nativeLang/english.xml 与 chineseSimplified.xml。
//
// 本移植历史上把中文文案直接写在代码里，没有 Qt 的 tr() / .qm 体系。为了不重写上百处
// setText()，这里以「中文原文」为源串：遍历已创建的 QObject，按属性 (text/title/
// windowTitle/...) 记录原文，再按词典替换为英文；切回中文时用记录的原文恢复。
// 词典 = 上游中英文 XML 的同 ID 配对 + 本移植 Linux 专属文案补充表。
class LocalizationManager : public QObject {
    Q_OBJECT
public:
    static LocalizationManager& instance();

    // 安装全局事件过滤器并载入语言资源；在 QApplication 构造后调用。
    void initialize(QCoreApplication* app);
    void setLanguage(const QString& language); // "zh_CN" / "en_US"
    QString language() const { return _language; }

    QString translate(const QString& source) const;
    // 重新翻译所有已创建窗口；语言切换与新建对话框时调用。
    void applyToAll();
    void applyToWidget(QWidget* root);

signals:
    // 语言实际发生切换后发出；运行期拼接的文本（状态栏等）据此自刷新。
    void languageChanged();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    explicit LocalizationManager(QObject* parent = nullptr);

    void ensureLoaded();
    void loadDictionaries();
    QString sourceText(QObject* object, const char* property, const QString& current);
    // 处理 "名称 (Ctrl+S)" / "名称(X)" 形式的 tooltip：翻译主体，保留快捷键/助记符后缀。
    QString translateTooltip(const QString& source) const;
    void translateObject(QObject* object);
    void translateTreeItem(QTreeWidgetItem* item);
    void translateComboBox(QComboBox* combo);

    QString _language{QStringLiteral("zh_CN")};
    QHash<QString, QString> _zhToEn;
    QHash<QString, QString> _extra;
    QSet<QWidget*> _knownWidgets;
    bool _initialized{false};
    bool _dictionariesLoaded{false};
};
