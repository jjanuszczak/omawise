<div align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="https://raw.githubusercontent.com/jjanuszczak/omawise/main/marketing/omawise-logo.svg">
    <source media="(prefers-color-scheme: light)" srcset="https://raw.githubusercontent.com/jjanuszczak/omawise/main/marketing/omawise-logo-light.svg">
    <img alt="Omawise — Readwise Reader for Omarchy" src="https://raw.githubusercontent.com/jjanuszczak/omawise/main/marketing/omawise-logo-light.svg" width="520">
  </picture>
</div>

# Omawise

Omawise is an early, keyboard-first desktop companion for Readwise Reader, built with Qt Quick for Omarchy. Browse Inbox, Later, Shortlist, Archive, and Feed; search your library; read supported articles; and manage document notes, tags, highlights, and highlight annotations. When connected with an API token, Omawise uses the Reader API for faster library and article loading and for asynchronous progress writes, and stores the token in the desktop Secret Service keyring. The official CLI remains in use for hybrid search and annotation/write operations that are not yet migrated.

For a development checkout, install the official CLI and launch the app:

```sh
npm install -g @readwise/cli
./bin/run
```

Build and run the regression suite with `./bin/build`; run just the tests with `./bin/test`. On first launch, use the **Set up Readwise** prompt or the account button at the bottom of the library sidebar. **Sign in with Readwise** starts the official CLI OAuth flow in your browser. That sign-in is needed for hybrid search, highlight retrieval, and annotation features. The optional direct Reader API connection speeds up library/article loading and progress writes: choose **Connect API token**, get a token from [Readwise](https://readwise.io/access_token), and paste it into the masked field. Omawise validates it and stores it in the desktop keyring through Secret Service; it is never written to app settings or passed in command arguments. A Secret Service provider such as GNOME Keyring must be available. Account settings show the two connection states separately, let you replace the API token, or disconnect it. The official CLI currently has no standalone sign-out command, so its session remains managed by the CLI.

## Build and release packages

On Arch/Omarchy, install the [latest Omawise release](https://github.com/jjanuszczak/omawise/releases/latest) with `sudo pacman -U ./omawise-*.pkg.tar.zst`. `./bin/package` builds a package from a checkout without installing it; `./bin/install` builds and installs the checkout through `makepkg` and `pacman`. To publish a release, update `pkgver` in `PKGBUILD`, commit the change, and push the matching tag (for example, `v0.1.6`). GitHub Actions checks the app and attaches an x86_64 Arch package to the release.

The app icon and sidebar use the vector mark in `assets/omawise-mark.svg`; dark and light horizontal wordmarks for promotional use are in `marketing/omawise-logo.svg` and `marketing/omawise-logo-light.svg`.

## Reader settings

Article typography follows Readwise Reader's adjustable type, size, line spacing, and content width model while keeping Omawise's current Omarchy dark colors and monospace font as defaults. Defaults use 12 px text, 1.2 line spacing, and 18 pt between paragraphs. Open **Omawise settings** from the bottom of the library navigation pane or use **Aa** in the reader to change typography, progress behavior, and configured external editors. Changes save to `~/.config/omawise/reader.toml` and apply while Omawise is running. Only configured editors that are installed appear under **Open in…**. The same settings can be edited by hand, and `config/reader.toml` is the example file. Supported options and bounds are documented there and in the packaged agent skill at `/usr/share/omawise/skills/omawise-reader-settings/SKILL.md`.

The article toolbar uses compact Inbox, Later, and Archive icons; the current location is marked and selecting another icon moves the article. **Aa** opens Omawise settings, and the overflow menu holds **Copy Markdown**, highlight, **Reset reading progress**, **Open original**, **Copy document URL**, and **Open in…** actions. The original URL comes from Reader's `source_url` metadata; **Open in Reader** uses the separate Reader document URL. **Open in…** includes Reader and configured, installed editors; the initial configuration enables Neovim, VS Code, and Obsidian. File-based editors receive a Markdown export under `~/Documents/Omawise Reader`; Obsidian creates a note in its active vault. Copy Markdown and editor handoff include YAML frontmatter with article metadata. Scroll progress is saved and restored when reopening an article. **Progress behavior** selects **Last position** (default) or **Furthest position**; this controls the value Omawise writes to Reader's shared `reading_progress` field. Readwise represents this API field as a fraction from 0 to 1; Omawise converts it to a percentage for display. Reader's API does not expose its separate last-location marker. Use the Info pane to mark the selected document seen or unseen. Click **+** beside the Omawise mark or choose **Save URL to Reader** from **Ctrl+K** to add an HTTP or HTTPS link.

In the library metadata pane, the document tag field loads the tag list for your Reader library when opened. Choose a suggestion to add it, or type a new tag and press Enter to create it.

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

This is an early functional slice. It currently supports Inbox, Later, Shortlist, Archive, and Feed views; hybrid search plus a separate title-search pass; Markdown reading; a right-side document metadata pane that works from the library selection without opening the article; editable document notes and removable tag badges; text highlights with per-highlight notes and tags; URL capture; seen/unseen state; and article reading progress. The library view shows the document list and metadata pane; select a row then press Enter, double-click it, or use the square Open icon to open it. In library view, the navigation footer opens Readwise account settings, links to the Readwise profile and preferences pages, and opens Omawise settings for reader typography, progress behavior, and external editor choices. When an article is open, the left pane is reserved for its table of contents. In article view, Inbox, Later, and Archive controls move the document; **Open in Reader** opens the Readwise Reader page. The overflow menu can open/copy the original URL, reset progress, export Markdown without converting ordinary paragraphs into code, and hand the article to installed editors configured in `reader.toml`. Public link enable/disable is not implemented because Readwise's published API and official CLI currently document no operation for it. The pane has **Info** and **Notebook** tabs. Notebook contains document notes and highlights. Both note editors use the same padded field style and update immediately; changes sync to Readwise in the background without reloading the library. Document notes stay in the document note field, and highlight notes appear beneath their passage. Saved highlight passages are also marked in the article with a muted olive fill and a fine yellow underline. Each highlight card shows its saved note beneath the passage; click the note preview or choose **Edit note** from the card overflow menu to change it. Choose **Remove note** from that menu to clear it, and use **Edit tags** for highlight tags. Highlight data loads when an article opens or Notebook is opened, then stays cached for the app session. Article bodies persist for up to 30 days; the latest first library page per location is reused for up to 24 hours. The shared cache is capped at 100 MiB. Cached articles and lists appear while a fresh Readwise request runs. Use **Ctrl+K → Clear cached article content** to clear saved article bodies; the cached library pages are retained. The app does not provide a durable offline write queue. Article text can be selected with the keyboard or mouse, and selection can be saved with the toolbar button or `Ctrl+Shift+H`. The UI uses Omarchy's monospace font and square controls. Localization catalogs and full media viewers remain future work. Search runs when submitted with Enter, avoiding repeated CLI calls while typing. Markdown and HTML article content is rendered in a centered, scrollable reading column with selectable rich text. Article progress resumes at the saved position; choose last-position or furthest-position behavior in reader settings. Precise PDF progress is unsupported by Reader. This is not yet a substitute for Reader's specialized PDF/EPUB viewers or an exact replica of its web typography.
