#!/usr/bin/env bash
# 打 .deb 安装包。产物在 build-deb/ 下，文件名形如
#   noda_1.0.0-1_amd64.deb
#
# 用法：
#   ./build_deb.sh          # 打当前架构的包
#   sudo dpkg -i build-deb/*.deb
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build-deb"

echo "=== Noda Debian 打包脚本 ==="

if ! command -v cmake &> /dev/null || ! command -v cpack &> /dev/null; then
    echo "错误: 未找到 cmake/cpack，请先安装: sudo apt install cmake"
    exit 1
fi
# 包的 Depends 由 dpkg-shlibdeps 按真实链接关系生成，它随 dpkg-dev 提供
if ! command -v dpkg-shlibdeps &> /dev/null; then
    echo "错误: 未找到 dpkg-shlibdeps，请先安装: sudo apt install dpkg-dev"
    exit 1
fi

# 每次都从干净目录构建：CPack 复用 CMakeCache 里的安装前缀，
# 换过前缀的旧缓存会打出 Exec 指向错误路径的包。
rm -rf "${BUILD_DIR}"
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

# 前缀必须是 /usr，不能沿用 CMake 默认的 /usr/local：.desktop 里的 Exec 在
# configure 阶段就被固化成 ${CMAKE_INSTALL_FULL_BINDIR}/noda，
# 前缀错了装完点图标没反应。
echo ">>> 配置 CMake (CMAKE_INSTALL_PREFIX=/usr)..."
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr ..

echo ">>> 开始多线程编译..."
cmake --build . -- -j"$(nproc)"

echo ">>> 生成 deb 包..."
cpack -G DEB

DEB_FILE="$(ls -1t ./*.deb | head -1)"
echo "=== 打包完成: ${BUILD_DIR}/${DEB_FILE#./} ==="
echo ">>> 安装: sudo apt install ./$(basename "${DEB_FILE}")   # 用 apt 装可自动补齐依赖"
echo ">>> 卸载: sudo apt remove noda"
