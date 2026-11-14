# Readwise Reader for Omarchy

An early native Qt Quick client prototype for Omarchy. This Phase 1 vertical slice lists Reader documents, searches the library, opens Markdown document details, and archives the selected document through the official Readwise CLI.

The app does not store or handle Readwise tokens. Install and authenticate the official CLI once, then launch the client:

```sh
npm install -g @readwise/cli
readwise login
./bin/run
```

Build and run the regression suite with `./bin/build`; run just the tests with `./bin/test`. The app invokes the CLI directly with `QProcess` and argument arrays (no shell), requests JSON for data commands, and surfaces CLI errors in the window. Login is launched only when the user selects **Connect / sign in**.

## Shortcuts

| Key | Action |
|---|---|
| `j` / `k` | Move through documents |
| `Enter` | Open selected document |
| `/` | Focus search; Enter submits the search |
| `Ctrl+K` | Open command palette |
| `a` | Archive selected document |
| `Ctrl+Shift+H` | Save selected article text as a highlight |
| `↑` / `↓` in article | Select previous / next paragraph; press `h` to highlight it |
| `Shift+Arrow` | Select article text by character |
| `Ctrl+Shift+←` / `Ctrl+Shift+→` | Extend article selection by word |
| `Ctrl+R` | Refresh current view |
| `Ctrl+←` / `Ctrl+→` | In library view, focus navigation / document list; in the article, move by word |
| `Escape` in search | Clear search and return to the document list |
| `Escape` | Return to library or close overlay |
| `?` | Show shortcut reference |

This is an early functional slice. It currently supports Inbox, Later, Shortlist, Archive, and Feed views; hybrid search plus a separate title-search pass; Markdown reading; a right-side document metadata pane that works from the library selection without opening the article; editable document notes and removable tag badges; text highlights with per-highlight notes and tags; and archiving. The library view shows the document list and metadata pane; select a row then press Enter or use the square Open icon to open it. In article view, Inbox, Later, and Archive controls move the document; **Open in Reader** opens its source URL. The pane has **Info** and **Notebook** tabs; highlight note and tag controls appear on hover. Article text can be selected with the keyboard or mouse, and selection can be saved with the toolbar button or `Ctrl+Shift+H`. The UI uses Omarchy's monospace font and square controls. Offline cache, localization catalogs, and full media viewers remain future work. Search runs when submitted with Enter, avoiding repeated CLI calls while typing. Markdown content is rendered with a centered reading column and selectable rich text. This is not yet a substitute for Reader's specialized PDF/EPUB viewers or an exact replica of its web typography.
