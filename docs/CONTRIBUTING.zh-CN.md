# 为 Noda 做贡献（中文版）

感谢你考虑为 **Noda** 做贡献。Noda 是 Notepad++ 源代码的独立移植，目标是做一个
**极致高性能**的 Linux 原生文本/源码编辑器。本文件说明如何本地构建、开发规范，以及
如何找到适合入门的任务。

> **关联声明**：Noda 是对 Notepad++ 源代码的独立修改，基于 GPL-3.0 发布，**与原作者及
> Notepad++ 官方无任何隶属、背书或关联**。提交补丁即表示你同意以相同许可证发布你的改动。
> 英文版见仓库根 [`CONTRIBUTING.md`](../CONTRIBUTING.md)。

## 1. 构建环境

依赖：`build-essential`、`cmake`、Qt6（`qt6-base-dev` + `qt6-base-dev-tools`）、
`libxcb1-dev`（X11 启动通知补漏，可选但推荐）。

```bash
sudo apt update
sudo apt install -y build-essential cmake qt6-base-dev qt6-base-dev-tools libxcb1-dev

cd npp-linux
./build.sh            # 产物：./build/noda
./build/noda
```

打包 `.deb`：`./build_deb.sh`，随后 `sudo apt install ./build-deb/noda_*_amd64.deb`。
打包 **AppImage**：`./build_appimage.sh`（首次运行会按需下载 linuxdeploy 工具链）。

## 2. 开发规范（务必遵守）

详见仓库根的 [`AGENTS.md`](../AGENTS.md)，要点：

1. **高性能第一原则**：任何性能不达标的模块宁可不做，也不将就。
2. **TDD 流程**：先写测试、再写实现；AI/人工生成的代码必须通过测试；测试不通过不得进入下一模块。
3. **不静默失败**：任何降级都必须有用户可见的提示，且程序状态可断言。
4. **不丢数据**：保存失败绝不 `SETSAVEPOINT`、绝不清除脏标记、绝不留下半成品文件（事务写）。
5. **单一事实来源**：快捷键表从真实 `QAction` 收集，避免维护第二份清单。
6. **不改动 vendored 代码**：`scintilla/`、`lexilla/`、`PowerEditor/src/`、`third_party/astyle/` 只读。
7. **禁止投机性依赖**：不引入任何影响高性能目标的第三方依赖。

## 3. 测试

```bash
cd npp-linux
./tests/run_tests.sh          # 只构建并运行 npp_tests，避免主程序 LTO 长链接
ctest --test-dir build --output-on-failure
```

新增功能请同步补充 `tests/` 下的用例；纯逻辑（序列化、路径探测、冲突检测等）尽量从
QWidget 中抽离以便单元测试。

## 4. 从哪里开始：`good first issue`

适合入门的任务通常具备以下特征，欢迎认领并打上 **`good first issue`** 标签：

- 修复一个界面文案 / 本地化缺失（参见 `utils/LocalizationManager.cpp` 的词典）。
- 补全某个已有功能的边界用例（空输入、超大文件、非法编码）。
- 为某个对话框的某个控件接上真实逻辑（仓库里偶有「有 UI 无逻辑」的占位项，已在计划中登记）。
- 改进一段文档或示例。

提交前请确认：

- [ ] 本地 `./tests/run_tests.sh` 全绿；
- [ ] 改动未触碰 `scintilla/`、`lexilla/`、`PowerEditor/src/`、`third_party/`；
- [ ] 涉及用户可见行为时同步更新 `CHANGELOG.md`；
- [ ] 提交信息遵循现有风格（`feat:` / `fix:` / `refactor:` / `docs:`，附中文简述）。

## 5. 版本与发版

版本号由 `npp-linux/release.sh` 依据 Git tag 自动改写（同时写入 `CMakeLists.txt` 与
`src/Version.h`）。请勿手动在多处改版本，避免 Release 包与二进制内版本号不一致。

```bash
export GITEE_TOKEN=xxxxxxxx
./npp-linux/release.sh v1.0.19     # 自动改版本 → 构建 deb → 推送 tag → Gitee Release 附 deb
```
