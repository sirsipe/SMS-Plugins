# Skins

## Ask an LLM to create a skin

Give it this guide and ask it to follow the
[AI authoring reference](../../Docs/AI/SKINS.md), starting from
[placeholder/skin.ini](../skins/placeholder/skin.ini). Specify the visual style,
display name, language, and output folder. If the LLM cannot access repository
links, attach all three files; this guide alone does not include the full key
and label catalog. Custom skins belong in ignored `build/skins/` or outside the
checkout. Only the placeholder is maintained in Git.

## Loading skins

Skin discovery runs when the editor opens. Colors, strings and artwork can
switch immediately through the menu; close and reopen the editor after adding,
renaming, removing, or editing skin files. Restart the host if it retains hidden
editors.

## One skin or a collection

A `skin` directory inside the plugin bundle overrides every other choice and
hides the skin selector. This preserves existing drop-in skins, including older
manifests without a name. A malformed forced skin falls back to the built-in
look, rather than enabling a collection behind it.

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

Each collection manifest must be valid and contain a nonempty display name:

```ini
[skin]
version=1
name=Graffiti
```

If at least one named skin is available, **hamburger → Skins** lists **Default**
(the built-in appearance) first, followed by skin names sorted alphabetically.
Names are taken directly from the manifests. The selected row is marked. Long
lists scroll with the mouse wheel; Default stays at the top. Only immediate
subfolders are scanned. Missing names, unsupported or malformed manifests, and
paths escaping the collection are skipped. Selecting a skin that has since
become unreadable keeps the current skin and reports an error.

The bundle root is alongside `Contents`, rather than beside its nested binary.
LV2 uses the same `skin`/`skins` directories inside `SMS-AnvilleSampler.lv2`.
For single-file CLAP, use sibling `SMS-AnvilleSampler.skin` for an override or
`SMS-AnvilleSampler.skins` for a collection. CLAP host integration remains
untested.

Every build includes an inactive `skin-template` example. Copy it as `skin` for
an override, or as `skins/dark` for a selectable skin. The source example is
[placeholder/skin.ini](../skins/placeholder/skin.ini). It catalogs colors,
material metrics, fixed/formatted labels and both image slots.

## Remembering a choice

Choices are saved per user in `$XDG_CONFIG_HOME/SMS-AnvilleSampler/selected-skin`,
or `~/.config/SMS-AnvilleSampler/selected-skin` when XDG_CONFIG_HOME is absent or
relative. A selection writes the preference atomically. An unavailable config
folder reports that the appearance changed but could not be remembered.

The preference stores the skin's subfolder name, not its display name or an
absolute path. Use matching subfolder names across LV2/VST3 collections to
share the choice. New editors restore it; already-open editors retain their
own appearance. A missing or unavailable remembered skin uses Default. A forced
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
| `[metrics]` | Decimal numbers; alpha values 0–1, radii/outline width 0–24 logical pixels |
| `[strings]` | Exact built-in label/format on the left, replacement text on the right |
| `[images]` | `background` and `logo`, each a relative image filename inside the skin |

For example:

```ini
[skin]
version=1
[colors]
controlAccent=#58B8E8
selection=#80D0FF
[metrics]
materialGrainAlpha=0.02
[strings]
PLAY=GO
SMS-ANVILLE SAMPLER=MY SAMPLER
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

Use PNG artwork (the supplied example does). NanoVG also accepts JPEG. The
background stretches across the 1344 × 756 logical chassis, behind the panels,
controls, and screws. It does not replace individual control artwork. The logo
stretches into the existing 170 × 78 header rectangle, drawn at 48% opacity.
Transparent pixels reveal the built-in rendering. Assets load once on the
first draw, when the graphics context is ready.

Missing settings and invalid individual colors, numbers, or images keep their
built-in defaults. Unknown settings are ignored. A missing, malformed,
oversized, or unsupported manifest uses the complete built-in theme. The
manifest limit is 64 KiB; string keys/values have a 512-byte limit and must be
valid UTF-8 without control characters. Absolute paths, `..`, and symlinks
escaping the skin directory are rejected. Each editor owns its palette; skin
loading and preference I/O never run in the
audio callback. Image replacement waits for the next draw with a current
graphics context.
