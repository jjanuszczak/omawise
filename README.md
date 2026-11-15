<div align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="https://raw.githubusercontent.com/jjanuszczak/omawise/main/marketing/omawise-logo.svg">
    <source media="(prefers-color-scheme: light)" srcset="https://raw.githubusercontent.com/jjanuszczak/omawise/main/marketing/omawise-logo-light.svg">
    <img alt="Omawise — Readwise Reader for Omarchy" src="https://raw.githubusercontent.com/jjanuszczak/omawise/main/marketing/omawise-logo-light.svg" width="520">
  </picture>
</div>

# Omawise

Omawise is an early, keyboard-first desktop companion for Readwise Reader, built with Qt Quick for Omarchy. Browse Inbox, Later, Shortlist, Archive, and Feed; search your library; read supported articles; and manage document notes, tags, highlights, and highlight annotations. When connected with an API token, Omawise uses the Reader API for faster library and article loading and stores the token in the desktop Secret Service keyring. The official CLI remains in use for hybrid search and annotation/write operations that are not yet migrated.

For a development checkout, install and authenticate the official CLI, then launch the app:

```sh
npm install -g @readwise/cli
readwise login
./bin/run
```

Build and run the regression suite with `./bin/build`; run just the tests with `./bin/test`. Without an API token, Omawise uses the CLI for library listing and article details. To enable faster direct API reads, open the command palette with `Ctrl+K`, choose **Connect direct Readwise API**, and enter an access token from [Readwise](https://readwise.io/access_token). Omawise validates the token and stores it in the desktop keyring through Secret Service; it is never written to app settings or passed as a process argument. A Secret Service provider such as GNOME Keyring must be available. CLI OAuth remains required for hybrid search, highlight retrieval, and current annotation/write operations.

## Build and release packages

On Arch/Omarchy, install the [latest Omawise release](https://github.com/jjanuszczak/omawise/releases/latest) with `sudo pacman -U ./omawise-*.pkg.tar.zst`. `./bin/package` builds a package from a checkout without installing it; `./bin/install` builds and installs the checkout through `makepkg` and `pacman`. To publish a release, update `pkgver` in `PKGBUILD`, commit the change, and push the matching tag (for example, `v0.1.4`). GitHub Actions checks the app and attaches an x86_64 Arch package to the release.

The app icon and sidebar use the vector mark in `assets/omawise-mark.svg`; dark and light horizontal wordmarks for promotional use are in `marketing/omawise-logo.svg` and `marketing/omawise-logo-light.svg`.

## Reader typography

Article typography follows Readwise Reader's adjustable type, size, line spacing, and content width model while keeping Omawise's current Omarchy dark colors and monospace font as defaults. Defaults use 12 px text, 1.2 line spacing, and 18 pt between paragraphs. To customize it, copy `config/reader.toml` to `~/.config/omawise/reader.toml`; the checkout command is `mkdir -p ~/.config/omawise && cp config/reader.toml ~/.config/omawise/reader.toml`. Omawise reloads the file while running. The supported options and bounds are documented in the example and in the packaged agent skill at `/usr/share/omawise/skills/omawise-reader-settings/SKILL.md`.

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

This is an early functional slice. It currently supports Inbox, Later, Shortlist, Archive, and Feed views; hybrid search plus a separate title-search pass; Markdown reading; a right-side document metadata pane that works from the library selection without opening the article; editable document notes and removable tag badges; text highlights with per-highlight notes and tags; and archiving. The library view shows the document list and metadata pane; select a row then press Enter, double-click it, or use the square Open icon to open it. In article view, Inbox, Later, and Archive controls move the document; **Open in Reader** opens its source URL. The pane has **Info** and **Notebook** tabs; highlight note and tag controls appear on hover. Highlight data loads only when Notebook is opened and is cached for the current app session. Article bodies persist for up to 30 days; the latest first library page per location is reused for up to 24 hours. The shared cache is capped at 100 MiB. Cached articles and lists appear while a fresh Readwise request runs. Use **Ctrl+K → Clear cached article content** to clear saved article bodies; the cached library pages are retained. The app does not provide a durable offline write queue. Article text can be selected with the keyboard or mouse, and selection can be saved with the toolbar button or `Ctrl+Shift+H`. The UI uses Omarchy's monospace font and square controls. Localization catalogs and full media viewers remain future work. Search runs when submitted with Enter, avoiding repeated CLI calls while typing. Markdown and HTML article content is rendered in a centered, scrollable reading column with selectable rich text. This is not yet a substitute for Reader's specialized PDF/EPUB viewers or an exact replica of its web typography.
