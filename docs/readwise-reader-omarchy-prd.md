# Omawise: PRD and implementation plan

## Product summary

Build a native, keyboard-first Reader client for Omarchy: a fast place to capture, organize, and read the material already in a user’s Readwise Reader account. It should feel familiar to Reader users, work with Omarchy’s theme and desktop conventions, and make the library easy to use from local AI agents.

The client should be a companion to Readwise’s web and Mac apps, with the Readwise account remaining the source of truth. Readwise offers Reader on web, Mac, and Windows, along with mobile apps and browser extensions. Its web app is designed around shortcuts and a command palette. [Reader platform and offline FAQ](https://docs.readwise.io/reader/docs/faqs), [Reader keyboard shortcut FAQ](https://docs.readwise.io/reader/docs/faqs)

## Product goals

- Make common Reader actions quick from the keyboard: find a document, read it, highlight it, add a note, organize it, and move on.
- Keep documents, reading state, highlights, notes, and tags synced with the user’s Readwise account.
- Provide a focused reading experience for articles, newsletters, PDFs, EPUBs, videos, and other supported Reader documents.
- Let local agents and user-written extensions work with Reader data through documented, scriptable interfaces.
- Follow Omarchy’s current theme, language, and desktop conventions.

## Product principles

1. **Keyboard first.** A command palette should make actions discoverable; shortcuts should cover the normal reading workflow.
2. **Readwise owns the library.** The app should not create a competing source of truth or surprise users with one-way local edits.
3. **Fast to open and responsive.** Startup, search, document opening, and routine actions should not wait on avoidable work.
4. **Local control for agents.** Users choose which local tools can access their account and data.
5. **Honest parity.** Reader’s server APIs and CLI expose some capabilities but don’t automatically provide every feature or the full native reading experience. Treat feature parity as a roadmap, and verify each capability against the current API.

## Intended users and primary workflows

**Reader on Omarchy:** keyboard-oriented users who want to triage their inbox, read without browser clutter, and annotate while keeping their existing Reader account.

**Local AI agents:** users who want an agent to search their library, summarize selected documents, prepare a reading list, or organize documents using Readwise’s CLI or an app-provided command.

**Core workflow:** open client → search or choose a library view → read → highlight and annotate → archive, defer, or shortlist → sync.

## PRD: functional requirements

### 1. Library and navigation

- Show Reader’s core locations, including New, Later, Shortlist, Archive, and Feed.
- Support document categories and tags, plus saved filters or query-based views where the API permits.
- Provide search, sorting, list navigation, and multi-select actions.
- Display useful metadata such as title, author, source, category, tags, reading progress, and unseen state.
- Make sync state visible, including errors and pending local edits.

Reader’s API documents listing by location, category, tag, and cursor, with optional HTML content. The API also has rate limits, so sync and search should be deliberate about pagination and retries. [Reader API: document listing and rate limits](https://readwise.io/reader_api)

### 2. Reading and annotation

- Render article and newsletter text in a quiet, adjustable reading view.
- Support highlighting selected text, notes, and tags when the relevant API operations allow it.
- Show existing highlights and notes in context where possible.
- Provide controls for text size, line width, spacing, theme, and progress.
- Support PDFs and EPUBs through dedicated viewers; treat accurate highlight anchoring as a separate requirement from ordinary article text.
- Provide a fallback action to open the document in Readwise Reader when a document type or operation is not yet supported.

Reader’s current API documents creating, updating, and deleting highlights, with highlight positions represented as offsets and serialized DOM locations. Phase 0 should still validate how reliably those positions anchor across document types and reprocessing before promising full annotation parity. [Reader API](https://readwise.io/reader_api), [Readwise changelog](https://docs.readwise.io/changelog)

### 3. Capture and organization

- Save a URL to Reader.
- Support adding local files and URLs through desktop actions, including a “share to Reader” workflow where Omarchy/browser integration allows it.
- Move documents among supported locations, mark them seen or unseen, and manage tags.
- Provide reversible actions where possible, with clear confirmation for destructive operations such as deleting a document and its highlights.

The official CLI documents saving URLs, searching and reading documents, moving documents, creating highlights, and adding tags. [Readwise CLI overview and examples](https://readwise.io/cli)

### 4. Keyboard and command palette

- Provide a global command palette, with searchable actions and shortcuts.
- Offer a `?` shortcut reference.
- Support keyboard navigation in the library, reader, and annotation controls.
- Include configurable key bindings and respect keyboard layouts.
- Avoid collisions with Omarchy and common terminal/window-manager shortcuts.

Reader’s web app uses `Ctrl/Cmd+K` for its command palette, and provides configurable shortcuts and a shortcut reference. The client should preserve that mental model while checking for conflicts on Linux. [Reader keyboard shortcut FAQ](https://docs.readwise.io/reader/docs/faqs)

### 5. Local AI and extensibility

- Make the official Readwise CLI work cleanly alongside the app; offer setup guidance and a way to check whether it is installed and authenticated.
- Bundle an installable agent skill that explains safe, useful workflows and points agents to supported commands.
- Provide an app CLI for app-specific tasks such as opening a document, checking sync status, or exporting locally available content, with machine-readable output.
- Define a small extension interface for user scripts or plugins: documented events, permissions, and actions, with explicit user enablement.
- Keep Readwise account access under the user’s control; do not silently expose credentials or run arbitrary extensions.

The official CLI gives agents direct access to Reader and Readwise data, and Readwise also offers an MCP option. That makes the CLI a useful agent integration, but it doesn’t make it the ideal private backend contract for every part of a desktop reader. [Readwise CLI docs](https://docs.readwise.io/tools/cli), [Readwise MCP and CLI overview](https://docs.readwise.io/tools)

## Architecture recommendation

### Native Omarchy app shell

Use **C++ and Qt Quick/QML**, following Omarchy’s app pattern: C++ owns state, processes, network work, and failures; QML owns layout and interaction. Package it as an Arch app with a launcher entry. Follow the theme live and use Omarchy’s language setting. The Omarchy app conventions also call for application shortcuts, live theme handling, an agent-facing CLI, and a bundled skill.

### Readwise integration: CLI first, API as a measured fallback

Phase 0 confirmed the official CLI has commands for the first reading workflow and provides JSON output and OAuth login. The Phase 1 vertical slice therefore uses it as the GUI adapter while it is also the user’s agent interface.

Reasons:

- It owns its supported login and credential flow, so the app doesn’t need to collect or store a Readwise token in the initial slice.
- It covers list, search, detail, move, highlight, tag, and export operations and can return machine-readable JSON.
- It keeps the desktop app and local agents on the same supported Readwise surface.

Invoke a fixed allowlist asynchronously through `QProcess`, never a shell, and never pass credentials in arguments. Keep the command adapter behind a repository interface. Benchmark cold and warm startup and validate JSON response shapes against a test account before treating the CLI as a permanent internal contract; add a direct API adapter only if latency or capability requires it. The API documents pagination, per-endpoint limits, and `429` handling. [Reader API](https://readwise.io/reader_api), [Readwise CLI docs](https://docs.readwise.io/tools/cli)

### Local storage and sync

Use a small local database for document metadata, cached text, reading position, and a durable operation queue. Cache content selectively so feed subscriptions or large libraries don’t unexpectedly consume disk space. Offer storage controls and a way to clear local cached content.

For writes, track operation state and surface partial failures. Avoid blindly replaying non-idempotent actions. Since Readwise’s API may accept some updates in bulk and can return per-item failures, the sync layer should handle partial success explicitly. [Reader API: bulk updates](https://readwise.io/reader_api)

### Reader rendering

Treat rendering as a major technical risk. Plain article text is a good initial target; PDFs, EPUBs, video transcripts, and other formats each need dedicated behavior. The goal should be a deliberate, useful reading experience, not a generic web view presented as full parity.

## Proposed shortcut model

Keep shortcuts consistent with Reader where possible, publish them in a searchable palette and help overlay, and allow users to customize them.

| Context | Example action |
|---|---|
| Anywhere | `Ctrl+K` command palette; `?` shortcut help |
| Library | Search; move focus through results; open selected document |
| Reading | Scroll; change text size; jump between highlights |
| Annotation | Create highlight; add note; edit tags |
| Document | Move to Later, Shortlist, or Archive; mark seen/unseen |
| App | Open settings; show sync state; quit |

This is a starting point, not a claim that these exact keys match every Reader binding or keyboard layout.

## Delivery plan

### Phase 0 — Technical discovery

- Inventory the current official CLI commands and the Reader API operations needed for each proposed feature.
- Confirm authentication, pagination, content retrieval, highlight creation/editing, location changes, tags, and error behavior.
- Identify formats that the API supplies in usable form, and define fallback behavior for the rest.
- Prototype startup, list loading, document rendering, and keyboard navigation against a test account.

**Exit condition:** a short capability matrix separating supported, uncertain, and unavailable operations.

### Phase 1 — Fast, useful Reader companion

- Native Qt Quick app shell, Omarchy launcher packaging, live theme and language support.
- Login and account status.
- Library views, search, document list, article reading, core location changes.
- Basic keyboard navigation, command palette, help overlay.
- CLI-backed library list/search/detail/archive flow, with credentials managed by the CLI.
- Readable sync/error status and a theme-aware Qt Quick shell.

**Exit condition:** a user can authenticate with the official CLI, find and read an article, change its location, and see the result sync to Reader. The current slice implements those UI paths but still needs validation with a real test account.

### Phase 2 — Annotation and formats

- Highlight and note workflows.
- Tags and bulk operations.
- PDF and EPUB reading, including highlight behavior where supported.
- Reader-compatible progress and seen/unseen state.
- Cache controls and a first offline reading experience.

**Exit condition:** the supported formats and annotation workflows pass an explicit capability checklist.

### Phase 3 — Omarchy and agent extensibility

- App CLI with stable JSON output for app-level actions.
- Bundled agent skill, installable into the user’s preferred agent environment.
- Permissioned plugin/extension model with documented actions and events.
- Integration guidance for the official Readwise CLI and MCP.

**Exit condition:** an agent can use documented commands to search, inspect, and organize Reader content, and users can disable or remove integrations.

### Phase 4 — Parity expansion

- Evaluate remaining features from the web and Mac apps, such as advanced filters, audio/text-to-speech, Ghostreader workflows, sharing, and specialized document support.
- Add features only after confirming they can be implemented reliably with available interfaces or a supported local approach.
- Maintain a public feature matrix so “all features” has a clear, reviewable definition.

## Success measures

- Time from launch to a usable library view.
- Time and keystrokes to find, open, annotate, and archive a document.
- Sync success rate and time to reflect a change in the Reader account.
- Frequency of failed or confusing keyboard actions.
- Local cache size and offline reading reliability.
- Agent task success using documented commands, with no credential leakage.

Set numeric targets after the Phase 0 prototype establishes realistic baselines.

## Main risks and decisions

- **“All features” cannot be assumed from the CLI/API.** The official CLI is broad, but the documented API surface does not establish parity for every Reader UI feature. Build a verified capability matrix before treating this as a complete replacement.
- **Rendering and annotation are the hardest parts.** A usable article reader is much easier than faithful PDF/EPUB highlighting and less common document types.
- **Sync is a product feature.** Rate limits, partial bulk failures, offline changes, and conflicts need visible handling.
- **Plugins increase the trust surface.** Start with a narrow permission model and explicit enablement; don’t let arbitrary extensions inherit account credentials.
- **Package/release cadence matters.** Plan for Arch packaging and compatibility with Omarchy’s app conventions from the start.

**Recommendation:** position the first release as a fast, keyboard-first native companion with strong article reading, search, organization, and agent access. Define “feature-complete Reader replacement” as a later, measurable goal, contingent on API coverage and validated rendering work.

## Sources

- [Readwise Reader documentation](https://docs.readwise.io/reader)
- [Reader FAQ: platform, offline behavior, shortcuts](https://docs.readwise.io/reader/docs/faqs)
- [Official Readwise CLI](https://docs.readwise.io/tools/cli)
- [Readwise CLI examples](https://readwise.io/cli)
- [Reader API reference](https://readwise.io/reader_api)
