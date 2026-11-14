# Implementation handoff

**Status date:** 2026-11-14
**Current phase:** Phase 1 vertical slice, early development
**Purpose:** Let another developer or agent continue without relying on conversation history.

## What exists

- Native C++/Qt Quick application target `rwreader`, built with qmake (`rwreader.pro`).
- `src/backend.{h,cpp}` invokes the official Readwise CLI asynchronously using `QProcess` argument arrays; it does not shell out or handle credentials.
- Current GUI actions: list Inbox/Later/Shortlist/Archive/Feed, hybrid search, local case-insensitive matching against the currently loaded documents, open a document, set a document note and tags, create and review text highlights, archive a document, and start CLI login.
- Search runs only after Enter, avoiding repeated CLI launches while the user types; Escape clears the query and returns to the current view.
- Keyboard navigation: `j/k` and arrows in the focused nav/list; Enter opens/activates; `/` focuses search; `Ctrl+K` opens the command palette; `Ctrl+←/→` changes focus between navigation and document list; `Ctrl+R` refreshes; `a` archives; `?` opens help.
- The left pane switches from library navigation to an article table of contents while reading; the article view returns to the library list through its Back button or Escape. A bottom status bar shows current operation and errors, with a compact activity spinner; list and article loading also show a large centered spinner.
- `src/theme.{h,cpp}` follows Omarchy accent color and Hyprland rounding.
- `tests/rwreader_tests.cpp` contains Qt Test coverage for fake-CLI list/feed arguments, search result merging, and Markdown formatting. `bin/build` runs `bin/test` after the application build.
- The reading view converts CLI Markdown using `QTextDocument`, then renders selectable rich text in a centered 760 px column with styled body text, headings, lists, code, and quotes.
- `prototype/reader-ux/` is the original disconnected mock. The active app is the project-root `rwreader` target.
- `docs/readwise-reader-omarchy-prd.md` contains the PRD and roadmap; `docs/phase-0-discovery.md` contains CLI/API discovery.

## Latest fixes

- The article view previously received no usable width. The root `RowLayout` now reserves the list width and allocates a minimum/preferred width to the reader pane.
- Search now merges title/metadata matches from the latest loaded list with CLI hybrid-search results. Exact local matching only covers documents currently loaded in the selected view; CLI search remains the broader search path.
- Reader content now uses custom typography and a narrower centered column instead of Qt's default Markdown presentation.
- The document list panel now expands with the available window width, and Feed is exposed as a library location supported by the existing CLI list command.
- The reader has a document-note editor backed by `reader-bulk-edit-document-metadata`; saving updates the note field for the current document.
- Document tags are edited with the same bulk metadata command. Existing highlights load with `reader-get-document-highlights`; selecting article text and choosing **Highlight selection** creates a highlight with `reader-create-highlight`.
- Article text receives focus when opened; up/down selects adjacent paragraphs and `h` highlights the current selection. `Shift+Arrow` selects by character, `Ctrl+Shift+Arrow` extends by word, and `Ctrl+Shift+H` saves the selection. Pane-switch shortcuts do not intercept arrows in reading mode.
- The UI uses Omarchy's `monospace` fontconfig alias, square Material controls, and subtle surface corners to match native Omarchy plugins.
- Modal note, tag, highlight, and help dialogs use custom square action buttons instead of Qt Material's rounded generated button boxes.
- The highlight toolbar caches the selected passage before taking focus, scoped to the open document, so mouse selection is preserved for the click action.
- Opening a document resets article scroll to the top and adds more top/bottom reading padding to prevent the first lines from appearing clipped.
- `bin/run` rebuilds the application before launch, so use it to pick up source changes.

## Build and run

```sh
./bin/build
./bin/run
```

The user has an authenticated Readwise CLI. The app uses that existing CLI login. Do not issue account calls during routine development; use a fake `readwise` executable for interaction checks unless the user explicitly asks to test against their account.

## Verification already performed

- `./bin/build` succeeds with the available Qt 6.12 toolchain. The backend suite has 5 test methods and reports 7 Qt Test passes including setup and cleanup.
- A temporary Qt Quick interaction harness outside the project used a fake CLI and verified: keyboard list movement, article activation by keyboard, sidebar navigation, live search for a term in a title, Escape from search, and article activation by click.
- The app loads offscreen with a fake CLI after QML changes. Keyboard text selection, highlight button activation, modal footers, and highlight refresh have not yet been exercised in a checked-in interaction harness.
- A grabbed offscreen mock article confirmed the article pane is visible and Markdown headings/body/list formatting render at the intended width.
- No checked-in QML interaction harness exists yet; the prior temporary harness was removed after verification.
- No authenticated Readwise account was queried during these checks.

## Known gaps and risks

- Search local matching only examines the latest loaded page (currently up to 50 docs). Results outside that page depend on CLI hybrid search; title search against a real account has not been validated here.
- The real CLI detail response has not been exercised in this project session. Markdown parsing was verified against a representative fake CLI response. If the user's document appears blank, inspect the CLI's actual `reader-get-document-details` output shape with the user's consent and adjust the parser.
- Styled Markdown is an approximation of Readwise Web. Rich inline styles and link colors are partly controlled by Qt Quick; complex HTML, remote images, embeds, PDF/EPUB layout and highlight anchoring, TTS, Ghostreader, and offline sync are not implemented.
- The app is English-only. QML/C++ strings use translation wrappers, but Omarchy localization catalogs and live language switching remain unimplemented.
- Startup, CLI spawn, detail load, and search latency have not been benchmarked against a real account.
- The local development `PKGBUILD` and launcher assets exist, but the Arch package has not been built or installed as part of this handoff.
- The feature work is committed on `main` in `86d646b` (`Second commit`). The current edits to `AGENTS.md`, `README.md`, and this handoff are documentation updates and remain uncommitted.

## Suggested next steps

1. Have the user relaunch with `./bin/run` and compare the same article with Readwise Web.
2. If typography/content still differs, inspect a sanitized representative CLI detail response and decide whether more source HTML, images, or metadata is available from supported CLI/API surfaces.
3. Validate account-backed search behavior (especially title matches beyond the loaded page) only with explicit user approval; consider adding a dedicated title-search command or pagination strategy.
4. Add offscreen QML interaction coverage for search submission, keyboard focus, activation, paragraph/word selection, highlight creation through both the shortcut and toolbar, and square modal footers. Backend regressions run through `./bin/build`.
5. Validate highlight selection and refreshed highlight responses against real article content only with explicit account-test authorization; exact matching and PDF/EPUB anchoring have not been tested against an account.
6. Compare rendered articles with Readwise Web using representative content and improve typography/HTML fidelity where supported source data allows it.
7. Review the development PKGBUILD's source/build paths before using it to install.

## Contributor instructions

Read [AGENTS.md](../AGENTS.md) first. Update it together with this handoff whenever the architecture, CLI contract, build flow, verification method, or supported capabilities change.
