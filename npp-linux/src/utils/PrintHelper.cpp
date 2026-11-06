#include "PrintHelper.h"
#include <QPagedPaintDevice>

namespace PrintHelper {

void paintTextLines(QPainter& p, const QStringList& lines, int tabWidth, int fromPage, int toPage) {
    // 必须取 painter 的度量：打印机/PDF 的 DPI 与屏幕不同，用 QFontMetrics(font) 会按屏幕 DPI 换算。
    const QFontMetrics fm(p.fontMetrics());
    const int lineH = fm.height();
    const int pageH = p.viewport().height();
    const int spaceW = qMax(1, fm.horizontalAdvance(QLatin1Char(' ')));
    // 左侧行号栏宽度随总行数变宽，保证列对齐。
    const int gutter = fm.horizontalAdvance(QString::number(lines.size())) + 3 * spaceW;
    const int maxCols = qMax(1, (p.viewport().width() - gutter) / spaceW);
    const int tab = qMax(1, tabWidth);
    // QPagedPaintDevice 不是 QObject，所以只能用 dynamic_cast 而不是 qobject_cast。
    // QPrinter 和 QPdfWriter 都继承它，非分页设备（QImage 等）则为 nullptr。
    auto* paged = dynamic_cast<QPagedPaintDevice*>(p.device());

    // 基线从 lineH 起逐行下推一个行高，最后一行的基线不能越过页底，所以每页恰好 pageH / lineH 行。
    const int linesPerPage = qMax(1, pageH / lineH);
    // 页码范围之外的行直接跳过（而不是“走而不画”），否则开头会多出一整页空白。
    const int firstLine = (fromPage - 1) * linesPerPage;
    const int lastLine = toPage > 0 ? toPage * linesPerPage : lines.size();

    for (int i = firstLine; i < qMin(lastLine, lines.size()); ++i) {
        const int rowInPage = (i - firstLine) % linesPerPage;
        if (rowInPage == 0 && i > firstLine) {
            if (!paged) break; // 非分页设备只保留第一页
            paged->newPage();
        }
        const int baseline = (rowInPage + 1) * lineH;

        QString line = lines[i];
        if (line.endsWith(QLatin1Char('\r'))) line.chop(1); // CRLF 文档按 LF 切分后残留的 CR

        // 制表符展开到列边界；超出页宽的列直接截断（与 Notepad++ 默认“打印不折行”一致）。
        QString out;
        int col = 0;
        for (const QChar ch : line) {
            if (ch == QLatin1Char('\t')) {
                const int next = col + tab - (col % tab);
                out += QString(next - col, QLatin1Char(' '));
                col = next;
                continue;
            }
            const int w = ch.unicode() < 0x80 ? 1 : 2; // CJK 等宽字符按两列估算
            if (col + w > maxCols) break;
            out += ch;
            col += w;
        }
        p.drawText(0, baseline, QString::number(i + 1));   // i 是绝对行号，跳页打印时行号仍正确
        p.drawText(gutter, baseline, out);
    }
}

} // namespace PrintHelper
