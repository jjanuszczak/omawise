# Implementation handoff

**Status date:** 2026-11-15
**Current phase:** Phase 1 vertical slice, early development
**Purpose:** Let another developer or agent continue without relying on conversation history.

## What exists

- Article formatting is configurable through `~/.config/omawise/reader.toml`, with live reload and bounded fallback values. The `Aa` reader appearance dialog has pickers for article typeface, text size, line spacing, paragraph spacing, and content width; changes are atomically written to TOML and applied while the app runs. Defaults are 12 px text, 1.2 line spacing, and 18 pt between paragraphs, retaining Omawise's current dark colors, Omarchy accent links, and monospace typeface. The 760 px reading column remains configurable.
- The Arch package includes a TOML example and the `omawise-reader-settings` agent skill under `/usr/share/omawise/`.
- The article toolbar uses compact Inbox/Later/Archive icons that move the selected article, an `Aa` appearance editor, and an overflow menu with **Copy Markdown** (including YAML frontmatter), highlight, and **Open in Reader** actions.

- Native C++/Qt Quick application target `omawise`, built with qmake (`omawise.pro`); the Arch package and desktop application are also named Omawise.
- `src/backend.{h,cpp}` invokes the official Readwise CLI asynchronously using `QProcess` argument arrays; it does not shell out or pass CLI credentials.
- Article bodies and first library pages are cached in memory and persistently under Qt's application data directory. Atomic JSON entries expire after 30 days for articles and 24 hours for library pages; the shared cap is 100 MiB, with 16 MiB per article and 2 MiB per library page. The command palette clears article bodies. Cached article text and list rows render while fresh API/CLI requests run; failed refreshes retain the last saved data. Credentials, document notes, and highlights are not persisted in these cache files.
- Optional direct API authentication validates a Readwise access token and stores it in the desktop Secret Service keyring. With a token connected, list, cursor pagination, and article-detail requests use the Reader API; without one, they retain the CLI fallback. Hybrid search, highlight retrieval, and annotation/write operations still require CLI authentication.
- Current GUI actions: list Inbox/Later/Shortlist/Archive/Feed, hybrid search, local case-insensitive matching against the currently loaded documents, inspect selected-document metadata in the library or reading view, open an article separately, edit document notes/tags with the full Reader tag list available on demand, edit individual highlight notes/tags, create and review text highlights, archive a document, and start CLI login.
- Search runs only after Enter, avoiding repeated CLI launches while the user types; Escape clears the query and returns to the current view.
- Keyboard navigation: `j/k` and arrows in the focused nav/list; Enter opens/activates; `/` focuses search; `Ctrl+K` opens the command palette; `Ctrl+←/→` changes focus between navigation and document list; `Ctrl+R` refreshes; `a` archives; `?` opens help.
- The left pane switches from library navigation to an article table of contents while reading; the article view returns to the library list through its Back button or Escape. A bottom status bar shows current operation and errors, with a compact activity spinner. Article loading stays inline and does not cover the reading surface. The library list supports both single-click selection and double-click opening.
- `src/theme.{h,cpp}` follows Omarchy accent color and Hyprland rounding.
- `tests/omawise_tests.cpp` contains Qt Test coverage for fake-CLI backend behavior and an offscreen QML interaction path. `bin/build` runs the suite after the application build.
- The reading view converts Markdown and HTML fragments using `QTextDocument`, then renders selectable rich text in a centered 760 px column inside one scrollable text surface, with styled body text, headings, lists, code, and quotes.
- `prototype/reader-ux/` is the original disconnected mock. The active app is the project-root `omawise` target.
- `docs/readwise-reader-omarchy-prd.md` contains the PRD and roadmap; `docs/phase-0-discovery.md` contains CLI/API discovery.

## Latest fixes

- `feat/pagination-cache-account-validation` implements cursor pagination through both the CLI and direct API paths. The backend appends subsequent 50-document pages and deduplicates by document ID. Read-only account checks confirmed two 50-document Inbox pages through each path with zero duplicate IDs. The fake-CLI suite covers cursor argument forwarding, append behavior, and duplicate removal.
- `feat/issue-3-persistent-cache-latency` adds atomic persistent article and first-page library caches, background refresh, account-switch cache clearing, and highlight refresh before annotation writes. Cached library rows omit document notes/tags; credentials and highlights are never persisted.
- qmake now enables Qt Quick ahead-of-time cache generation for both the app and tests. With compiled QML, 21 offscreen samples measured startup-to-library-frame at 71 ms p50 / 75 ms p95, cached article open-to-next-frame at 15 ms p50 / 19 ms p95, cached library-refresh-to-row-property at 1 ms p50 / 1 ms p95, and article-cache-read-to-content-property at 1 ms p50 / 2 ms p95. These are offscreen fake-CLI results; confirm on Omarchy hardware.
- Historical read-only performance sample on 2026-11-14 with Readwise CLI 0.5.9: five `readwise --version` process starts had a 544 ms median (539–565 ms); three Inbox list calls (50 rows) had a 2,817 ms median (2,711–3,437 ms); three low-match searches had a 3,255 ms median (2,146–4,315 ms); and three detail calls had a 3,427 ms median (2,492–3,848 ms). A new seven-run sample on 2026-11-15 measured CLI Inbox list at 2,238.5 ms p50 / 3,035.1 ms p95 and detail at 1,924.6 ms p50 / 2,576.1 ms p95. These include process startup and network latency; they are not GUI timings.
- Interleaved five-run comparison using the project-local `READWISE_API_TOKEN` in ignored `.env`: direct API list of 50 Inbox documents had a 748 ms median (487–784 ms), versus 3,119 ms (2,746–3,930 ms) through the CLI. Direct API detail with HTML had a 446 ms median (431–503 ms), versus 2,385 ms (1,952–2,975 ms) through the CLI. Both list paths returned 50 rows with matching top-level response keys. API detail returns HTML/content fields while the CLI returns Markdown, so that comparison is similar but not byte-for-byte equivalent. The API reference has no documented free-text search endpoint, so search remains CLI-only in this comparison.
- One live GUI sample on 2026-11-14: Omawise's Hyprland window appeared 947 ms after process launch; its initial account-backed list process completed 4,762 ms after launch. This confirms that visible startup precedes library readiness, but one sample is not a robust GUI latency distribution. Repeated article-open latency remains unmeasured.
- The project `.env` contains a user-provided API token and is ignored by `.gitignore`; it is for development benchmarking only and the app does not read it. Never print, commit, or read CLI credential storage. Direct API token entry and keyring storage are implemented for the faster list/detail read paths.
- On 2026-11-15, a temporary integration harness validated the token, stored it through Secret Service, reloaded it in a fresh backend, fetched the list and article detail through the API, then removed the keyring entry. It printed only pass/fail stages; the token and article content were not output. A separate read-only API pagination check returned 50 rows on each of two pages and no duplicate IDs.

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

## Packaging and releases

- `./bin/package` creates a local Arch package from the current checkout without installing it; `./bin/install` builds and installs the checkout through `makepkg`/`pacman`. Both use `build/makepkg/` for makepkg's work directory so `--cleanbuild` cannot collide with the project's `src/` folder.
- `.github/workflows/release.yml` runs on `v*` tags, verifies the tag equals `v<pkgver>`, builds and checks in Arch Linux, then creates a GitHub Release with the `.pkg.tar.zst` package attached.
- To publish a release, update `pkgver` in `PKGBUILD`, commit, and push the matching tag. The package currently targets x86_64 Arch Linux/Omarchy. A user can install the downloaded asset with `sudo pacman -U ./omawise-*.pkg.tar.zst`.
- Release `v0.1.3` is published at <https://github.com/jjanuszczak/omawise/releases/tag/v0.1.3> with `omawise-0.1.3-1-x86_64.pkg.tar.zst` attached. Its GitHub Actions build, tests, package assembly, and release upload all passed.

Do not issue account calls during routine development; use a fake `readwise` executable for interaction checks unless the user explicitly asks to test against a real account.

## Verification already performed

- The app compiles with the available Qt 6.12 toolchain. The full offscreen suite reports 19 Qt Test passes including setup and cleanup.
- The repository's QML interaction test uses a fake CLI and verifies search submission, keyboard document activation, the Open action, article focus and paragraph/word selection, highlight creation through the shortcut and toolbar, inline document note/tag editing, API token dialog masking/clearing, and highlight refresh after creation. Coverage includes retained paragraph margins after Markdown conversion, reader settings defaults, live TOML reload and value bounds; persistent cache load/clear/expiry; cached library load; failed-refresh fallback; file permissions; title-search `document_id` normalization; highlight refresh before edits; and cache-backed QML frame timings.
- `bin/test` sets `QT_QPA_PLATFORM=offscreen` and `QT_QPA_PLATFORMTHEME=generic` so GUI checks do not depend on the host desktop session.
- The CLI sign-in prompt is hidden normally and appears only when CLI error text indicates an authentication failure; it launches the official CLI OAuth flow when selected. The app does not inspect CLI credential files. The separate API token flow stores its token in Secret Service.
- `./bin/package --cleanbuild` succeeds locally without deleting or modifying the project's `src/` files. Release `v0.1.3` repeated the clean package build successfully in GitHub Actions; the package has not yet been installed locally.
- A grabbed offscreen mock article confirmed the article pane is visible and Markdown headings/body/list formatting render at the intended width.
- A controlled 50-document Inbox account check found the CLI title-search response uses `document_id`, while the app parser previously required `id`. `resultRows()` now accepts `id`, `document_id`, and `documentId`; repeating the check found the selected exact-title document among 15 results. A same-value metadata write using the app's current payload succeeded and read back the unchanged note. The prior temporary write failure cannot be reproduced from the saved handoff because its exact error/response was not recorded; no sentinel value was written during this turn.

## Known gaps and risks

- Search local matching only examines documents currently loaded in the selected view. Results outside that set depend on CLI hybrid and title search; the real-account title-search response now normalizes correctly from its `document_id` field.
- A raw CLI detail response was inspected during the performance comparison; full UI rendering with the real CLI path has not been verified. Markdown rendering was also covered with the representative fake CLI response.
- Styled Markdown is an approximation of Readwise Web. Rich inline styles and link colors are partly controlled by Qt Quick; complex HTML, remote images, embeds, PDF/EPUB layout and highlight anchoring, TTS, Ghostreader, and offline sync are not implemented.
- The app is English-only. QML/C++ strings use translation wrappers, but Omarchy localization catalogs and live language switching remain unimplemented.
- Existing five-run account timings show API list/detail calls (about 748/446 ms median), above 400 ms. The latest seven-run CLI sample also exceeds 400 ms (list 2,238.5 ms p50 / 3,035.1 ms p95; detail 1,924.6 ms p50 / 2,576.1 ms p95). With ahead-of-time QML caching, offscreen startup-to-library-frame p50/p95 was 71/75 ms and cached article open-to-frame was 15/19 ms. Cached actions meet the target in this environment; uncached network calls do not.
- The exact earlier metadata-write failure remains unclassified because its error details were not saved. The current same-value account write/readback path succeeded without changing the note.
- The local development package has been assembled successfully, but it has not been installed or launched from the installed package.
- GitHub Actions reported non-blocking Node.js 20 deprecation annotations for the v4 actions and a future `ubuntu-latest` image migration; update runner/action versions when needed.
- Current release commit `6c9dd98` (`Fix clean Arch package builds`) is on `main` and tagged `v0.1.3`.

## Suggested next steps

1. Confirm the offscreen cached-action and startup p50/p95 on Omarchy hardware.
2. Measure direct API p95 with the keyring-backed app when connected; the existing API account comparison recorded five-run medians only. CLI list/detail p50/p95 are now recorded above.
3. Continue routine development with fake CLI fixtures. The title-search parser, persistent cache lifecycle, refresh fallback, expiry, clear, and highlight preflight now have regression coverage.
4. Consider explicit URL capture, seen/reading-progress controls, offline reading, settings and shortcut customization, and dedicated PDF/EPUB viewers as future work. Keep PDF/EPUB anchoring, TTS, Ghostreader, and complete Reader parity out of current claims.

## Contributor instructions

Read [AGENTS.md](../AGENTS.md) first. Update it together with this handoff whenever the architecture, CLI contract, build flow, verification method, or supported capabilities change.
