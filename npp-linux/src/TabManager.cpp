#include "TabManager.h"
#include "widgets/HexView.h"
#include <QContextMenuEvent>
#include <QEvent>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QStyle>
#include <QTabBar>

namespace {
void repolishTabBar(QTabBar* bar) {
    bar->style()->unpolish(bar);
    bar->style()->polish(bar);
    bar->update();
}
} // namespace

void TabManager::scheduleTabBarRepolish() {
    if (_styleBatchDepth > 0) {
        _styleBatchDirty = true;
        return;
    }
    repolishTabBar(tabBar());
    ++_styleRepolishCount;
}

void TabManager::beginStyleBatch() {
    ++_styleBatchDepth;
}

void TabManager::endStyleBatch() {
    if (_styleBatchDepth > 0) --_styleBatchDepth;
    if (_styleBatchDepth == 0 && _styleBatchDirty) {
        _styleBatchDirty = false;
        repolishTabBar(tabBar());
        ++_styleRepolishCount;
    }
}

TabManager::TabManager(QWidget* parent) : QTabWidget(parent) {
    setTabsClosable(true);
    setMovable(true);
    setDocumentMode(false);

    connect(this, &QTabWidget::tabCloseRequested, this, &TabManager::onTabCloseRequested);
    connect(this, &QTabWidget::currentChanged, this, &TabManager::onCurrentChanged);

    // 右键菜单由 TabManager 识别命中（它知道 tabAt），但菜单内容与文件操作归 MainWindow，
    // 因此这里只负责「选中 + 转发坐标」。
    tabBar()->installEventFilter(this);
}

bool TabManager::eventFilter(QObject* watched, QEvent* event) {
    if (watched == tabBar() && event->type() == QEvent::MouseButtonDblClick) {
        if (_doubleClickClose) {
            auto* me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton) {
                int index = tabBar()->tabAt(me->position().toPoint());
                if (index >= 0) {
                    closeTab(index);
                    event->accept();
                    return true;
                }
            }
        }
    }
    if (watched == tabBar() && event->type() == QEvent::ContextMenu) {
        auto* ce = static_cast<QContextMenuEvent*>(event);
        int index = tabBar()->tabAt(tabBar()->mapFromGlobal(ce->globalPos()));
        QPoint globalPos = ce->globalPos();

        // 键盘（菜单键 / Shift+F10）没有鼠标坐标，回退到当前标签的中心。
        if (index < 0 && ce->reason() == QContextMenuEvent::Keyboard) {
            index = currentIndex();
            if (index >= 0) globalPos = tabBar()->mapToGlobal(tabBar()->tabRect(index).center());
        }

        // 上游 TabBar::runProc 的 WM_RBUTTONDOWN：命中标签才改变选中项；
        // 空白条带（tabAt == -1）既不选中也不弹菜单。
        if (index < 0) {
            event->accept();
            return true;
        }

        if (index != currentIndex()) setCurrentIndex(index);
        emit tabContextMenuRequested(index, globalPos);
        event->accept();
        return true;
    }
    return QTabWidget::eventFilter(watched, event);
}

void TabManager::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        // 标签栏的宽度只等于标签本身那一段（QSS 没让它铺满），它右边到窗口边缘的空白
        // 落在 QTabWidget 自己身上：竖坐标在标签那一行里、横坐标在标签栏右侧，就是空白区。
        // 双击标签本身仍由 QTabBar 处理，不会跑到这里；编辑区的双击也被 EditorView 吃掉。
        const QRect barRect = tabBar()->geometry();
        const QPoint pos = event->position().toPoint();
        if (pos.y() >= barRect.top() && pos.y() <= barRect.bottom() && pos.x() > barRect.right()) {
            emit blankAreaActivated();
            event->accept();
            return;
        }
    }
    QTabWidget::mouseDoubleClickEvent(event);
}

EditorView* TabManager::currentEditor() const {
    return qobject_cast<EditorView*>(currentWidget());
}

EditorView* TabManager::editorAt(int index) const {
    return qobject_cast<EditorView*>(widget(index));
}

EditorView* TabManager::createNewTab() {
    auto* editor = new EditorView(this);
    editor->buffer()->setNewDocIndex(_newDocCounter++);

    int idx = addTab(editor, QString::fromStdString(editor->buffer()->getFileName()));
    setCurrentIndex(idx);

    connect(editor, &EditorView::documentModified, this, [this, editor](bool) {
        int i = indexOf(editor);
        if (i >= 0) {
            updateTabTitle(i);
        }
    });

    emit editorCreated(editor);
    return editor;
}

EditorView* TabManager::openFile(const QString& filePath) {
    // 检查是否已经打开
    for (int i = 0; i < count(); ++i) {
        auto* ed = editorAt(i);
        if (ed && QString::fromStdString(ed->buffer()->getFilePath()) == filePath) {
            setCurrentIndex(i);
            return ed;
        }
    }

    // 如果当前只有一个且未修改的空标签，直接在此打开
    if (count() == 1) {
        auto* firstEd = editorAt(0);
        if (firstEd && firstEd->buffer()->getFilePath().empty() && !firstEd->buffer()->isModified()) {
            if (firstEd->loadFromFile(filePath)) {
                updateTabTitle(0);
                return firstEd;
            }
        }
    }

    auto* editor = new EditorView(this);
    if (!editor->loadFromFile(filePath)) {
        delete editor;
        return nullptr;
    }

    int idx = addTab(editor, QString::fromStdString(editor->buffer()->getFileName()));
    setCurrentIndex(idx);

    connect(editor, &EditorView::documentModified, this, [this, editor](bool) {
        int i = indexOf(editor);
        if (i >= 0) {
            updateTabTitle(i);
        }
    });

    emit editorCreated(editor);
    return editor;
}

void TabManager::updateTabTitle(int index) {
    auto* ed = editorAt(index);
    if (!ed) return;

    QString title = QString::fromStdString(ed->buffer()->getFileName());
    // 上游 tabCompactLabelLen：0 表示不截断，超出长度用省略号，完整路径仍在 tooltip 里。
    if (_compactLabelLength > 0 && title.size() > _compactLabelLength) {
        title = title.left(_compactLabelLength) + QStringLiteral("...");
    }
    if (ed->buffer()->isModified()) {
        title += "*";
    }
    // 只在文本真正变化时写入，避免重复触发标题栏无效刷新。
    if (tabText(index) != title) {
        setTabText(index, title);
        emit tabTitleChanged(index);
    }
    setTabToolTip(index, QString::fromStdString(ed->buffer()->getFilePath()));
}

bool TabManager::closeTab(int index) {
    auto* ed = editorAt(index);
    if (!ed) return false;

    if (ed->buffer()->isModified()) {
        QString msg = QString("是否保存对文件 %1 的更改？")
                          .arg(QString::fromStdString(ed->buffer()->getFileName()));
        QMessageBox box(QMessageBox::Question, QStringLiteral("保存"), msg, QMessageBox::NoButton, this);
        auto* btnYes = box.addButton(QStringLiteral("是(&Y)"), QMessageBox::YesRole);
        box.addButton(QStringLiteral("否(&N)"), QMessageBox::NoRole);
        auto* btnCancel = box.addButton(QStringLiteral("取消(&C)"), QMessageBox::RejectRole);
        box.setDefaultButton(btnYes);
        box.setEscapeButton(btnCancel);
        box.exec();
        if (box.clickedButton() == btnYes) {
            if (!ed->saveToFile()) {
                return false;
            }
        } else if (box.clickedButton() == btnCancel || box.clickedButton() == nullptr) {
            return false;
        }
    }

    if (!ed->buffer()->getFilePath().empty()) {
        _closedFilesStack.append(QString::fromStdString(ed->buffer()->getFilePath()));
    }

    emit editorClosing(ed);
    removeTab(index);
    auto hexIt = _hexViews.find(ed);
    if (hexIt != _hexViews.end()) {
        delete hexIt.value();
        _hexViews.erase(hexIt);
    }
    delete ed;

    if (count() == 0) {
        // 上游 quitOnEmpty：不再补一个空文档，而是请求主窗口退出。
        if (_quitOnEmpty) {
            emit quitOnEmptyRequested();
        } else {
            createNewTab();
        }
    }
    return true;
}

bool TabManager::closeAll() {
    while (count() > 0) {
        if (!closeTab(count() - 1)) {
            return false;
        }
        if (count() == 0) break; // quitOnEmpty 生效时不再继续 closeTab(0)
        if (count() == 1 && editorAt(0)->buffer()->getFilePath().empty() && !editorAt(0)->buffer()->isModified()) {
            break;
        }
    }
    return true;
}

bool TabManager::closeAllExceptCurrent() {
    auto* curEd = currentEditor();
    if (!curEd) return true;
    for (int i = count() - 1; i >= 0; --i) {
        if (editorAt(i) != curEd) {
            if (!closeTab(i)) {
                return false;
            }
        }
    }
    return true;
}

bool TabManager::closeAllToLeft() {
    int curIdx = currentIndex();
    for (int i = curIdx - 1; i >= 0; --i) {
        if (!closeTab(i)) {
            return false;
        }
    }
    return true;
}

bool TabManager::closeAllToRight() {
    int curIdx = currentIndex();
    for (int i = count() - 1; i > curIdx; --i) {
        if (!closeTab(i)) {
            return false;
        }
    }
    return true;
}

bool TabManager::closeAllUnchanged() {
    for (int i = count() - 1; i >= 0; --i) {
        auto* ed = editorAt(i);
        if (ed && !ed->buffer()->isModified() && !ed->buffer()->getFilePath().empty()) {
            closeTab(i);
        }
    }
    return true;
}

void TabManager::moveTabLeft() {
    int cur = currentIndex();
    if (cur > 0) {
        auto* w = widget(cur);
        QString title = tabText(cur);
        QString tooltip = tabToolTip(cur);
        removeTab(cur);
        insertTab(cur - 1, w, title);
        setTabToolTip(cur - 1, tooltip);
        setCurrentIndex(cur - 1);
    }
}

void TabManager::moveTabRight() {
    int cur = currentIndex();
    if (cur < count() - 1) {
        auto* w = widget(cur);
        QString title = tabText(cur);
        QString tooltip = tabToolTip(cur);
        removeTab(cur);
        insertTab(cur + 1, w, title);
        setTabToolTip(cur + 1, tooltip);
        setCurrentIndex(cur + 1);
    }
}

void TabManager::moveTabFirst() {
    int cur = currentIndex();
    if (cur > 0) {
        auto* w = widget(cur);
        QString title = tabText(cur);
        QString tooltip = tabToolTip(cur);
        removeTab(cur);
        insertTab(0, w, title);
        setTabToolTip(0, tooltip);
        setCurrentIndex(0);
    }
}

void TabManager::moveTabLast() {
    int cur = currentIndex();
    if (cur < count() - 1) {
        auto* w = widget(cur);
        QString title = tabText(cur);
        QString tooltip = tabToolTip(cur);
        removeTab(cur);
        addTab(w, title);
        setTabToolTip(count() - 1, tooltip);
        setCurrentIndex(count() - 1);
    }
}

bool TabManager::restoreClosedFile() {
    if (_closedFilesStack.isEmpty()) return false;
    QString path = _closedFilesStack.takeLast();
    return openFile(path) != nullptr;
}

void TabManager::onTabCloseRequested(int index) {
    closeTab(index);
}

void TabManager::onCurrentChanged(int index) {
    auto* ed = editorAt(index);
    emit activeEditorChanged(ed);
    emit hexViewToggled(isHexViewActive());
}

void TabManager::setTabReduce(bool reduce) {
    if (_tabReduce == reduce) return;
    _tabReduce = reduce;
    // 动态属性驱动主题 QSS，不塞局部样式表（会覆盖选中态并触发整棵子树重算）。
    QTabBar* bar = tabBar();
    bar->setProperty("compact", reduce);
    scheduleTabBarRepolish();
}

void TabManager::setTabBarHidden(bool hide) {
    if (_tabBarHidden == hide) return;
    _tabBarHidden = hide;
    setTabBarAutoHide(false); // 上游 hide 是“始终隐藏”，不是单标签自动隐藏
    tabBar()->setVisible(!hide);
}

void TabManager::setVerticalTabs(bool vertical) {
    if (_verticalTabs == vertical) return;
    _verticalTabs = vertical;
    setTabPosition(vertical ? QTabWidget::West : QTabWidget::North);
}

void TabManager::setActiveTopBar(bool draw) {
    if (_activeTopBar == draw) return;
    _activeTopBar = draw;
    QTabBar* bar = tabBar();
    bar->setProperty("activeTopBar", draw);
    scheduleTabBarRepolish();
}

void TabManager::setInactiveColored(bool on) {
    if (_inactiveColored == on) return;
    _inactiveColored = on;
    QTabBar* bar = tabBar();
    bar->setProperty("inactiveColored", on);
    scheduleTabBarRepolish();
}

void TabManager::setCompactLabelLength(int length) {
    const int clamped = qMax(0, qMin(257, length)); // 上游 NB_MAX_TAB_COMPACT_LABEL_LEN
    if (_compactLabelLength == clamped) return;
    _compactLabelLength = clamped;
    for (int i = 0; i < count(); ++i) updateTabTitle(i);
}

void TabManager::resizeEvent(QResizeEvent* event) {
    QTabWidget::resizeEvent(event);
    auto* ed = currentEditor();
    if (ed) {
        auto it = _hexViews.find(ed);
        if (it != _hexViews.end() && it.value() && it.value()->isVisible()) {
            it.value()->setGeometry(ed->rect());
        }
    }
}

HexView* TabManager::currentHexView() const {
    auto* ed = currentEditor();
    if (!ed) return nullptr;
    auto it = _hexViews.find(ed);
    if (it != _hexViews.end()) {
        return it.value();
    }
    auto* hex = new HexView(ed);
    hex->setDarkMode(_isDarkMode);
    hex->hide();
    _hexViews.insert(ed, hex);
    return hex;
}

HexView* TabManager::hexViewAt(int index) const {
    auto* ed = editorAt(index);
    if (!ed) return nullptr;
    auto it = _hexViews.find(ed);
    if (it != _hexViews.end()) {
        return it.value();
    }
    auto* hex = new HexView(ed);
    hex->setDarkMode(_isDarkMode);
    hex->hide();
    _hexViews.insert(ed, hex);
    return hex;
}

bool TabManager::isHexViewActive() const {
    auto* ed = currentEditor();
    if (!ed) return false;
    auto it = _hexViews.find(ed);
    return (it != _hexViews.end() && it.value() && it.value()->isVisible());
}

void TabManager::toggleHexView(bool enable) {
    auto* ed = currentEditor();
    if (!ed) return;
    auto* hex = currentHexView();
    if (!hex) return;

    if (enable) {
        // 已保存文件直接映射磁盘原始字节（保真且零整份拷贝）；
        // 未保存的新文档回退为内存缓冲。十六进制视图反映的是磁盘内容，
        // 未保存的编辑需先保存后才体现在十六进制视图中。
        const QString path = QString::fromStdString(ed->buffer()->getFilePath());
        if (path.isEmpty() || !hex->setFile(path)) {
            hex->setData(ed->documentBytes());
        }
        hex->setGeometry(ed->rect());
        hex->show();
        hex->setFocus();
    } else {
        hex->hide();
        ed->setFocus();
    }
    emit hexViewToggled(enable);
}

void TabManager::setDarkMode(bool dark) {
    _isDarkMode = dark;
    for (auto* hex : _hexViews) {
        if (hex) hex->setDarkMode(dark);
    }
}
