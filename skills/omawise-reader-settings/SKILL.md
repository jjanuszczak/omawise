---
name: omawise-reader-settings
description: Adjust Omawise article typography, reading progress, and external editor handoff through its user TOML configuration. Use when changing the reading font, size, line or paragraph spacing, article column width, progress mode, or installed editors shown in Open in.
---

# Omawise reader settings

Open **Omawise settings** from the bottom of the library navigation pane to adjust the supported options, or edit `~/.config/omawise/reader.toml` directly. The article's **Aa** button opens the same dialog. Omawise watches the file and applies valid changes while running. The packaged example is installed at `/usr/share/omawise/examples/reader.toml`.

Supported keys are in the `[reader]` table:

| Key | Default | Accepted range |
|---|---:|---:|
| `font_family` | `"monospace"` | Fontconfig family name, letters/numbers/spaces and `,_-`, up to 80 characters |
| `font_size_px` | `12` | 12–48 px |
| `line_spacing` | `1.2` | 1.0–2.2 |
| `paragraph_spacing_pt` | `18` | 0–36 pt |
| `content_width` | `760` | 480–1200 px |
| `progress_mode` | `"last_position"` | `"last_position"` or `"furthest_position"` |
| `external_editors` | `["neovim", "vscode", "obsidian"]` | IDs from `neovim`, `vscode`, `obsidian`, `helix`, `emacs`, `zed`, `sublime`, `kate`, `mousepad` |

Keep values as TOML strings or numbers. Missing, malformed, or unsupported values use the defaults. Numeric values outside the accepted range are clamped. The Omarchy accent color, current dark article palette, and `monospace` typeface remain the defaults; `font_family` can override the article typeface only.

Example: to use a larger reading column and more open paragraphs:

```toml
[reader]
font_family = "monospace"
font_size_px = 19
line_spacing = 1.5
paragraph_spacing_pt = 19
content_width = 820
progress_mode = "last_position"
external_editors = ["neovim", "vscode", "obsidian"]
```

`last_position` resumes from the latest saved location, including backward scrolling. `furthest_position` keeps the maximum progress reached and resumes there. Progress is saved while reading and when leaving an article. New articles start at the top; PDF progress is unsupported by Reader.

The `external_editors` array controls the editor IDs Omawise may show in the article's **Open in…** submenu. Omawise hides entries whose application is not installed. Neovim and Helix require a supported terminal; file-based editor exports are placed in `~/Documents/Omawise Reader`. Obsidian receives the Markdown clipboard content through its new-note URI and creates a note in the active vault.

When changing renderer code, preserve selectable text and link routing through `Backend::openExternalUrl`. Use a multi-block mock article to inspect headings, paragraphs, lists, quotes, code, links, and scrolling. Update the example, this skill, `README.md`, and `AGENTS.md` together when the supported settings change.
