# Contributor guide

## Project

`rwreader` is an early native Readwise Reader client for Omarchy. It is a C++/Qt Quick app built with qmake. The initial functional slice lists and searches Reader documents, opens Markdown details, and archives a document through the official `readwise` CLI.

## Build and launch

- Build: `./bin/build`
- Run from this checkout: `./bin/run` (rebuilds before launch)
- Install/package: `./bin/install` invokes the local development PKGBUILD. Review the packaging paths before changing or invoking it.
- The app expects `readwise` on `PATH`. CLI login is user-managed with `readwise login`; the app invokes login only when its Connect button is selected.

## Architecture

- `src/backend.{h,cpp}` owns app state and calls the CLI asynchronously with `QProcess`. Keep invocations on a fixed allowlist, pass argument arrays (never a shell string), and do not pass credentials in arguments.
- `src/Main.qml` owns layout and interaction. Keep library/document navigation keyboard-accessible, preserve focus behavior, and update the shortcut overlay and README when bindings change.
- `src/theme.{h,cpp}` follows Omarchy's accent color and Hyprland rounding.
- `prototype/reader-ux/` is the disconnected mock-data prototype from Phase 0; the active application is at the project root.
- `docs/readwise-reader-omarchy-prd.md` is the product plan. `docs/phase-0-discovery.md` records API/CLI findings and known capability limits.
- `docs/implementation-status.md` is the current handoff: what works, what was verified, known gaps, and next steps. Update it as the implementation moves forward.

## Working conventions

- Keep C++ responsible for state, CLI/process work, parsing, and failures; keep QML responsible for view and interaction.
- Do not query a real Readwise account as part of routine development. Use a fake `readwise` executable for CLI/UI smoke checks unless the user explicitly asks to exercise their account.
- Do not claim feature parity where the CLI/API or document renderer has not been verified. In particular, PDF/EPUB highlighting, TTS, Ghostreader, offline sync, and advanced Reader features remain out of scope for the current slice.
- Keep visible QML strings wrapped in `qsTr()` and C++ UI strings in `tr()`. The current interface is English-only; add and test Omarchy language catalogs before claiming localization support.
- The Reader pane must receive an explicit minimum/preferred width in the root `RowLayout`; otherwise the document list can consume the remaining width and make a successful open look like a no-op. For reader-layout changes, inspect a grabbed window image after opening a mock document.
- The reading body is converted from Markdown with `QTextDocument` styling, shown as selectable rich text in a centered 760 px column. When changing this renderer, check headings, body paragraphs, links, lists, blockquotes, and code with a multi-block mock article; QML rich text may override some link styling.
- Search combines CLI hybrid-search results with case-insensitive matches from the currently loaded document list so a visible title match is not lost to semantic ranking.
- Keep generated qmake output under `build/`; do not commit binaries or generated build files.
- There is no checked-in automated test suite yet. Build after code changes with `./bin/build`. For interaction changes, use a mock CLI and exercise keyboard focus, activation, search, and reader navigation before considering the change complete.

## Keep this guide current

When the architecture, build/install workflow, CLI contract, supported features, or verification process changes, update this file in the same change so a new contributor can pick up the project without relying on chat history.
