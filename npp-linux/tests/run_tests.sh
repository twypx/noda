#!/usr/bin/env bash
# 回归测试入口。只构建 npp_tests 目标，避免每次迭代都触发主程序的 LTO 链接。
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "${SCRIPT_DIR}")"
BUILD_DIR="${ROOT_DIR}/build"

echo "=== Noda 回归测试 ==="
cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Release -DNPP_BUILD_TESTS=ON

# 提前校验真实配置资源：多个用例通过 applicationDirPath() 读取它们，
# 缺失时测试会以「与功能无关」的原因集体报红，必须在构建前就给出清晰错误。
for resource in langs.model.xml stylers.model.xml themes nativeLang; do
    if [[ ! -e "${BUILD_DIR}/${resource}" ]]; then
        echo "错误：构建目录缺少 ${resource}，测试无法加载真实配置。" >&2
        echo "请检查 CMakeLists.txt 中 configure_file/file(COPY) 的资源复制规则。" >&2
        exit 1
    fi
done

echo ">>> 构建 npp_tests ..."
cmake --build "${BUILD_DIR}" --target npp_tests -- -j"$(nproc)"

echo ">>> 运行 ctest ..."
ctest --test-dir "${BUILD_DIR}" --output-on-failure
