# Implementation handoff

**Status date:** 2026-11-13  
**Current phase:** Phase 1 vertical slice, early development  
**Purpose:** Let another developer or agent continue without relying on conversation history.

## What exists

- Native C++/Qt Quick application target `rwreader`, built with qmake (`rwreader.pro`).
- `src/backend.{h,cpp}` invokes the official Readwise CLI asynchronously using `QProcess` argument arrays; it does not shell out or handle credentials.
- Current GUI actions: list Inbox/Later/Shortlist/Archive, hybrid search, local case-insensitive matching against the currently loaded documents, open a document, archive it, and start CLI login.
- Search waits 350 ms after typing, runs on Enter immediately, and Escape clears it and returns focus to the document list.
- Keyboard navigation: `j/k` and arrows in the focused nav/list; Enter opens/activates; `/` focuses search; `Ctrl+K` opens the command palette; `Ctrl+←/→` changes focus between navigation and document list; `Ctrl+R` refreshes; `a` archives; `?` opens help.
- `src/theme.{h,cpp}` follows Omarchy accent color and Hyprland rounding.
- The reading view converts CLI Markdown using `QTextDocument`, then renders selectable rich text in a centered 760 px column with styled body text, headings, lists, code, and quotes.
- `prototype/reader-ux/` is the original disconnected mock. The active app is the project-root `rwreader` target.
- `docs/readwise-reader-omarchy-prd.md` contains the PRD and roadmap; `docs/phase-0-discovery.md` contains CLI/API discovery.

## Latest fixes

- The article view previously received no usable width. The root `RowLayout` now reserves the list width and allocates a minimum/preferred width to the reader pane.
- Search now merges title/metadata matches from the latest loaded list with CLI hybrid-search results. Exact local matching only covers documents currently loaded in the selected view; CLI search remains the broader search path.
- Reader content now uses custom typography and a narrower centered column instead of Qt's default Markdown presentation.
- `bin/run` rebuilds the application before launch, so use it to pick up source changes.

## Build and run

```sh
./bin/build
./bin/run
```

The user has an authenticated Readwise CLI. The app uses that existing CLI login. Do not issue account calls during routine development; use a fake `readwise` executable for interaction checks unless the user explicitly asks to test against their account.

## Verification already performed

- `./bin/build` succeeds with the available Qt 6.12 toolchain.
- A temporary Qt Quick interaction harness outside the project used a fake CLI and verified: keyboard list movement, article activation by keyboard, sidebar navigation, live search for a term in a title, Escape from search, and article activation by click.
- A grabbed offscreen mock article confirmed the article pane is visible and Markdown headings/body/list formatting render at the intended width.
- No automated tests or harness were added to the project. The temporary harness was removed after verification.
- No authenticated Readwise account was queried during these checks.

## Known gaps and risks

- Search local matching only examines the latest loaded page (currently up to 50 docs). Results outside that page depend on CLI hybrid search; title search against a real account has not been validated here.
- The real CLI detail response has not been exercised in this project session. Markdown parsing was verified against a representative fake CLI response. If the user's document appears blank, inspect the CLI's actual `reader-get-document-details` output shape with the user's consent and adjust the parser.
- Styled Markdown is an approximation of Readwise Web. Rich inline styles and link colors are partly controlled by Qt Quick; complex HTML, remote images, embeds, PDF/EPUB layout, TTS, Ghostreader, highlights/notes, and offline sync are not implemented.
- The app is English-only. QML/C++ strings use translation wrappers, but Omarchy localization catalogs and live language switching remain unimplemented.
- Startup, CLI spawn, detail load, and search latency have not been benchmarked against a real account.
- The local development `PKGBUILD` and launcher assets exist, but the Arch package has not been built or installed as part of this handoff.
- Git is not initialized: the existing `.git` path is mounted read-only in the current environment. `git init` failed trying to create `.git/hooks`. `.gitignore` and this project's `AGENTS.md` are present; no files were staged.

## Suggested next steps

1. Have the user relaunch with `./bin/run` and compare the same article with Readwise Web.
2. If typography/content still differs, inspect a sanitized representative CLI detail response and decide whether more source HTML, images, or metadata is available from supported CLI/API surfaces.
3. Validate account-backed search behavior (especially title matches beyond the loaded page) only with explicit user approval; consider adding a dedicated title-search command or pagination strategy.
4. Add a checked-in fake CLI fixture and automated UI/backend tests for search and open so regressions are reproducible without account access.
5. Implement missing Phase 1 work deliberately: feed view, highlight/note actions, robust document-type handling, CLI error UX, and localization architecture.
6. Fix repository write access for `.git`, initialize Git, and review the development PKGBUILD's source/build paths before using it to install.

## Contributor instructions

Read [AGENTS.md](../AGENTS.md) first. Update it together with this handoff whenever the architecture, CLI contract, build flow, verification method, or supported capabilities change.
