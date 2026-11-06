#pragma once

#include <QPainter>
#include <QStringList>

// 把纯文本按行分页绘制到 painter 当前的绘制设备上（打印机或 PDF）。
// 与 QPrinter / 打印对话框解耦，好处有两个：
//  1. 可以用 QPdfWriter 离线验证分页、行号栏和制表符展开，不必真的送打印任务；
//  2. onPrint 只负责“取文本 + 选打印机”，排版规则集中在一处便于调整。
namespace PrintHelper {

// toPage <= 0 表示一直打到文档结尾；fromPage/toPage 与打印对话框里的“页码范围”对应。
void paintTextLines(QPainter& p, const QStringList& lines, int tabWidth,
                    int fromPage = 1, int toPage = 0);

} // namespace PrintHelper
