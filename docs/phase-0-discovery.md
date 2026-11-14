# Phase 0 — Readwise interface and UX discovery

**Status:** capability audit complete; keyboard UX prototype added and smoke-launched offscreen. A live account integration was not attempted because this project has no configured test account. The prototype uses fictional local sample data.

## Executive findings

1. The official `readwise` CLI installed in this environment is version **0.5.9** and covers the core Reader workflows: list/search/read documents, save URLs, move documents, edit metadata, manage tags, fetch/create highlights, and export. It has a JSON output mode and a documented read-only mode.
2. The Reader API has matured since the initial PRD: its current docs now describe listing, save/create, metadata updates, bulk updates, delete, tags, and highlight creation/update/deletion. Highlight entries are Reader documents with a `parent_id`; the API supplies `highlight_offset` and `highlight_location` for anchoring.
3. The CLI is the strongest starting point for authentication and agent integration. The GUI should not shell out once per keystroke or use CLI output as an undocumented internal protocol. Phase 1 should compare (a) an async CLI adapter for supported actions with (b) direct API calls using the user's token; decide based on startup/latency, pagination, coverage, and credential handling. Reuse the CLI's auth session only if a supported integration exists; do not parse private config files.
4. A full Reader replacement is not established by the public API/CLI surface. Ghostreader, TTS playback, public sharing, precise PDF/EPUB reading/highlighting, and Reader's complete local/offline behavior need separate capability validation or fallback links.
5. Local Qt Quick dependencies are installed (`qmake6`, Qt 6.12.0, Quick and Quick Controls 2). The UX prototype is in `prototype/reader-ux/` and has no account access or network code.

## Capability matrix

| Capability | Official CLI 0.5.9 | Reader API docs | Phase 0 assessment |
|---|---|---|---|
| Authenticate | OAuth browser login or token login | Token authorization header; token obtained from Readwise account | Prefer CLI for first-run login; confirm whether an app can securely reuse credentials without undocumented config access. |
| Browse locations | List by `new`, `later`, `shortlist`, `archive`, `feed` | List supports location, category, tag, cursor, update time | Supported. CLI category list includes podcast/audiobook; public API docs should be checked for current category coverage. |
| Search | Hybrid document search plus title, author, tag, note, summary, source, URL, date, category, and location filters | No equivalent search endpoint documented on Reader API page | CLI supports a dedicated title-search filter; the app runs it separately from hybrid search and merges unique IDs. Confirm latency and real-account result coverage. |
| Read document | Details command returns Markdown | List can include `html_content`; details and content fields documented | Supported, but compare detail fidelity/types and body size. Don't request full bodies in list views. |
| Save URL | `reader-create-document` | `POST /api/v3/save/` | Supported. |
| Move / archive / shortlist | Move documents; bulk metadata edit | Update and bulk update | Supported. Bulk operations have rate limits and per-item errors to handle. |
| Seen state and progress | Bulk metadata edit includes seen and metadata | Update/bulk update include seen and `reading_progress`; progress not supported for PDFs | Supported with PDF progress limitation. |
| Tags | Add/remove tags on documents and highlights | Tag list and document/highlight tag updates | Supported. Verify replace-vs-append semantics for each operation. |
| Document notes | Bulk metadata edit | Update endpoint supports notes | Supported, with distinct document-note and highlight-note semantics. |
| Highlights | List/get, create, tag, set notes | Highlights appear as child documents; save creates; update/delete endpoints supported | Core operations are now documented. Exact text matching and API anchoring need an integration spike, especially for EPUB/PDF. |
| Export | Full export to Markdown ZIP; status polling | API list can be paged | Supported. Export starts asynchronously. |
| Ghostreader / AI | Agent can use CLI/skills over library data | Not a general Ghostreader UI API | Agent workflows are possible; native Ghostreader parity is unverified and likely out of scope for first release. |
| Text to speech | No playback feature confirmed in CLI help | No playback interface documented | Unverified; link to Reader until a supported service/interface is established. |
| PDFs / EPUBs | API/CLI can expose metadata/content and EPUB upload tools exist in CLI | Raw source URL can be temporary; PDF progress unsupported | Viewer and highlight fidelity are high-risk; prototype dedicated viewer spike before committing. |
| Offline use | Export can provide local content snapshots | API is network based | App-owned cache/queue can support reading offline; full Reader parity and conflict behavior are not promised by API docs. |
| Keyboard customization | CLI not relevant | Reader web has keyboard map and customizable shortcuts | Recreate the user's main Reader workflow, then allow app-local keymap customization and layout-aware bindings. |

## CLI inspection

Read-only local inspection only; no account command or credential file was accessed. `readwise --help` reports JSON output support and commands for Reader search, document list/details/save, move, bulk metadata edit, tag management, highlight get/create/tag/note operations, and export/status polling. `reader-search-documents --help` confirms a dedicated `--title-search` option, which the app now invokes as a second query and merges with hybrid and loaded-page matches. It also documents `readwise config set readonly true`, which exposes only read operations to agents and requires re-authentication to turn writes back on. `reader-list-documents --help` confirms cursor pagination and that document bodies are excluded unless explicitly requested.

### Integration decision for the next phase

Phase 1 began with the **CLI adapter** for the first vertical slice: fixed command allowlist, asynchronous `QProcess`, JSON mode, no shell, and no secrets in arguments. A five-run account benchmark found direct API list/detail reads materially faster (about 0.45–0.75 s versus 2.4–3.1 s through the CLI). Omawise now supports an optional direct API token, validated through the documented auth endpoint and stored in Secret Service with libsecret. Direct API list/detail reads use `QNetworkAccessManager`; search and annotation/write features remain CLI-backed where API equivalents have not been implemented. Never read private CLI config files.

## UX prototype

`prototype/reader-ux/` contains the initial disconnected QML mock. The first functional app slice now lives at the project root (`src/`, `omawise.pro`) and includes list/search/detail/archive calls via a fake CLI process for the offscreen launch. It compiles with Qt 6.12.0 and remained running in the headless smoke launch. No authenticated account was queried; response compatibility, keyboard events, and latency remain unverified against Readwise.

## Open questions / Phase 1 gates

- Can the official CLI offer a supported machine-to-machine/session interface suitable for a desktop app, or should the app have separate API token login?
- Is the CLI's JSON output stable enough for the app adapter, and what is real-world spawn latency on target Omarchy hardware?
- Which document detail response fields retain usable HTML and highlight positions for each category?
- Can PDF/EPUB content and annotation operations provide reliable anchors across sync and document reprocessing?
- Which Reader features are must-have parity targets versus web-app fallbacks (Ghostreader, TTS, sharing, feed management, imports)?
- What plugin contract is appropriate: app CLI + bundled skills first, with third-party in-process plugins deferred in favor of permissioned external commands?

## Sources

- [Readwise CLI documentation](https://docs.readwise.io/tools/cli)
- [Official Readwise CLI command reference and examples](https://readwise.io/cli)
- [Reader API reference](https://readwise.io/reader_api)
- [Reader keyboard highlighting FAQ](https://docs.readwise.io/reader/docs/faqs/highlights-tags-notes)
- [Reader filtering syntax](https://docs.readwise.io/reader/guides/filtering/syntax-guide)
- [Readwise changelog](https://docs.readwise.io/changelog)
