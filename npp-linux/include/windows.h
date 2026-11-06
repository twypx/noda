#pragma once
// 兼容性 Dummy windows.h，供跨平台编译时占位使用
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdlib>
#include <cstdio>

#ifndef FALSE
#define FALSE 0
#endif

#ifndef TRUE
#define TRUE 1
#endif

typedef int BOOL;
typedef unsigned char BYTE;
typedef unsigned short WORD;
typedef unsigned long DWORD;
typedef void* HANDLE;
typedef void* HWND;
typedef void* HINSTANCE;

inline char* _itoa(int value, char* str, int radix) {
    if (radix == 10) {
        std::sprintf(str, "%d", value);
    } else if (radix == 16) {
        std::sprintf(str, "%x", value);
    } else if (radix == 8) {
        std::sprintf(str, "%o", value);
    } else {
        std::sprintf(str, "%d", value);
    }
    return str;
}
