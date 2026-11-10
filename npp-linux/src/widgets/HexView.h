#pragma once

#include <QAbstractScrollArea>
#include <QByteArray>
#include <QFile>
#include <QFont>
#include <QString>
#include <algorithm>
#include "utils/ThemeManager.h"

// 十六进制/ASCII 查看器。
//
// 性能约束（对齐项目「极致高性能」第一原则）：
//  - 已保存文件走 mmap，只对可见窗口做一次区间访问，绝不整份载入内存；
//  - 未保存文档回退为内存缓冲（QByteArray，COW 共享，不深拷）；
//  - paintEvent 仅遍历可见行，整行拼接后一次性绘制。
class HexView : public QAbstractScrollArea {
    Q_OBJECT
public:
    explicit HexView(QWidget* parent = nullptr);
    ~HexView() override;

    // 内存数据源（未保存文档 / 测试）。QByteArray 采用 COW，不产生深拷贝。
    void setData(const QByteArray& data);
    // 磁盘文件数据源：mmap 惰性映射，不拷贝整份文件。成功返回 true。
    bool setFile(const QString& path);

    qint64 dataSize() const { return _dataSize; }
    int rowCount() const;

    qint64 cursorPosition() const { return _cursorPos; }
    void setCursorPosition(qint64 pos);
    unsigned char selectedByte() const;

    // 读取 [offset, offset+len) 原始字节（自动夹取到数据末尾）。
    QByteArray bytesInRange(qint64 offset, qint64 len) const;

    void setBytesPerRow(int bytes) { _bytesPerRow = std::max(1, bytes); updateLayout(); }
    int bytesPerRow() const { return _bytesPerRow; }

    void setDarkMode(bool dark);
    bool isDarkMode() const { return _isDarkMode; }
    // 当前使用的主题色板（供测试与主题切换校验）。
    const HexViewColors& themeColors() const { return _colors; }

    // 高性能静态转换函数（查表法）
    static QString formatByteHex(unsigned char byte);
    static char byteToAscii(unsigned char byte);
    // 地址格式化；digits 默认 8（32 位文件），大文件可传 16。
    static QString formatAddress(qint64 address, int digits = 8);

    // 辅助计算视口可见范围
    void calculateVisibleRange(int scrollValue, int visibleHeightInRows, int& startRow, int& endRow) const;

    // 键盘事件供测试和自身调用
    void handleKeyEvent(QKeyEvent* event);

signals:
    void cursorPositionChanged(qint64 pos);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void changeEvent(QEvent* event) override;
    void scrollContentsBy(int dx, int dy) override;

private:
    enum class Source { None, Memory, Mapped, File };

    void initTables();
    void updateLayout();
    void scrollToCursor();
    void releaseSource();
    qint64 positionFromPoint(const QPoint& pt, bool* isAsciiArea = nullptr) const;
    // 内部区间读取：把最多 len 字节写入 out，返回实际读取字节数（const 但可能读文件）。
    qint64 bytesInRange(qint64 offset, qint64 len, char* out) const;

    QByteArray _data;              // Memory 源
    const char* _map{nullptr};     // Mapped 源
    qint64 _mapSize{0};
    mutable QFile _file;           // File 源（回退）
    Source _source{Source::None};
    qint64 _dataSize{0};
    int _addrDigits{8};

    qint64 _cursorPos{0};
    int _bytesPerRow{16};

    QFont _monoFont;               // 绘制与测量共用的等宽字体（不受 QSS 影响）
    int _charWidth{8};
    int _lineHeight{18};
    int _addrColWidth{80};
    int _hexColWidth{350};
    int _asciiColWidth{150};
    int _leftMargin{10};

    bool _isDarkMode{false};
    bool _isFocusInHex{true};
    HexViewColors _colors{};

    // paint 复用的可见窗口缓冲，避免每次重绘重新分配。
    mutable QByteArray _paintWindow;
};
