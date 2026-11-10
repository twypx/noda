#!/usr/bin/env bash
# 回归测试入口。只构建 npp_tests 目标，避免每次迭代都触发主程序的 LTO 链接。
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "${SCRIPT_DIR}")"
BUILD_DIR="${ROOT_DIR}/build"

echo "=== Noda 回归测试 ==="
cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Release -DNPP_BUILD_TESTS=ON

echo ">>> 构建 npp_tests ..."
cmake --build "${BUILD_DIR}" --target npp_tests -- -j"$(nproc)"

echo ">>> 运行 ctest ..."
ctest --test-dir "${BUILD_DIR}" --output-on-failure
