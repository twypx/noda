#pragma once

#include <string>

// 单遍流式 JSON 美化器：只重排空白/缩进，完整保留对象键顺序、数字与字符串
// 字面量（含转义）。格式化前后“去掉空白后的内容逐字节一致”，可安全整篇替换
// 并精确映射光标。非法 JSON 返回空串，调用方按“未改动”处理。
// format() 同步阻塞，须放工作线程执行，避免大文件卡住 UI。
namespace JsonFormatter {

std::string format(const std::string& utf8);

} // namespace JsonFormatter