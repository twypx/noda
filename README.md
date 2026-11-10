<p align="center">
  <img src="npp-linux/resources/icons/app_256.png" width="112" alt="Noda logo">
</p>

<h1 align="center">Noda</h1>

<p align="center"><b>The fastest native text editor for Linux.</b><br>
C++20 + Qt6 Widgets · Scintilla/Lexilla core · no Wine, no Electron, no WebView.</p>

<p align="center">
  <b>English</b> &nbsp;|&nbsp; <a href="docs/README.zh-CN.md">简体中文</a>
</p>

---

Noda is a native Linux text editor **derived from Notepad++ source code**. It keeps the upstream
assets and logic (`langs.model.xml`, `stylers.model.xml`, uchardet, Scintilla, Lexilla) but is
rewritten as a pure Qt6 Widgets application. It is **not** a Wine wrapper and **not** an Electron app.

> **Attribution.** Noda is an independent modification of Notepad++ source code, released under
> GPL-3.0. It is not affiliated with, endorsed by, or connected to Notepad++ or its author.


## What Noda adds over Notepad++

Some features overlap with upstream; the point is that they are **built in, dependency-free,
and tuned for Linux** — not bolted on through plugins or a compatibility layer.

| Capability | Notepad++ (Windows) | Noda |
|---|---|---|
| Platform | Windows only | **Native Linux** (X11 + Wayland), `.deb`, `.desktop`, hicolor icons |
| Runtime | Win32 / GDI | **C++20 + Qt6 Widgets**, ~80–120 ms cold start, ~35–45 MB RSS |
| Binary / hex view | Via the HexEditor plugin | **Built-in raw-byte viewer** — `mmap`, viewport-only reads, no full-file copy, >4 GB–safe addresses |
| Folder workspace | "Folder as Workspace" | **Built-in dock** with lazy loading, zero recursive pre-scan, custom branch/indent painting |
| UI language | `nativeLang/*.xml` | **Runtime English ⇄ 简体中文 switch**, fully bilingual menus, tooltips and status bar |
| Theme | Built-in themes | **Obsidian-inspired light/dark tokens**, unified UI font (CJK + Latin), native borderless title bar |
| Code formatting | Plugins / external tools | **Built-in AStyle (C/C++/C#/Java/Obj-C) + JSON formatter**, runs off the UI thread |
| TypeScript | By lexer config | **`.ts` / `.tsx` highlighting** reusing the cpp lexer with correct properties |
| Session & backup | Periodic backup | **Session snapshot + auto backup**, unsaved content is restored with the caret position |

## Highlights

- **Performance-first engineering.** Viewport-only smart highlighting with a hard time budget,
  font-list and theme-metadata singletons, batched tab-bar re-polish, mmap-backed hex viewer,
  and no speculative dependencies.
- **A serious, modern UI.** Tree-structured preferences, editable shortcut mapper with conflict
  detection, style configurator (per language × style slot), and only real, backed toggles —
  no placeholder switches.
- **Linux-native integration.** X11 startup-notification fix-up, `%F` file associations,
  a `register_open_with.py` helper for the desktop context menu, and correct Wayland handling.
- **Clickable URLs.** URLs are underlined in the visible viewport and open in your default
  browser on `Ctrl` + click.
- **Binary files just work.** Files containing NUL bytes automatically open in the hex viewer,
  showing the true on-disk bytes — not the decoded text buffer.

## Core features

- [x] Multi-tab editing: new / open / save / save-as / save-copy-as, unsaved-change prompts,
      double-click empty tab bar to create a document.
- [x] Tab context menu: close / batch-close, save, rename, move to trash, reload, print,
      read-only, copy path, move tab, open containing folder.
- [x] Drag-and-drop files (multi-file) and command-line / IPC open.
- [x] Native Scintilla editing: undo/redo, cut/copy/paste, select-all, column selection.
- [x] Line numbers, fold margin, current-line highlight, word-wrap toggle.
- [x] Encoding detection (UTF-8 / GBK / Big5 / UTF-16 …) and EOL detection (LF / CRLF / CR).
- [x] Fast find & replace: match case, whole word, regex, replace-all.
- [x] External file-change monitoring via `inotify` with reload prompts.
- [x] Session memory + automatic backup of unsaved content.
- [x] Code formatting: AStyle for C-family, order-preserving JSON pretty-printer, worker thread.
- [x] Style configurator: per language × style slot colours, bold/italic, font family & size.
- [x] Preferences (tree): theme, UI language, tab bar, auto-completion, highlight matching,
      UI fonts, editor behaviour — only real, backed options.
- [x] Shortcut mapper generated from real menu actions; editable, conflict detection, reset.
- [x] Opening a folder: lazy, dependency-free directory tree in a left dock.
- [x] Built-in hexadecimal / binary viewer with raw on-disk bytes.
- [x] TypeScript / TSX syntax highlighting.

## Quick start

```bash
sudo apt update
sudo apt install -y build-essential cmake qt6-base-dev qt6-base-dev-tools

cd npp-linux
./build.sh
./build/notepad-plus-plus-linux
```

Package as a `.deb`:

```bash
cd npp-linux
./build_deb.sh
sudo apt install ./build-deb/notepad-plus-plus-linux_*_amd64.deb
```

Toolbar and application icons are generated offline by `npp-linux/tools/*.py` and embedded as
Qt resources — a normal build needs no Python, no network and no system icon theme.

## Repository layout

This repository vendors every third-party source needed to build, so the directory layout is
**load-bearing**: `npp-linux/CMakeLists.txt` resolves dependencies via `NPP_ROOT = ..`.

| Path | Origin | Purpose |
|---|---|---|
| `npp-linux/` | Noda | Application: windows, editor wrapper, dialogs, theme and icon tooling |
| `scintilla/` | Upstream (vendored) | Editing engine (buffer, selection, undo, rendering) |
| `lexilla/` | Upstream (vendored) | Lexer engine and per-language lexers |
| `PowerEditor/src/uchardet/` | Upstream (vendored) | Charset auto-detection |
| `PowerEditor/src/pugixml/` | Upstream (vendored) | XML parsing for language/style models |
| `PowerEditor/src/langs.model.xml`<br>`PowerEditor/src/stylers.model.xml` | Upstream (vendored) | Language and theme definitions, reused verbatim |

## License

- Noda is released under the **GNU General Public License v3.0 (GPL-3.0)** — see [`LICENSE`](LICENSE).
  Original Notepad++ code is Copyright (C) 2021 Don HO <don.h@free.fr>.
- `scintilla/` and `lexilla/` are under Neil Hodgson's permissive licenses (see each `License.txt`).
- `PowerEditor/src/uchardet/` is triple-licensed **MPL 1.1 / GPL 2.0 / LGPL 2.1**.
- `PowerEditor/src/pugixml/` is **MIT**.

Noda is an independent project. It is not affiliated with, endorsed by, or connected to Notepad++
or its author, and it is not an official release of the Notepad++ project.
