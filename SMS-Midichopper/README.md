# SMS-Midichopper

A live stereo chopping sampler for Linux. Arm it and tap a MIDI pad at each
slice boundary. The first tap starts recording; later taps finish the slice and
continue onto the next pad.

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
  libxrandr-dev libxcursor-dev libxinerama-dev libdbus-1-dev
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
2. Choose a bank, enable monitoring if needed, then click **ARM**. The first
   empty visible pad is selected; click another pad while idle to override it.
3. The first MIDI note-on starts the slice. Later note-ons close the current
   slice and continue at the next pad; their note identity does not choose it.
4. Click **FINALIZE** or return to **PLAY** to keep the last open slice.
5. Select Bank A–D and play from MIDI note 36 upward. The base is configurable.

The yellow outline is the last successfully played pad in Play and the current
or next capture destination in Arm. A manual Play bank change clears it.

The hamburger menu provides two MIDI bank modes. **All Banks** (the default)
assigns stable, gapless notes to exposed sample slots. **Selected Bank** reuses
the base-note range for the visible bank. With 16 pads and the default base,
Banks A–D use 36–51, 52–67, 68–83, and 84–99; eight-pad layouts use consecutive
eight-note ranges. A note-on activates its bank and the UI follows it.
Supporting hosts save that Active Bank change. Layout changes regroup slots
without changing notes. All Banks limits the effective base to 64 so every
layout fits MIDI 0–127.

The pad layout can be switched between 16 pads (4×4), 12 pads (3×4), and 8 pads
(4×2). Pad numbering runs from the bottom row upward to match common hardware.
Each bank retains 16 storage slots, so switching to a smaller layout hides the
unused slots without deleting their samples. Sequential capture skips hidden
slots and continues automatically into the next bank in Selected Bank mode. In
All Banks mode, capture and display instead use contiguous layout-sized pages.

In Play, open **Sample Editor** to edit a pad's region and ADSR non-destructively.
Side-panel clicks select and load pads without auditioning them. MIDI playback
selects populated pads, including retriggers. The
editor is unavailable in Arm so capture controls remain visible.

In Play or Sample Editor, right-click a pad to import, export, or clear it. See
[WAV files](Docs/WAV-FILES.md) for formats and Linux requirements. **Clear Pad**
requires confirmation.

Max Voices limits simultaneous pads. At the limit, a new trigger stops the
oldest voice; set it to one for monophonic playback.

Pre-roll moves boundaries slightly earlier to compensate for late taps. Fixed
mode records one fixed-length slice per note-on. Samples from all four banks are
stored with the DAW project as compact 16-bit stereo state.

Stereo LED rails show raw input on the left and final output on the right in
every mode, even with monitoring off. Yellow begins at −18 dBFS and red at
−6 dBFS.

For development and host/UI testing, see [Contributing](../CONTRIBUTING.md).
Licensed under the [MIT License](LICENSE).
