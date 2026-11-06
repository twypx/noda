#!/usr/bin/env bash
# 一键发版：打 tag → 构建 deb → 推送 → 在 Gitee 建 Release 并附上 deb。
#
# 用法：
#   export GITEE_TOKEN=xxxxxxxx        # Gitee 私人令牌，勾选 projects 权限即可
#   tools/release.sh v1.0.0
#   tools/release.sh v1.0.0 --dry-run  # 只做校验与构建，不推送、不建 Release
#
# 发版前先把 npp-linux/CMakeLists.txt 里 project(... VERSION x.y.z) 改成与 tag 一致，
# 脚本会拦住不一致的情况 —— 否则会打出一个版本号对不上 tag 的包。
set -euo pipefail

TAG="${1:-}"
DRY_RUN=0
[[ "${2:-}" == "--dry-run" ]] && DRY_RUN=1

if [[ -z "$TAG" ]]; then
    sed -n '2,10p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
cd "${REPO_ROOT}"

# ---- 1. tag 格式 -----------------------------------------------------------
if [[ ! "$TAG" =~ ^v[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "错误: tag 需形如 v1.0.0，当前为 '${TAG}'" >&2
    exit 1
fi
VERSION="${TAG#v}"

# ---- 2. 版本号一致性 -------------------------------------------------------
# deb 的版本由 CMake 的 project(VERSION) 决定，tag 只是个标签。两者一旦不同步，
# Release 上就会出现「tag 叫 v1.2.0、包却是 1.0.0-1」这种事故，必须拦住。
CMAKE_VER="$(sed -n '/^project(/,/)/p' npp-linux/CMakeLists.txt \
    | grep -oE 'VERSION +[0-9]+\.[0-9]+\.[0-9]+' | head -1 | awk '{print $2}')"
if [[ "$CMAKE_VER" != "$VERSION" ]]; then
    echo "错误: tag ${TAG} 与 CMakeLists 的 VERSION ${CMAKE_VER:-<未找到>} 不一致" >&2
    echo "      先改 npp-linux/CMakeLists.txt 里的 VERSION 为 ${VERSION} 并提交" >&2
    exit 1
fi

# ---- 3. 解析 Gitee 仓库 ----------------------------------------------------
REMOTE_URL="$(git remote get-url origin 2>/dev/null || true)"
if [[ "$REMOTE_URL" != *gitee.com* ]]; then
    echo "错误: origin 不是 Gitee 仓库（当前: ${REMOTE_URL:-<未设置>}）" >&2
    exit 1
fi
REMOTE_URL="${REMOTE_URL%.git}"
OWNER="$(echo "$REMOTE_URL" | awk -F'[:/]' '{print $(NF-1)}')"
REPO="$(echo "$REMOTE_URL" | awk -F'[:/]' '{print $NF}')"
API="https://gitee.com/api/v5/repos/${OWNER}/${REPO}"
echo ">>> 仓库: ${OWNER}/${REPO}    tag: ${TAG}"

# ---- 4. 同步远端状态 -------------------------------------------------------
# 必须连 tag 一起拉：如果 tag 是在 Gitee 网页上打的，本地没有它，后面
# git tag -a 会重复创建并导致 push 被拒。拉下来之后第 5 步就能识别成“已存在”。
git fetch -q origin --tags 2>/dev/null || true

# 本地不能有未推送的提交：否则 tag 指向的 commit 在远端不存在，Release 挂上去打不开。
if [[ -n "$(git log origin/master..HEAD --oneline 2>/dev/null)" ]]; then
    echo "错误: 存在尚未推送的提交，请先 git push origin master" >&2
    git log origin/master..HEAD --oneline | sed 's/^/      /' >&2
    exit 1
fi

# ---- 5. 打 tag -------------------------------------------------------------
if git rev-parse -q --verify "refs/tags/${TAG}" >/dev/null; then
    echo ">>> tag ${TAG} 已存在，复用"
elif [[ $DRY_RUN -eq 1 ]]; then
    # dry-run 不改动仓库状态：只报告将要做什么
    echo ">>> [dry-run] 将创建 tag ${TAG}"
else
    git tag -a "${TAG}" -m "Notepad++ Linux Port ${TAG}"
    echo ">>> 已创建 tag ${TAG}"
fi

# ---- 6. 构建 deb -----------------------------------------------------------
echo ">>> 开始构建 deb（全量重编，约数分钟）..."
./npp-linux/build_deb.sh
DEB="$(ls -1t ./npp-linux/build-deb/*.deb | head -1)"
DEB_NAME="$(basename "$DEB")"

# 再核对一次实际产物的版本，防止 CMake 缓存导致打出旧版本
DEB_VERSION="$(dpkg-deb -f "$DEB" Version)"
if [[ "$DEB_VERSION" != "${VERSION}-1" ]]; then
    echo "错误: deb 版本为 ${DEB_VERSION}，期望 ${VERSION}-1" >&2
    exit 1
fi
echo ">>> 产物: ${DEB_NAME} ($(du -h "$DEB" | cut -f1))"

if [[ $DRY_RUN -eq 1 ]]; then
    echo ">>> --dry-run：跳过推送与 Release 上传，以上校验全部通过"
    exit 0
fi

# ---- 7. 推送 tag -----------------------------------------------------------
git push origin "${TAG}"

# ---- 8. 准备令牌 -----------------------------------------------------------
if [[ -z "${GITEE_TOKEN:-}" ]]; then
    echo "错误: 请先设置 GITEE_TOKEN 环境变量。" >&2
    echo "      在 https://gitee.com/profile/personal_access_tokens 生成，勾选 projects 权限" >&2
    exit 1
fi

# Release 说明：取上个 tag 以来的提交标题，避免手写
PREV_TAG="$(git describe --tags --abbrev=0 "${TAG}^" 2>/dev/null || true)"
if [[ -n "$PREV_TAG" ]]; then
    BODY="$(git log --pretty=format:'- %s' "${PREV_TAG}..${TAG}")"
else
    BODY="$(git log --pretty=format:'- %s' -n 20 "${TAG}")"
fi

# ---- 9. 创建或复用 Release --------------------------------------------------
json_field() { python3 -c 'import sys,json;print(json.load(sys.stdin).get(sys.argv[1],"") or "")' "$1"; }

RELEASE_ID="$(curl -sS "${API}/releases/tags/${TAG}?access_token=${GITEE_TOKEN}" 2>/dev/null \
    | json_field id 2>/dev/null || true)"

if [[ -n "$RELEASE_ID" ]]; then
    echo ">>> Release ${TAG} 已存在（id=${RELEASE_ID}），将追加附件"
else
    RESP="$(curl -sS -X POST "${API}/releases" \
        --data-urlencode "access_token=${GITEE_TOKEN}" \
        --data-urlencode "tag_name=${TAG}" \
        --data-urlencode "name=Notepad++ Linux Port ${TAG}" \
        --data-urlencode "body=${BODY}" \
        --data-urlencode "target_commitish=master" \
        --data-urlencode "prerelease=false")"
    RELEASE_ID="$(echo "$RESP" | json_field id)"
    if [[ -z "$RELEASE_ID" ]]; then
        echo "错误: 创建 Release 失败，接口返回：" >&2
        echo "$RESP" >&2
        exit 1
    fi
    echo ">>> 已创建 Release ${TAG} (id=${RELEASE_ID})"
fi

# ---- 10. 上传 deb ----------------------------------------------------------
echo ">>> 上传 ${DEB_NAME} ..."
UPLOAD="$(curl -sS -X POST "${API}/releases/${RELEASE_ID}/attach_files" \
    -F "access_token=${GITEE_TOKEN}" \
    -F "file=@${DEB}")"
DOWNLOAD_URL="$(echo "$UPLOAD" | json_field browser_download_url)"
if [[ -z "$DOWNLOAD_URL" ]]; then
    echo "错误: 附件上传失败，接口返回：" >&2
    echo "$UPLOAD" >&2
    exit 1
fi

echo "=== 发布完成 ==="
echo "    Release : https://gitee.com/${OWNER}/${REPO}/releases/tag/${TAG}"
echo "    下载    : ${DOWNLOAD_URL}"
