#pragma once

#include <string>

// 代码格式化封装：C/C++/C#/Java/Objective-C 走纯 AStyle（MIT），JSON 走自带的
// 保序流式美化器。两者都只重排空白、保证“去掉空白后内容逐字节一致”。
// format() 是同步阻塞调用，必须放到工作线程执行，避免大文件卡住 UI。
namespace CodeFormatter {

bool isSupported(const std::string& langId);

// 按 langId 分派格式化引擎，返回格式化后的 UTF-8 文本；
// 引擎失败或无需改动时原样返回输入，JSON 非法时返回空串（均按“未改动”处理）。
std::string format(const std::string& utf8, const std::string& langId);

} // namespace CodeFormatter