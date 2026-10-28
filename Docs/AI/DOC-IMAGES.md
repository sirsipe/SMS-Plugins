# Documentation images

Use real plugin captures for documentation. Build the desired version using
[Development](DEVELOPMENT.md), then launch that bundle using the host/UI tools in
[Testing](TESTING.md). Reopen the host after rebuilding; verify the version in
its header before capturing.

## Capture inputs

Use the 4×4 pad layout and the same window size for every capture, normally
1344 × 756. Populate pads with related slices. Use colours sparingly to mark
two neighbouring groups, such as blue and orange 2×2 blocks; leave other
pads untinted. Select an untinted pad in Main View so selection does not obscure
a group colour. Show playback in Main View, a shaped envelope and edited region
in Sample Editor, and meaningful proposed boundaries in the cut views.

Within each view, keep audio, settings, selection, zoom and pending cuts unchanged
while switching appearances. Playback cursors and meters can naturally differ.
Close menus and dialogs and move the pointer away from controls before capturing
with `screenshot-window`. A held pad may remain pressed for Main View playback.
Capture only the plugin content, without host decorations or the desktop.
Inspect every capture for clipping, missing waveforms, overlays and incorrect skins.

## Assets and README layout

Store sixteen separate, full-resolution PNGs in `SMS-AnvilleSampler/Docs`:

| View | Default filename |
| --- | --- |
| Main View | `main-view.png` |
| Sample Editor | `sample-editor.png` |
| Split Sample | `split-sample.png` |
| Adjust Cut Points | `adjust-cut-points.png` |

For each view, add `-finnish-dream`, `-looks-like-wu`, and
`-white-cadillac-vinyl` before `.png` for the other appearances. Basic Usage
captures (`arm-view.png`, `context-menu.png`,
`hamburger-menu.png`, `knob-context-menu.png`) always use the built-in appearance. Keep working captures
under ignored build output.
For `hamburger-menu.png`, leave the hamburger and PAD LAYOUT submenu open so
all top-level actions and layout choices, including CLEAR PADS, are visible;
keep the pointer away from other menu rows. For `knob-context-menu.png`, open
TUNE's Settings menu and its Value Mode submenu, with all three modes visible.
These menu captures are exceptions to closing menus before capture.

Both READMEs share the Main View captures. The plugin README also shows each
editor in all four appearances. Markdown tables and linked HTML `img` elements
label the built-in appearance **Default** and show it prominently, with three
named skin previews at one-third its display width.
The Skins section uses three equally sized, separate Main View images. Every
image links to its original PNG; do not bake layouts, labels or scaling into assets.

The [additional-skins gallery](../../SMS-AnvilleSampler/additional-skins/README.md)
uses separate initial-state captures: empty pads, the default 4×4 layout, and
unchanged factory controls. Capture every available additional skin at 1344 × 756
and store the PNGs directly in `SMS-AnvilleSampler/additional-skins/`, named
after their skin folders (for example, `RedBlackSMSTag.png`), outside the
downloadable skin folders. Each preview links to its full-resolution image
and has a Download Directory link prefilled with that skin folder's GitHub URL.
Keep the manual download section last.

Check every image/link target, including HTML `src` attributes, and inspect the
README layout. Update the capture-version caption. Run
`python3 scripts/check_docs.py` and `git diff --check`; the checker validates
Markdown link targets but does not inspect HTML image attributes or rendering.
