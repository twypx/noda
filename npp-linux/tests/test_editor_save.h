#pragma once

#include <QObject>

class TestEditorSave : public QObject {
    Q_OBJECT
private slots:
    void saveSuccessUsesConfiguredEncoding();
    void saveSuccessWritesBom();
    // 转码失败不能 SETSAVEPOINT，不能截断原文件，失败原因要可查询。
    void saveFailureKeepsFileAndDirtyFlag();
    // 拷贝另存为必须走与保存相同的编码/BOM 链路，且不改变当前文档状态。
    void saveCopyWritesTargetEncodingAndKeepsState();
    void saveCopyRespectsBom();
    void saveCopyFailureWritesNothing();
    // 载入文件不应创建撤销动作，载入后 Undo 不能清空文件
    void loadFromFileDoesNotCreateUndoStep();
    // 虚拟空格批量对齐
    void columnInsertVirtualSpacesBlock();
    // 行排序在不同换行符下一致并具备撤销事务
    void sortLinesSupportsCrAndUndo();
    // 行号边距宽度按位数缓存，避免高频字符/行变动时重复字体度量
    void lineNumberMarginWidthDigitsCaching();
};
