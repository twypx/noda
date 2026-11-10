#pragma once

#include <QObject>

class TestHexView : public QObject {
    Q_OBJECT
private slots:
    void hexFormattingLookupCorrectness();
    void viewportOnlyRenderingCalculatesRows();
    void selectionAndOffsetBidirectionalSync();
    void mainWindowTogglesHexViewSmoothly();

    // 模块 M：原始字节保真 + 视口惰性读取
    void rawBytesMatchDisk();
    void rangeReadAtHighOffset();
    void unsavedDocumentUsesMemory();
    void addressWidthAdapts();

    // 模块 N：二进制自动进入十六进制 + 主题色板
    void binaryFileOpensInHexView();
    void themeColorsFollowDarkMode();
    void monospaceFontSurvivesGlobalStyleSheet();
    void scrollbarDragRepaintsViewport();
};
