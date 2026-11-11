# Changelog

All notable changes to **Noda** are documented here. The format is based on
[Keep a Changelog](https://keepachangelog.com/), and this project adheres to
[Semantic Versioning](https://semver.org/).

> Version numbers are bumped automatically by `npp-linux/release.sh` to match the
> Git tag; the same number is written into `npp-linux/src/Version.h` so the
> in-app "About" dialog matches the released package.

## [1.0.18] - 2026-11-11

### Fixed
- 目录树（首选项左侧目录 + 文件树）选中 / hover 底色跟随主题色，不再写死紫色。

## [1.0.17] - 2026-11-11

### Added
- 外观页主题色：7 个预设（默认紫 / macOS 蓝 / Ubuntu 橙 / GNOME 蓝 / 翠绿 / 玫瑰红 / 琥珀）+ 自定义颜色选择器，驱动选中、链接、按钮高亮与标签色条。

## [1.0.16] - 2026-11-11

### Added
- 工具栏下拉按钮复刻上游（「显示所有字符」「文件对比」），新增「选择文件比对」入口。

## [1.0.15] - 2026-11-11

### Changed
- 比对菜单收敛为子菜单，差异导航与「关闭比对」移入比对工具条。

## [1.0.14] - 2026-11-11

### Fixed
- 修复比对模式下偶发 SIGSEGV 崩溃；修复 CI 无头环境下测试沙箱隔离与单实例断言。

## [1.0.13] - 2026-11-11

### Added
- GitHub Actions 自动化工作流；代码比对功能（文档 / 选区 / Git HEAD / SVN BASE）。

## [1.0.12] - 2026-11-10

### Added
- 新增代码比对功能（基于直方图/Patience 锚点 + 有界 Myers、wyhash 块哈希、移动块检测、异步计算）。

## [1.0.11] - 2026-11-10

### Changed
- 产品名 / 图标 / 授权协议调整（由 Notepad++ 移植独立发布，GPL-3.0）。
- 优化中英文界面语言运行时切换。

---

项目仍在活跃开发中，后续版本将补齐插件体系、宏、函数列表等上游能力。
