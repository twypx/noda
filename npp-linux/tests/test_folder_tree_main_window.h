#pragma once

#include <QObject>

class TestFolderTreeMainWindow : public QObject {
    Q_OBJECT
private slots:
    void fileMenuAndToolBarExposeOpenFolder();
    void dockStartsHidden();
    void openFolderPathShowsDockAndSetsRoot();
    void activatingFileOpensItInMainArea();
    void viewMenuToggleControlsDockVisibility();
    void shortcutManagerSeesOpenFolderAction();
};
