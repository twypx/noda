# Contributing to Noda

Thanks for considering contributing to **Noda**. Noda is an independent port of the Notepad++
source code, whose goal is to be a **peak-performance** native Linux text/source editor.
This document explains how to build locally, the development rules, and how to find
beginner-friendly tasks.

> **Attribution notice.** Noda is an independent modification of Notepad++ source code, released
> under GPL-3.0. It is **not affiliated with, endorsed by, or connected to Notepad++ or its author**,
> and is not an official Notepad++ release. By submitting a patch you agree to license your changes
> under the same terms.

## 1. Build environment

Dependencies: `build-essential`, `cmake`, Qt6 (`qt6-base-dev` + `qt6-base-dev-tools`),
`libxcb1-dev` (X11 startup-notification fix-up; optional but recommended).

```bash
sudo apt update
sudo apt install -y build-essential cmake qt6-base-dev qt6-base-dev-tools libxcb1-dev

cd npp-linux
./build.sh            # output: ./build/noda
./build/noda
```

Package a `.deb`: `./build_deb.sh`, then `sudo apt install ./build-deb/noda_*_amd64.deb`.
Package an **AppImage**: `./build_appimage.sh` (downloads the linuxdeploy toolchain on first run).

## 2. Development rules (must follow)

See the repository root [`AGENTS.md`](AGENTS.md) for the full list. Highlights:

1. **Performance-first.** Any module that misses the performance bar is better left undone than shipped as a compromise.
2. **TDD flow.** Write the test first, then the implementation; AI/human-generated code must pass tests; no module advances until its tests pass.
3. **No silent failure.** Any downgrade must surface a user-visible notice, and the program state must remain assertable.
4. **No data loss.** A failed save must never `SETSAVEPOINT`, never clear the dirty flag, never leave a half-written file (transactional write).
5. **Single source of truth.** The shortcut table is collected from real `QAction`s — never maintain a second copy.
6. **Don't touch vendored code.** `scintilla/`, `lexilla/`, `PowerEditor/src/`, `third_party/astyle/` are read-only.
7. **No speculative dependencies.** Do not introduce any third-party dependency that would jeopardize the performance goal.

## 3. Testing

```bash
cd npp-linux
./tests/run_tests.sh          # build & run npp_tests only, avoids the long LTO link of the main app
ctest --test-dir build --output-on-failure
```

When adding a feature, add the corresponding case under `tests/`. Pure logic (serialization,
path probing, conflict detection, …) should be decoupled from QWidget so it is unit-testable.

## 4. Where to start: `good first issue`

Beginner-friendly tasks usually share these traits — feel free to claim one and tag it
**`good first issue`**:

- Fix an interface string / missing localization (see the dictionary in `utils/LocalizationManager.cpp`).
- Add a boundary case for an existing feature (empty input, huge files, invalid encoding).
- Wire up real logic behind a UI control that currently has "UI but no logic" (registered in the plan).
- Improve a doc or example.

Before submitting, please confirm:

- [ ] `./tests/run_tests.sh` passes locally;
- [ ] changes don't touch `scintilla/`, `lexilla/`, `PowerEditor/src/`, `third_party/`;
- [ ] if the change affects user-visible behavior, update `CHANGELOG.md`;
- [ ] commit message follows the existing style (`feat:` / `fix:` / `refactor:` / `docs:`, with a Chinese summary).

中文版贡献指南见 **[docs/CONTRIBUTING.zh-CN.md](docs/CONTRIBUTING.zh-CN.md)**.

## 5. Versioning & release

The version number is bumped automatically by `npp-linux/release.sh` from the Git tag; the same
number is written into `CMakeLists.txt` and `src/Version.h` so the in-app "About" dialog matches
the released package. Don't edit the version in multiple places by hand.

```bash
export GITEE_TOKEN=xxxxxxxx
./npp-linux/release.sh v1.0.19     # bump version → build deb → push tag → attach deb to Gitee Release
```
