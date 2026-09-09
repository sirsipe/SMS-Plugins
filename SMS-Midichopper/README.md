# SMS-Midichopper

A live stereo chopping sampler for Linux, currently under development.
Arm it, start the vinyl or other audio source, and tap any MIDI pad at each
slice boundary. After the first tap starts recording, each tap finishes
the current slice and continues recording onto the next pad.

> **Disclaimer:** This is an AI-generated project, created under the supervision
> and testing of [SirSipe](https://github.com/sirsipe/).

![SMS-Midichopper interface](Docs/SMS-Midichopper-v0.0.3.png)

_Screenshot from version v0.0.3._

## Vision

See [Docs/VISION.md](Docs/VISION.md) for product direction and feature scope.

## Build

Ubuntu/Debian prerequisites:

```bash
sudo apt install build-essential cmake ninja-build pkg-config git \
  lv2-dev libgl1-mesa-dev libx11-dev libxext-dev \
  libxrandr-dev libxcursor-dev libxinerama-dev
```

From the repository root:

```bash
git submodule update --init --recursive
cmake -S SMS-Midichopper -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

The plug-in is produced at `build/bin/SMS-Midichopper.lv2`. Install it for
the current user with:

```bash
mkdir -p ~/.lv2
cp -a build/bin/SMS-Midichopper.lv2 ~/.lv2/
```

## Use

1. Insert **SMS-Midichopper** on a stereo Reaper track receiving audio and MIDI.
2. Select the first destination pad, enable monitoring if needed, then click **ARM**.
3. The first MIDI note-on starts the first slice. Each later note-on closes that
   slice and immediately starts the next consecutive pad.
4. Click **FINALIZE** or return to **PLAY** to keep the last open slice.
5. Select Bank A–D and play pads from MIDI note 36 upward; the base note is
   configurable.

The hamburger menu provides two MIDI bank modes. **All Banks** (the default)
assigns stable, gapless notes to the sequential sample slots exposed by the
layout. **Selected Bank** reuses the base-note range for whichever bank is
visible. With 16 pads per bank and the default base note, Banks A–D use 36–51,
52–67, 68–83, and 84–99. With eight pads they instead use consecutive
eight-note ranges. A
note-on activates the bank containing that sample. Hosts that support plug-in
input-parameter change requests also mirror that bank in the UI. Changing
layout regroups the same sample slots into differently sized bank pages without
changing a sample's note. Base notes above 64 are limited to 64 in this mode so
every layout can fit in MIDI's 0–127 note range.

The pad layout can be switched between 16 pads (4×4), 12 pads (3×4), and 8 pads
(4×2). Pad numbering runs from the bottom row upward to match common hardware.
Each bank retains 16 storage slots, so switching to a smaller layout hides the
unused slots without deleting their samples. Sequential capture skips hidden
slots and continues automatically into the next bank in Selected Bank mode. In
All Banks mode, capture and display instead use contiguous layout-sized pages.

Open **Sample Editor** to move the pad bank into the right panel and edit the
selected pad in the main view. Drag the waveform's start/end handles to choose
the playback region, then adjust attack, decay, sustain, and release. These
settings are stored independently for every pad and do not alter captured audio.

Max Voices limits how many pads may play simultaneously. When the limit is
reached, triggering another pad stops the oldest playing voice; setting it to
one provides monophonic playback.

Pre-roll moves boundaries slightly earlier to compensate for late taps. Fixed
mode records one fixed-length slice per note-on. Samples from all four banks are
stored with the DAW project as compact 16-bit stereo state.

For development and host/UI testing, see [Contributing](../CONTRIBUTING.md).
Licensed under the [MIT License](LICENSE).
