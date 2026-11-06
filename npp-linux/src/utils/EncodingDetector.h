#pragma once
#include <string>
#include <string_view>
#include "uchardet.h"

class EncodingDetector {
public:
    static std::string detect(std::string_view data) {
        if (data.empty()) {
            return "UTF-8";
        }

        // BOM 优先检测
        if (data.size() >= 3 && 
            (unsigned char)data[0] == 0xEF && 
            (unsigned char)data[1] == 0xBB && 
            (unsigned char)data[2] == 0xBF) {
            return "UTF-8 BOM";
        }
        if (data.size() >= 2) {
            if ((unsigned char)data[0] == 0xFF && (unsigned char)data[1] == 0xFE) {
                return "UTF-16LE";
            }
            if ((unsigned char)data[0] == 0xFE && (unsigned char)data[1] == 0xFF) {
                return "UTF-16BE";
            }
        }

        uchardet_t handle = uchardet_new();
        if (!handle) {
            return "UTF-8";
        }

        // 仅取头部样本做统计探测：uchardet 对全文逐字节统计，超大文件会显著拖慢打开速度。
        // 1 MiB 样本对 UTF-8 / GBK / Big5 / Shift-JIS 等已足够可靠。
        constexpr std::size_t kProbeCap = 1u << 20;
        std::size_t probeLen = data.size() > kProbeCap ? kProbeCap : data.size();
        uchardet_handle_data(handle, data.data(), probeLen);
        uchardet_data_end(handle);

        const char* charset = uchardet_get_charset(handle);
        std::string result = (charset && charset[0] != '\0') ? charset : "UTF-8";
        uchardet_delete(handle);

        return result;
    }
};
