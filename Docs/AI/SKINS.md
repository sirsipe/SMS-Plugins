# Skin authoring reference

Audience: an LLM creating a skin, not changing the sampler's implementation.

## Required inputs and deliverables

Read the [skin guide](../../SMS-AnvilleSampler/Docs/SKINS.md) for manifest syntax,
image dimensions, activation, persistence, and fallback rules. Read
[placeholder/skin.ini](../../SMS-AnvilleSampler/skins/placeholder/skin.ini) for the
complete color/metric catalog, defaults, and standard label/format keys. If
either file is inaccessible, request its contents before claiming a complete
skin. Do not invent undocumented keys or assume CSS, SVG, fonts, scripts, or
layout definitions are supported.

Start from the placeholder and change values. Deliver one folder containing
`skin.ini` and every image it references. Set `[skin] name` to the requested
display name and keep `version=1`. Use a stable subfolder ID: preferences store
that folder name. Artwork is optional; omit an image entry to use the built-in
appearance for that slot. Package a selectable skin as
`skins/<id>/skin.ini`, or an override as `skin/skin.ini`, preserving adjacent
asset paths. Include short installation instructions and a preview if possible.

Create personal experiments under ignored `build/skins/<id>/` or outside the
checkout. Do not add personal skins, artwork, previews, or archives to Git.
[Anville Graffiti](../../SMS-AnvilleSampler/skins/AnvilleGraffiti/skin.ini),
[Finnish Dream](../../SMS-AnvilleSampler/skins/FinnishDream/skin.ini), and the
[placeholder](../../SMS-AnvilleSampler/skins/placeholder/) are maintained
repository skins; change bundled skins only for an explicitly requested product
change. Keep sampler code and the placeholder unchanged when authoring custom skins.

## Palette design

The template is the authoritative key/default inventory. These groups explain
how to choose values; the [adapter](../../Common-UI/DPF/SkinTheme.hpp) maps them
to [shared theme fields](../../Common-UI/DPF/Theme.hpp).

| Group | Fields and purpose |
| --- | --- |
| Base surfaces | `canvas`, `surface`, `surfaceRaised`, `outline`: canvas margins, inset surfaces, raised fills and lines |
| Text | `contentPrimary`, `contentSecondary`: primary labels and quieter annotations |
| Activity | `activityPlayback`, `activityCapture`, `selection`: waveforms, capture and selected regions/pads |
| Controls | `controlAccent`, `controlActive`, `controlActiveContent`, `intentDanger`: knobs/playback buttons, active surfaces/content, record and destructive actions |
| Meter zones | `meterGreen`, `meterYellow`, `meterRed`: stereo meter zones; `meterGreen` also colors PLAY in the header |
| Chassis | `chassisTop`, `chassisBottom`, `chassisEdge`: body gradient and edge |
| Panels | `recess`, `recessEdge`, `panelTop`, `panelBottom`: inset surfaces and panel gradients |
| Pads/controls | `rubberTop`, `rubberBottom`, `controlTop`, `controlBottom`: rubber pads and physical control gradients |
| Material details | `edgeHighlight`, `shadow`, `faderTop`, `faderBottom`: bevels, shadows and fader caps |
| Slot tints | The six `pad` entries recolor fixed saved palette slots; preserve their order |

Changing only `controlAccent` does not change every red element. Coordinate
activity, selection, active controls, danger, and meter colors when replacing
an accent family. Preserve useful distinctions between playback, recording,
selection and disabled controls. Keep text readable over every fill and image.

Logo halos use `logoGlow`, `logoGlowAlpha`, `logoGlowRadius` and
`logoGlowPulseSeconds`; see the skin guide for bounds, defaults and activation.
The renderer caches a white alpha mask and tints/pulses it at draw time,
with the original logo fully opaque over the halo.

Metrics control material grain, tint/shadow/bevel/disabled/hover opacity, panel
and control radii, and outline width. Use the template's exact keys and the
guide's bounds. Geometry and font sizes stay fixed; metrics cannot relocate or
resize widgets. `backgroundAlpha`, `padAlpha`, `padPanelAlpha` and
`controlPanelAlpha` independently control image, pad-surface, main pad-bank backing
and right-side backing opacity. See the skin guide
for bounds/defaults. Preserve label/selection opacity; fade every pad
surface layer, including shadows, tint, hover/activity fill, bevels and grain.
Panel gradient alpha also reveals artwork; pad gradients use `padAlpha`.
Some drawing primitives replace a color's alpha for hover/disabled rendering,
so alpha is not a universal opacity control. Test the rendered result.

## Artwork composition

Generate actual PNG/JPEG assets when illustrations or textures are requested.
Do not substitute an HTML mockup or bake plugin controls into the background.
The plugin draws panels, pads, screws, labels, meters and overlays over it.
Place strong artwork around the header, edges and panel gaps; use restrained
detail under text. The upper-left header contains the title and subtitle; the
upper-right contains mode text and the menu. Keep both readable.

Use the guide's canvas and logo dimensions/aspect ratios. A transparent logo
should preserve alpha; without a halo its fixed rendering opacity reduces contrast. The
background stretches across the chassis and does not tile or crop to preserve
aspect ratio. It does not skin each knob or pad individually.

## Translation and verification

Translate only values in `[strings]`; preserve exact source keys, spaces,
punctuation and Unicode. Replacements are literal text, not quoted strings.
Preserve every printf conversion byte-for-byte and in the same order; for
example, translate the words around `%02d` or `%+.2f`, not those tokens. Keep
escaped `%%`. Use short labels suited to narrow buttons and small knobs.
Keep technical identifiers and product names when appropriate. Skin display
names are used literally, rather than looked up in `[strings]`.

The template covers standard labels and numeric readouts. For exhaustive
in-app error translations, also inspect user-facing messages in
[UI/controller](../../SMS-AnvilleSampler/src/ui/MidichopperUI.cpp),
[cut controller](../../SMS-AnvilleSampler/src/ui/ChopEditorController.hpp),
[workflows](../../SMS-AnvilleSampler/src/plugin/PadWorkflows.cpp),
[file actions](../../SMS-AnvilleSampler/src/plugin/PadFileActions.cpp),
[adapter](../../SMS-AnvilleSampler/src/plugin/MidichopperPlugin.cpp), and
[WAV codec](../../Common-Src/Audio/WavCodec.cpp). Never translate protocol keys,
saved-state names or host parameter identities. OS dialogs and host interfaces
follow their own language settings.

Verify all referenced files exist, the manifest meets the guide's bounds,
and no image escapes its folder. In a skin-capable build, check discovery/name,
Default/custom switching, main/editor/capture views, menus, resizing, label
clipping and persisted selection. Inspect plugin-only screenshots. Follow the
[testing reference](TESTING.md) when host tooling is available; report actual
unavailable checks. Deliver asset paths and installation instructions.
