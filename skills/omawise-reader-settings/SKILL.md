---
name: omawise-reader-settings
description: Adjust Omawise article typography through its user TOML configuration. Use when changing the reading font, size, line or paragraph spacing, or article column width.
---

# Omawise reader settings

Edit `~/.config/omawise/reader.toml` to change article formatting. Omawise watches the file and applies valid changes while running. The packaged example is installed at `/usr/share/omawise/examples/reader.toml`.

Supported keys are in the `[reader]` table:

| Key | Default | Accepted range |
|---|---:|---:|
| `font_family` | `"monospace"` | Fontconfig family name, letters/numbers/spaces and `,_-`, up to 80 characters |
| `font_size_px` | `12` | 12–48 px |
| `line_spacing` | `1.2` | 1.0–2.2 |
| `paragraph_spacing_pt` | `18` | 0–36 pt |
| `content_width` | `760` | 480–1200 px |

Keep values as TOML strings or numbers. Missing, malformed, or unsupported values use the defaults. Numeric values outside the accepted range are clamped. The Omarchy accent color, current dark article palette, and `monospace` typeface remain the defaults; `font_family` can override the article typeface only.

Example: to use a larger reading column and more open paragraphs:

```toml
[reader]
font_family = "monospace"
font_size_px = 19
line_spacing = 1.5
paragraph_spacing_pt = 19
content_width = 820
```

When changing renderer code, preserve selectable text and link routing through `Backend::openExternalUrl`. Use a multi-block mock article to inspect headings, paragraphs, lists, quotes, code, links, and scrolling. Update the example, this skill, `README.md`, and `AGENTS.md` together when the supported settings change.
