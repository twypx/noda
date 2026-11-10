#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"

echo "=== Noda 一键构建脚本 ==="

# 检查 cmake
if ! command -v cmake &> /dev/null; then
    echo "错误: 未找到 cmake，请先安装: sudo apt install cmake"
    exit 1
fi

mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

echo ">>> 配置 CMake..."
cmake -DCMAKE_BUILD_TYPE=Release ..

echo ">>> 开始多线程编译..."
cmake --build . -- -j$(nproc)

echo "=== 构建成功！执行程序位于: ${BUILD_DIR}/noda ==="
