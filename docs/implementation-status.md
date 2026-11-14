# Implementation handoff

**Status date:** 2026-11-14
**Current phase:** Phase 1 vertical slice, early development
**Purpose:** Let another developer or agent continue without relying on conversation history.

## What exists

- Native C++/Qt Quick application target `rwreader`, built with qmake (`rwreader.pro`).
- `src/backend.{h,cpp}` invokes the official Readwise CLI asynchronously using `QProcess` argument arrays; it does not shell out or handle credentials.
- Current GUI actions: list Inbox/Later/Shortlist/Archive/Feed, hybrid search, local case-insensitive matching against the currently loaded documents, inspect selected-document metadata in the library or reading view, open an article separately, edit document notes/tags and individual highlight notes/tags, create and review text highlights, archive a document, and start CLI login.
- Search runs only after Enter, avoiding repeated CLI launches while the user types; Escape clears the query and returns to the current view.
- Keyboard navigation: `j/k` and arrows in the focused nav/list; Enter opens/activates; `/` focuses search; `Ctrl+K` opens the command palette; `Ctrl+←/→` changes focus between navigation and document list; `Ctrl+R` refreshes; `a` archives; `?` opens help.
- The left pane switches from library navigation to an article table of contents while reading; the article view returns to the library list through its Back button or Escape. A bottom status bar shows current operation and errors, with a compact activity spinner; list and article loading also show a large centered spinner.
- `src/theme.{h,cpp}` follows Omarchy accent color and Hyprland rounding.
- `tests/rwreader_tests.cpp` contains Qt Test coverage for fake-CLI backend behavior and an offscreen QML interaction path. `bin/build` runs the suite after the application build.
- The reading view converts CLI Markdown using `QTextDocument`, then renders selectable rich text in a centered 760 px column with styled body text, headings, lists, code, and quotes.
- `prototype/reader-ux/` is the original disconnected mock. The active app is the project-root `rwreader` target.
- `docs/readwise-reader-omarchy-prd.md` contains the PRD and roadmap; `docs/phase-0-discovery.md` contains CLI/API discovery.

## Latest fixes

- The article view previously received no usable width. The root `RowLayout` now reserves the list width and allocates a minimum/preferred width to the reader pane.
- Search now merges title/metadata matches from the latest loaded list with CLI hybrid-search results. Exact local matching only covers documents currently loaded in the selected view; CLI search remains the broader search path.
- Search also runs the CLI's explicit `--title-search` filter after hybrid search, then merges unique IDs across loaded-page, hybrid, and title results. This reduces dependence on semantic ranking and loaded-page limits; real-account coverage remains unverified.
- Reader content now uses custom typography and a narrower centered column instead of Qt's default Markdown presentation.
- The document list panel now expands with the available window width, and Feed is exposed as a library location supported by the existing CLI list command.
- The right-side metadata pane remains visible beside the list or article and uses compact **Info** and **Notebook** tabs. Info shows list-provided title, author, summary, tags, and available metadata. Notebook holds the document note and highlights. Selecting a list item does not start a detail request; the list stays navigable. Notebook highlights load lazily, show a panel-local spinner, and are cached in memory for the app session. Document tags appear as removable badges and document notes/tags are edited inline through `reader-bulk-edit-document-metadata`.
- Article details and highlights are cached per document for the app session after opening. Each highlight shows its text; hover reveals controls for editing its note and tags, with note editing expanded inline. Updates use `reader-set-highlight-notes` and the corresponding add/remove tag CLI commands. Selecting article text and choosing **Highlight selection** creates a highlight with `reader-create-highlight`.
- Article text receives focus when opened; up/down selects adjacent paragraphs and `h` highlights the current selection. `Shift+Arrow` selects by character, `Ctrl+Shift+Arrow` extends by word, and `Ctrl+Shift+H` saves the selection. Pane-switch shortcuts do not intercept arrows in reading mode.
- The UI uses Omarchy's `monospace` fontconfig alias, square Material controls, and subtle surface corners to match native Omarchy plugins.
- Metadata editing is inline in the always-visible pane. Highlight note and tag controls appear on hover, and the note editor expands inline; the help dialog remains a separate overlay.
- The highlight toolbar caches the selected passage before taking focus, scoped to the open document, so mouse selection is preserved for the click action.
- Opening a document resets article scroll to the top and adds more top/bottom reading padding to prevent the first lines from appearing clipped.
- Document details accept raw Markdown, object-shaped JSON with known Markdown fields, and JSON string output.
- `bin/run` rebuilds the application before launch, so use it to pick up source changes.

## Build and run

```sh
./bin/build
./bin/run
```

The user has an authenticated Readwise CLI. The app uses that existing CLI login. Do not issue account calls during routine development; use a fake `readwise` executable for interaction checks unless the user explicitly asks to test against their account.

## Verification already performed

- `./bin/build` passes with the available Qt 6.12 toolchain. The suite has 10 test methods and reports 12 Qt Test passes including setup and cleanup.
- The repository's offscreen QML interaction test uses a fake CLI and verifies search submission, keyboard document activation, the Open action, article focus and paragraph/word selection, highlight creation through the shortcut and toolbar, and inline document note/tag editing. Backend tests cover document moves and highlight note/tag commands.
- `bin/test` sets `QT_QPA_PLATFORM=offscreen` and `QT_QPA_PLATFORMTHEME=generic` so GUI checks do not depend on the host desktop session.
- The sign-in prompt is hidden normally and appears only when CLI error text indicates an authentication failure; it launches the official CLI OAuth flow when selected. The app does not inspect CLI credential files or handle the token.
- The PKGBUILD keeps generated qmake output under the checkout's `build/` directory. A package was assembled from an isolated temporary copy and contains the executable, desktop entry, icon, and license; it was not installed.
- A grabbed offscreen mock article confirmed the article pane is visible and Markdown headings/body/list formatting render at the intended width.
- No authenticated Readwise account was queried during these checks.

## Known gaps and risks

- Search local matching only examines the latest loaded page (currently up to 50 docs). Results outside that page depend on CLI hybrid search; title search against a real account has not been validated here.
- The real CLI detail response has not been exercised in this project session. Markdown parsing was verified against a representative fake CLI response. If the user's document appears blank, inspect the CLI's actual `reader-get-document-details` output shape with the user's consent and adjust the parser.
- Styled Markdown is an approximation of Readwise Web. Rich inline styles and link colors are partly controlled by Qt Quick; complex HTML, remote images, embeds, PDF/EPUB layout and highlight anchoring, TTS, Ghostreader, and offline sync are not implemented.
- The app is English-only. QML/C++ strings use translation wrappers, but Omarchy localization catalogs and live language switching remain unimplemented.
- Startup, CLI spawn, detail load, and search latency have not been benchmarked against a real account.
- The local development package has been assembled successfully, but it has not been installed or launched from the installed package.
- The original feature work is committed on `main` in `86d646b` (`Second commit`), and the earlier contributor-guide/handoff updates are committed in `49b8df2` (`Docs update`). Current continuation changes to search, detail parsing, QML test hooks, and packaging are uncommitted.

## Suggested next steps

1. Have the user relaunch with `./bin/run` and compare the same article and modal styling with Readwise Web.
2. If typography/content differs, inspect a sanitized representative CLI detail response and decide whether more source HTML, images, or metadata is available from supported CLI/API surfaces.
3. Validate account-backed search behavior (especially title matches beyond the loaded page) only with explicit user approval; the dedicated title-search pass is implemented but its real-account coverage is unverified.
4. Validate highlight selection and refreshed highlight responses against real article content only with explicit account-test authorization; exact matching and PDF/EPUB anchoring have not been tested against an account.

## Contributor instructions

Read [AGENTS.md](../AGENTS.md) first. Update it together with this handoff whenever the architecture, CLI contract, build flow, verification method, or supported capabilities change.
