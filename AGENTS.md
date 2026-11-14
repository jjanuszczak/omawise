# Contributor guide

## Project

`Omawise` is an early native Readwise Reader client for Omarchy. It is a C++/Qt Quick app built with qmake (`omawise.pro`, executable/package `omawise`). It lists and searches Reader documents, opens Markdown details, edits document notes and tags, creates/reviews text highlights, and archives documents through the official `readwise` CLI.

## Build and launch

- Build: `./bin/build`
- Run from this checkout: `./bin/run` (rebuilds before launch)
- Package without installing: `./bin/package` invokes the checkout PKGBUILD with `makepkg`.
- Install from this checkout: `./bin/install` invokes the PKGBUILD with `makepkg -fsi`. Review the packaging paths before changing or invoking it.
- GitHub releases: update `pkgver` in `PKGBUILD`, commit, and push a matching `v<version>` tag. `.github/workflows/release.yml` builds/tests in Arch Linux and attaches the package to a GitHub Release. Releases currently target x86_64 Arch Linux/Omarchy.
- Both package scripts set makepkg's `BUILDDIR` under ignored `build/makepkg/`; this is required because the default `src/` build directory would collide with the app's source folder, especially under `--cleanbuild`.
- The app expects `readwise` on `PATH`. CLI login is user-managed with `readwise login`; the app offers that flow only after a CLI authentication error.

## Architecture

- `src/backend.{h,cpp}` owns app state and calls the CLI asynchronously with `QProcess`. Keep invocations on a fixed allowlist, pass argument arrays (never a shell string), and do not pass credentials in arguments. Document location actions use `reader-move-documents` with only Inbox (`new`), Later, or Archive destinations.
- `src/Main.qml` owns layout and interaction. The selected document's compact metadata pane stays visible beside the list or article; it has Info and Notebook tabs, with document notes/highlights in Notebook and hover-revealed highlight edit controls. List selection uses fields already present in the list response; highlights load lazily in Notebook and are cached for the app session. Metadata loading must not disable list navigation or cover the list with a blocking overlay. Keep library/document navigation keyboard-accessible, preserve focus behavior, and update the shortcut overlay and README when bindings change.
- `src/theme.{h,cpp}` follows Omarchy's accent color and reads Hyprland rounding; the UI uses Omarchy's `monospace` fontconfig alias and square Material controls, with only subtle rounding on small surfaces.
- App identity is Omawise. `assets/omawise-mark.svg` is the shared app mark, embedded from `src/resources.qrc` and installed by `PKGBUILD`; `marketing/omawise-logo.svg` and `marketing/omawise-logo-light.svg` are dark/light horizontal wordmarks for promotional use. The README selects between them with a GitHub `prefers-color-scheme` picture element.
- `prototype/reader-ux/` is the disconnected mock-data prototype from Phase 0; the active application is at the project root.
- `docs/readwise-reader-omarchy-prd.md` is the product plan. `docs/phase-0-discovery.md` records API/CLI findings and known capability limits.
- `docs/implementation-status.md` is the current handoff: what works, what was verified, known gaps, and next steps. Update it as the implementation moves forward.

## Working conventions

- Keep C++ responsible for state, CLI/process work, parsing, and failures; keep QML responsible for view and interaction.
- Do not query a real Readwise account as part of routine development. Use a fake `readwise` executable for CLI/UI smoke checks unless the user explicitly asks to exercise their account.
- Do not claim feature parity where the CLI/API or document renderer has not been verified. In particular, PDF/EPUB highlighting, TTS, Ghostreader, offline sync, and advanced Reader features remain out of scope for the current slice.
- Keep visible QML strings wrapped in `qsTr()` and C++ UI strings in `tr()`. The current interface is English-only; add and test Omarchy language catalogs before claiming localization support.
- The Reader pane and always-visible metadata pane must receive explicit minimum/preferred widths in the root `RowLayout`; otherwise the document list can consume the remaining width. For reader-layout changes, inspect a grabbed window image with a mock document selected and with its article open.
- The reading body is converted from Markdown with `QTextDocument` styling, shown as selectable rich text in a centered 760 px column. When changing this renderer, check headings, body paragraphs, links, lists, blockquotes, and code with a multi-block mock article; QML rich text may override some link styling.
- Article hyperlinks are activated from the selectable rich-text `TextArea`; route them through `Backend::openExternalUrl`, which permits only HTTP, HTTPS, and mailto schemes.
- Search is submitted with Enter rather than on every keystroke. It combines CLI hybrid-search results, a CLI title-search pass, and case-insensitive matches from the currently loaded document list. Preserve all three paths so title matches are not lost to semantic ranking or page limits.
- The reader's right metadata pane shows document fields, edits document notes/tags with `reader-bulk-edit-document-metadata`, and edits highlight notes/tags with `reader-set-highlight-notes`, `reader-add-tags-to-highlight`, and `reader-remove-tags-from-highlight`. Keep these commands on the backend allowlist, use argument arrays, and preserve list-provided metadata when detail output omits it. Article highlights use `reader-get-document-highlights` and `reader-create-highlight` with the selected passage. Do not promise precise PDF/EPUB highlight anchoring.
- When reading, give article text keyboard focus. Up/down selects adjacent article paragraphs and `h` highlights the current selection; Shift+Arrow selects by character and Ctrl+Shift+Arrow extends selection by word. Keep pane-switch shortcuts from intercepting word navigation while reading; `Ctrl+Shift+H` saves the selection as a highlight.
- The **Highlight selection** button caches the current selection before it takes focus; keep that cached passage scoped to the currently open document so the toolbar action and keyboard shortcut use the same text.
- Keep generated qmake output under `build/`, including the local PKGBUILD build; do not commit binaries or generated build files.
- The release PKGBUILD packages the exact tagged checkout. Keep `pkgver` aligned with release tags (`v<pkgver>`), and keep runtime/build dependencies current in `PKGBUILD` and the GitHub workflow.
- `./bin/build` builds the app and runs the Qt Test backend and offscreen QML interaction suite through `./bin/test`. The suite uses a fake `readwise` executable; do not query a real account in routine development. Expand the tests as backend behavior changes. For QML interaction changes, cover keyboard focus, activation, search, reader navigation, highlight actions, and affected metadata-pane controls with the mock CLI.

## Keep this guide current

When the architecture, build/install workflow, CLI contract, supported features, or verification process changes, update this file in the same change so a new contributor can pick up the project without relying on chat history.
