# Skins

## Ask an LLM to create a skin

Supply this guide, the [AI authoring reference](../../Docs/AI/SKINS.md), and
[placeholder/skin.ini](../skins/placeholder/skin.ini) (the key catalog).
Specify style, name, language and output folder.
Personal skins belong in ignored `build/skins/` or outside the
checkout; Git maintains the bundled skins and placeholder.

## Bundled appearance

**Default** is the built-in appearance without a skin.
[Anville Graffiti](../skins/AnvilleGraffiti/skin.ini) and
[Finnish Dream](../skins/FinnishDream/skin.ini) are bundled as optional choices
under **hamburger → Skins**. A remembered selection is restored on reopening.

Builds refresh both in `skins/` (CLAP: sibling `SMS-AnvilleSampler.skins`).
Builds remove the old forced `skin` from build output, including CLAP's
`SMS-AnvilleSampler.skin`. Create `skin` in installed bundles to override the
collection.

## Loading skins

Discovery runs when the editor opens; menu choices switch immediately.
Reopen after editing; restart hosts retaining hidden editors.

## One skin or a collection

A `skin` directory inside the plugin bundle overrides every other choice and
hides the skin selector. Older unnamed manifests remain valid. A malformed forced skin uses the
built-in look and still suppresses collections.

Without `skin`, create `skins` containing one subfolder per skin:

```text
SMS-AnvilleSampler.vst3/
  Contents/
  skins/
    dark/
      skin.ini
      background.png
      logo.png
    graffiti/
      skin.ini
      background.png
      logo.png
```

Collections require a display name:

```ini
[skin]
version=1
name=Graffiti
```

If at least one named skin is available, **hamburger → Skins** lists **Default**
(the built-in appearance) first, followed by skin names sorted alphabetically.
Names appear literally. Selection is marked; wheel scrolling keeps Default
at the top. Only immediate subfolders are scanned. Missing names, unsupported or malformed manifests, and
paths escaping the collection are skipped. Selecting a skin that has since
become unreadable keeps the current skin and reports an error.

The bundle root contains `Contents`.
LV2 uses the same `skin`/`skins` directories inside `SMS-AnvilleSampler.lv2`.
For single-file CLAP, use sibling `SMS-AnvilleSampler.skin` for an override or
`SMS-AnvilleSampler.skins` for a collection. CLAP host integration remains
untested.

Copy the bundled `skin-template` as `skin` or `skins/dark`. Its source
[placeholder/skin.ini](../skins/placeholder/skin.ini) catalogs colors, metrics,
labels/formats and image slots.

## Remembering a choice

Choices are saved per user under `SMS-AnvilleSampler/selected-skin`:

| Platform | Parent folder |
| --- | --- |
| Linux | Absolute `$XDG_CONFIG_HOME`, otherwise `~/.config` |
| Windows 10 (1607+) | Native Roaming AppData (normally `%APPDATA%`) |
| macOS | `~/Library/Application Support` |

Selections replace preferences atomically. Unavailable config reports
that the appearance changed but could not be remembered.

Preferences store subfolder IDs, shared across LV2/VST3.
New editors restore them; already-open editors retain their appearance. A missing or unavailable remembered skin uses Default. A forced
`skin` ignores the preference without overwriting it. Choosing Default also
persists. Skins do not change host parameters or DAW project state.

## Edit the manifest

`skin.ini` is UTF-8 text with case-sensitive keys. `[skin]` must contain
`version=1`; collections also require `name=Display name`. Names share the
512-byte UTF-8 text limit. Each setting occupies one line. Comments begin with
`;` or `#`
on their own line. Surrounding spaces are trimmed; internal spaces are kept.
Values are literal: do not add quotes, inline comments, or escape sequences.

| Section | Values |
| --- | --- |
| `[colors]` | `#RRGGBB` or `#RRGGBBAA`; semantic and material keys from the example |
| `[metrics]` | Decimal numbers; alpha 0–1, radii/outline width 0–24 pixels, logo pulse 0–60 seconds |
| `[strings]` | Exact built-in label/format on the left, replacement text on the right |
| `[images]` | `background` and `logo`: relative image filenames inside the skin |

Rename custom skins' `GATE` string keys to `HOLD`.

For example:

```ini
[skin]
version=1
[colors]
controlAccent=#58B8E8
selection=#80D0FF
[metrics]
backgroundAlpha=0.75
padAlpha=0.60
padPanelAlpha=0.35
controlPanelAlpha=0.50
[images]
background=background.png
```

Keep replacement labels short: the layout and font sizes remain fixed.
Headings and numeric readouts also accept translations: preserve each printf
conversion exactly, including order, width, precision, and `%%`. Incompatible
formats fall back; added conversions, `%n`, dynamic widths, and positional
arguments are rejected. MIDI note names, bank letters, version text, and
host-provided interfaces stay fixed. Replacements never become commands, URLs,
parameter names, or saved-state keys. The six pad palette colors
use `pad1` through `pad6`; these retain the saved slot indices in their original
order. Their menu names can be replaced in `[strings]`.

The ADSR curve and all points use `activityPlayback`, including on hover.

Use PNG or JPEG artwork, up to 64 MiB per encoded file, with Unicode paths supported. The
background stretches across the 1344 × 756 logical chassis, behind the panels,
controls, and screws. It does not replace individual control artwork. The logo
stretches into the 170 × 78 header rectangle: 48% opacity without a glow,
fully opaque over an enabled halo.
Transparent pixels reveal the built-in rendering. Assets load once on the
first draw with a ready graphics context.

Under `[metrics]`, `backgroundAlpha` fades artwork over the chassis;
`padAlpha` fades pad fills, tints, shadows and grain. `padPanelAlpha` fades
the main pad-bank backing panel so artwork shows through it. `controlPanelAlpha`
fades the right-side backing panel in the main view, Sample Editor and Cut Point
Editor. Both panel metrics include the backing's borders, shadows and grain.
Labels, pad selection outlines, menus and individual controls retain their opacity.
All four accept 0–1 and default to `1`.

Configure halos with `[colors] logoGlow` and `[metrics] logoGlowAlpha`,
`logoGlowRadius`, and `logoGlowPulseSeconds`. Alpha or radius `0` disables it;
a transparent glow color also disables it. Pulse `0` is steady; positive values
set the cycle length in seconds. Brightness gently cycles from 65% to 100% of
the configured alpha. Missing alpha defaults to `0`, preserving older skins.
The halo follows logo alpha; use transparent PNGs. Logos exceeding 4096 pixels
on either axis receive no halo. Animation runs only while visible, up to 30 FPS.

Missing/invalid settings retain defaults; unknown keys are ignored.
Missing, malformed, oversized or unsupported manifests use the built-in theme. The
manifest limit is 64 KiB; string keys/values have a 512-byte limit and must be
valid UTF-8 without control characters. Absolute paths, `..`, and symlinks
escaping the skin directory are rejected. Each editor owns its palette; skin
loading and preference I/O never run in
audio callbacks. Image replacement waits for a draw with a current
graphics context.
