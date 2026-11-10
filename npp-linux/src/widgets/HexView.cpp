#include "HexView.h"

#include <QPainter>
#include <QScrollBar>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QFontDatabase>
#include <QFileInfo>

#include <algorithm>
#include <climits>
#include <cstring>

#include <sys/mman.h>
#include <unistd.h>

namespace {

static char s_hexTable[256][3];
static char s_asciiTable[256];
static bool s_tablesInitialized = false;

void ensureTables() {
    if (s_tablesInitialized) return;
    static const char hexDigits[] = "0123456789ABCDEF";
    for (int i = 0; i < 256; ++i) {
        s_hexTable[i][0] = hexDigits[(i >> 4) & 0x0F];
        s_hexTable[i][1] = hexDigits[i & 0x0F];
        s_hexTable[i][2] = '\0';

        // ASCII 可打印范围 0x20 到 0x7E
        s_asciiTable[i] = (i >= 0x20 && i <= 0x7E) ? static_cast<char>(i) : '.';
    }
    s_tablesInitialized = true;
}

} // namespace

HexView::HexView(QWidget* parent)
    : QAbstractScrollArea(parent)
{
    ensureTables();
    _colors = ThemeManager::hexViewColors(false);

    _monoFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    _monoFont.setStyleHint(QFont::Monospace);
    _monoFont.setFixedPitch(true);
    setFont(_monoFont);
    // 全局 QSS 的 QWidget{font-family} 会把控件（含 viewport）换成比例界面字体；
    // 比例字体下空格宽度≠数字宽度，列栅格按 c*charWidth 计算会与实际绘制逐列错位，
    // 表现为越往右光标/点击命中越偏。两道保险：
    //  1) 控件级样式表钉死等宽字体（优先级高于应用样式表）；
    //  2) paintEvent 显式 painter.setFont(_monoFont)，与 _charWidth 测量同源，彻底不受 QSS 影响。
    const QString monoFamily = _monoFont.family();
    if (!monoFamily.isEmpty()) {
        setStyleSheet(QStringLiteral("font-family: \"%1\";").arg(monoFamily));
    }

    setFocusPolicy(Qt::StrongFocus);
    viewport()->setAutoFillBackground(false);

    updateLayout();
}

HexView::~HexView() {
    releaseSource();
}

void HexView::releaseSource() {
    if (_map) {
        if (_mapSize > 0) {
            ::munmap(const_cast<char*>(_map), static_cast<size_t>(_mapSize));
        }
        _map = nullptr;
    }
    _mapSize = 0;
    if (_file.isOpen()) _file.close();
    _data.clear();
    _dataSize = 0;
    _source = Source::None;
}

void HexView::setData(const QByteArray& data) {
    releaseSource();
    _source = Source::Memory;
    _data = data;                 // QByteArray 为 COW：此处不产生深拷贝
    _dataSize = _data.size();
    _addrDigits = (_dataSize > 0xFFFFFFFFll) ? 16 : 8;
    if (_dataSize <= 0) {
        _cursorPos = 0;
    } else if (_cursorPos >= _dataSize) {
        _cursorPos = _dataSize - 1;
    }
    updateLayout();
    viewport()->update();
}

bool HexView::setFile(const QString& path) {
    releaseSource();

    const QFileInfo fi(path);
    if (!fi.exists() || !fi.isFile()) return false;

    _file.setFileName(path);
    if (!_file.open(QIODevice::ReadOnly)) return false;

    const qint64 size = _file.size();
    _dataSize = size;
    _addrDigits = (size > 0xFFFFFFFFll) ? 16 : 8;

    if (size > 0) {
        void* m = ::mmap(nullptr, static_cast<size_t>(size), PROT_READ, MAP_PRIVATE,
                         _file.handle(), 0);
        if (m != MAP_FAILED) {
            _map = static_cast<const char*>(m);
            _mapSize = size;
            _source = Source::Mapped;
            _file.close();        // 映射已持有内存，释放 fd
        } else {
            _source = Source::File; // 回退：paint 时按需 seek+read
        }
    } else {
        _source = Source::None;
        _file.close();
    }

    if (_dataSize <= 0) {
        _cursorPos = 0;
    } else if (_cursorPos >= _dataSize) {
        _cursorPos = _dataSize - 1;
    }
    updateLayout();
    viewport()->update();
    return true;
}

int HexView::rowCount() const {
    if (_dataSize <= 0) return 1;
    const qint64 rows = (_dataSize + _bytesPerRow - 1) / _bytesPerRow;
    return (rows > INT_MAX) ? INT_MAX : static_cast<int>(rows);
}

qint64 HexView::bytesInRange(qint64 offset, qint64 len, char* out) const {
    if (!out || len <= 0 || offset < 0 || offset >= _dataSize) return 0;
    const qint64 n = std::min(len, _dataSize - offset);
    switch (_source) {
    case Source::Memory:
        std::memcpy(out, _data.constData() + offset, static_cast<size_t>(n));
        return n;
    case Source::Mapped:
        if (!_map) return 0;
        std::memcpy(out, _map + offset, static_cast<size_t>(n));
        return n;
    case Source::File: {
        if (!_file.isOpen()) return 0;
        if (!_file.seek(offset)) return 0;
        const qint64 rd = _file.read(out, n);
        return rd < 0 ? 0 : rd;
    }
    case Source::None:
    default:
        return 0;
    }
}

QByteArray HexView::bytesInRange(qint64 offset, qint64 len) const {
    if (len <= 0 || offset < 0 || offset >= _dataSize) return QByteArray();
    const qint64 n = std::min(len, _dataSize - offset);
    QByteArray out;
    out.resize(static_cast<int>(n));
    const qint64 got = bytesInRange(offset, n, out.data());
    if (got < n) out.truncate(static_cast<int>(std::max<qint64>(0, got)));
    return out;
}

void HexView::setCursorPosition(qint64 pos) {
    if (_dataSize == 0) {
        _cursorPos = 0;
        return;
    }
    const qint64 clamped = std::clamp<qint64>(pos, 0, _dataSize - 1);
    if (_cursorPos != clamped) {
        _cursorPos = clamped;
        scrollToCursor();
        viewport()->update();
        emit cursorPositionChanged(_cursorPos);
    }
}

unsigned char HexView::selectedByte() const {
    if (_cursorPos < 0 || _cursorPos >= _dataSize) return 0;
    char c = 0;
    if (bytesInRange(_cursorPos, 1, &c) == 1) return static_cast<unsigned char>(c);
    return 0;
}

void HexView::setDarkMode(bool dark) {
    if (_isDarkMode != dark) {
        _isDarkMode = dark;
        _colors = ThemeManager::hexViewColors(dark);
        viewport()->update();
    }
}

QString HexView::formatByteHex(unsigned char byte) {
    ensureTables();
    return QString::fromLatin1(s_hexTable[byte], 2);
}

char HexView::byteToAscii(unsigned char byte) {
    ensureTables();
    return s_asciiTable[byte];
}

QString HexView::formatAddress(qint64 address, int digits) {
    ensureTables();
    digits = std::clamp(digits, 1, 16);
    char buf[17];
    static const char hexDigits[] = "0123456789ABCDEF";
    quint64 a = static_cast<quint64>(address);
    for (int i = digits - 1; i >= 0; --i) {
        buf[i] = hexDigits[a & 0x0F];
        a >>= 4;
    }
    buf[digits] = '\0';
    return QString::fromLatin1(buf, digits);
}

void HexView::calculateVisibleRange(int scrollValue, int visibleHeightInRows,
                                    int& startRow, int& endRow) const {
    startRow = std::max(0, scrollValue);
    const int total = rowCount();
    endRow = std::min(total, startRow + visibleHeightInRows);
}

void HexView::updateLayout() {
    const QFontMetrics fm(_monoFont);
    _charWidth = std::max(1, fm.horizontalAdvance(QLatin1Char('0')));
    _lineHeight = std::max(16, fm.lineSpacing());

    // 位宽自适应：8 位地址 + ':' + 空格；16 位地址同理。
    _addrColWidth = _charWidth * (_addrDigits + 2);
    // bytesPerRow 个 "XX " + 第 8 字节后的 1 个空隙
    _hexColWidth = _charWidth * (_bytesPerRow * 3 + 1);
    _asciiColWidth = _charWidth * _bytesPerRow;

    const int totalRows = rowCount();
    const int visibleRows = std::max(1, viewport()->height() / _lineHeight);
    verticalScrollBar()->setRange(0, std::max(0, totalRows - visibleRows));
    verticalScrollBar()->setPageStep(visibleRows);
}

void HexView::resizeEvent(QResizeEvent* event) {
    QAbstractScrollArea::resizeEvent(event);
    updateLayout();
    viewport()->update();
}

void HexView::scrollContentsBy(int dx, int dy) {
    Q_UNUSED(dx);
    Q_UNUSED(dy);
    // 本控件完全自绘（paintEvent 按 verticalScrollBar()->value() 计算起始行），
    // 不能依赖基类的像素滚动：拖动滚动条只改 value，若不在这里重绘，视口内容不会更新。
    // 滚轮路径由 Qt 另行刷新，所以此前表现为“滚轮能滚、拖条没反应”。
    viewport()->update();
}

void HexView::changeEvent(QEvent* event) {
    QAbstractScrollArea::changeEvent(event);
    // 主题/界面字体变化会触发 FontChange：把等宽字号同步到实际字号，并重算字符宽度与行高，
    // 保证列栅格、绘制与鼠标命中共用同一套度量。
    if (event->type() == QEvent::FontChange) {
        const QFont f = font();
        if (f.pointSizeF() > 0) {
            _monoFont.setPointSizeF(f.pointSizeF());
        } else if (f.pixelSize() > 0) {
            _monoFont.setPixelSize(f.pixelSize());
        }
        updateLayout();
        viewport()->update();
    }
}

void HexView::scrollToCursor() {
    const int targetRow = static_cast<int>(_cursorPos / _bytesPerRow);
    const int curScroll = verticalScrollBar()->value();
    const int visibleRows = std::max(1, viewport()->height() / _lineHeight);

    if (targetRow < curScroll) {
        verticalScrollBar()->setValue(targetRow);
    } else if (targetRow >= curScroll + visibleRows) {
        verticalScrollBar()->setValue(targetRow - visibleRows + 1);
    }
}

qint64 HexView::positionFromPoint(const QPoint& pt, bool* isAsciiArea) const {
    if (_dataSize == 0) return 0;

    const int row = verticalScrollBar()->value() + pt.y() / _lineHeight;
    if (row >= rowCount()) return _dataSize - 1;

    const int x = pt.x() - _leftMargin;
    const int hexStartX = _addrColWidth;
    const int hexEndX = hexStartX + _hexColWidth;
    const int asciiStartX = hexEndX + _charWidth * 2;
    const int asciiEndX = asciiStartX + _asciiColWidth;

    if (x >= hexStartX && x < hexEndX) {
        if (isAsciiArea) *isAsciiArea = false;
        const int hexX = x - hexStartX;
        int col = hexX / (_charWidth * 3);
        // 后 8 字节区域需要补偿中间多出的 1 个字符间隙
        if (hexX >= _charWidth * (8 * 3)) {
            col = (hexX - _charWidth) / (_charWidth * 3);
        }
        col = std::clamp(col, 0, _bytesPerRow - 1);
        const qint64 pos = static_cast<qint64>(row) * _bytesPerRow + col;
        return std::min<qint64>(pos, _dataSize - 1);
    } else if (x >= asciiStartX && x <= asciiEndX + _charWidth) {
        if (isAsciiArea) *isAsciiArea = true;
        int col = (x - asciiStartX) / _charWidth;
        col = std::clamp(col, 0, _bytesPerRow - 1);
        const qint64 pos = static_cast<qint64>(row) * _bytesPerRow + col;
        return std::min<qint64>(pos, _dataSize - 1);
    }

    return -1;
}

void HexView::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        bool inAscii = false;
        const qint64 pos = positionFromPoint(event->pos(), &inAscii);
        if (pos >= 0) {
            _isFocusInHex = !inAscii;
            setCursorPosition(pos);
        }
    }
    QAbstractScrollArea::mousePressEvent(event);
}

void HexView::handleKeyEvent(QKeyEvent* event) {
    keyPressEvent(event);
}

void HexView::keyPressEvent(QKeyEvent* event) {
    if (_dataSize == 0) {
        QAbstractScrollArea::keyPressEvent(event);
        return;
    }

    qint64 newPos = _cursorPos;
    const int visibleRows = std::max(1, viewport()->height() / _lineHeight);

    switch (event->key()) {
    case Qt::Key_Left:
        newPos = std::max<qint64>(0, _cursorPos - 1);
        break;
    case Qt::Key_Right:
        newPos = std::min<qint64>(_dataSize - 1, _cursorPos + 1);
        break;
    case Qt::Key_Up:
        newPos = std::max<qint64>(0, _cursorPos - _bytesPerRow);
        break;
    case Qt::Key_Down:
        newPos = std::min<qint64>(_dataSize - 1, _cursorPos + _bytesPerRow);
        break;
    case Qt::Key_PageUp:
        newPos = std::max<qint64>(0, _cursorPos - static_cast<qint64>(visibleRows) * _bytesPerRow);
        break;
    case Qt::Key_PageDown:
        newPos = std::min<qint64>(_dataSize - 1, _cursorPos + static_cast<qint64>(visibleRows) * _bytesPerRow);
        break;
    case Qt::Key_Home:
        newPos = (event->modifiers() & Qt::ControlModifier)
                     ? 0
                     : (_cursorPos / _bytesPerRow) * _bytesPerRow;
        break;
    case Qt::Key_End:
        if (event->modifiers() & Qt::ControlModifier) {
            newPos = _dataSize - 1;
        } else {
            newPos = std::min<qint64>(_dataSize - 1,
                                      (_cursorPos / _bytesPerRow) * _bytesPerRow + (_bytesPerRow - 1));
        }
        break;
    default:
        QAbstractScrollArea::keyPressEvent(event);
        return;
    }

    if (newPos != _cursorPos) {
        setCursorPosition(newPos);
    }
}

void HexView::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(viewport());
    painter.setFont(_monoFont); // 与 _charWidth 测量同源，避免 QSS 改字体导致的列错位

    const QColor bg = _colors.background;
    const QColor addrFg = _colors.address;
    const QColor textFg = _colors.text;
    const QColor lineSep = _colors.separator;
    const QColor cursorHexBg = _colors.cursorHex;
    const QColor cursorAsciiBg = _colors.cursorAscii;

    painter.fillRect(viewport()->rect(), bg);

    const int startRow = verticalScrollBar()->value();
    const int visibleRows = viewport()->height() / _lineHeight + 2;
    const int totalRows = rowCount();
    const int endRow = std::min(totalRows, startRow + visibleRows);

    const int hexStartX = _leftMargin + _addrColWidth;
    const int hexEndX = hexStartX + _hexColWidth;
    const int asciiStartX = hexEndX + _charWidth * 2;

    painter.setPen(lineSep);
    painter.drawLine(hexStartX - _charWidth, 0, hexStartX - _charWidth, viewport()->height());
    painter.drawLine(hexEndX + _charWidth, 0, hexEndX + _charWidth, viewport()->height());

    if (endRow <= startRow) return;

    const QFontMetrics fm(_monoFont);
    const int baselineOffset = fm.ascent();

    // 一次性读取可见窗口的原始字节：mmap 路径为 memcpy，文件回退为单次 seek+read。
    const qint64 firstByte = static_cast<qint64>(startRow) * _bytesPerRow;
    const qint64 want = static_cast<qint64>(endRow - startRow) * _bytesPerRow;
    _paintWindow.resize(static_cast<int>(want));
    const qint64 got = bytesInRange(firstByte, want, _paintWindow.data());
    const char* win = _paintWindow.constData();

    const int cursorRow = static_cast<int>(_cursorPos / _bytesPerRow);
    const int cursorCol = static_cast<int>(_cursorPos % _bytesPerRow);

    QString hexRow;
    QString asciiRow;

    for (int r = startRow; r < endRow; ++r) {
        const int y = (r - startRow) * _lineHeight;
        const qint64 rowOffset = static_cast<qint64>(r) * _bytesPerRow;
        const qint64 remain = _dataSize - rowOffset;
        const int bytesThisRow = static_cast<int>(std::clamp<qint64>(remain, 0, _bytesPerRow));

        // 光标高亮底色（先画，文本覆盖其上）：仅高亮当前焦点所在的一侧（HEX / ASCII）。
        if (r == cursorRow && cursorCol < bytesThisRow) {
            if (_isFocusInHex) {
                int hexX = hexStartX + cursorCol * (_charWidth * 3);
                if (cursorCol >= 8) hexX += _charWidth;
                painter.fillRect(hexX - 2, y + 1, _charWidth * 2 + 4, _lineHeight - 2, cursorHexBg);
            } else {
                const int asciiX = asciiStartX + cursorCol * _charWidth;
                painter.fillRect(asciiX - 1, y + 1, _charWidth + 2, _lineHeight - 2, cursorAsciiBg);
            }
        }

        // 地址列
        painter.setPen(addrFg);
        painter.drawText(_leftMargin, y + baselineOffset,
                         formatAddress(rowOffset, _addrDigits) + QLatin1Char(':'));

        // 整行拼接后一次绘制，消除逐字节 QString 分配与 drawText 调用
        hexRow.clear();
        asciiRow.clear();
        hexRow.reserve(bytesThisRow * 3 + 1);
        asciiRow.reserve(bytesThisRow);
        const qint64 rowWindowBase = static_cast<qint64>(r - startRow) * _bytesPerRow;
        for (int c = 0; c < bytesThisRow; ++c) {
            const qint64 wi = rowWindowBase + c;
            const unsigned char b = (wi < got)
                ? static_cast<unsigned char>(win[wi])
                : static_cast<unsigned char>(0);
            hexRow += QLatin1Char(s_hexTable[b][0]);
            hexRow += QLatin1Char(s_hexTable[b][1]);
            hexRow += QLatin1Char(' ');
            if (c == 7) hexRow += QLatin1Char(' ');
            asciiRow += QLatin1Char(s_asciiTable[b]);
        }

        painter.setPen(textFg);
        painter.drawText(hexStartX, y + baselineOffset, hexRow);
        painter.drawText(asciiStartX, y + baselineOffset, asciiRow);
    }
}
