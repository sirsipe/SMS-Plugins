# Working with WAV files

Right-click a pad in Play or Sample Editor:

- **Import WAV...** replaces that pad and resets its cut points, mixer, and ADSR.
- **Export WAV...** writes the complete stored sample without processing.
- **Export Processed...** writes the selected region with per-pad Gain, Pan, Tune
  varispeed, Forward/Reverse direction, and ADSR applied. It is disabled while all editor controls use
  their defaults. Reverse flips the whole source before applying Start/End.
  Global Mixer settings are excluded.

Import accepts mono or stereo WAV files up to eight minutes long, subject to
free sampler storage: 16-, 24-, or
32-bit integer PCM and 32-bit floating point, including common extensible WAV
headers. Mono is duplicated to stereo. A long song can occupy one pad and then
be split onto following pads. Imported audio is embedded in the DAW project,
so the source file is not needed when the project is reopened. Large songs can
make project files and save times substantially larger.

## Stored sample rates

All pad audio is stored at **96 kHz or below**, including live recordings,
imports, pasted audio and samples restored from older projects. Higher-rate
audio is converted with an anti-alias filter; lower rates are preserved without
upsampling. Conversion keeps duration to the nearest stored frame, retaining at
least one frame for nonempty audio. Original files are unchanged, but the
original higher-rate PCM is not retained in the sampler or its next project save.
Both WAV exports therefore use a rate of at most 96 kHz.

Hosts can still run above 96 kHz. Input monitoring stays at the host rate, while
recording is converted to the capped rate. Chop boundaries, pre-roll and fixed
durations follow host timing and are quantized to stored frames. Playback still
converts from the stored rate and applies tuning. See the
[processing benchmarks and their limits](RESAMPLING-BENCHMARKS.md).

## Project storage and export

New saves preserve stored finite float32 samples exactly, including quiet detail and
levels above full scale. Older 16-bit projects still load and retain their
decoded samples when resaved, subject to the rate cap above; previously lost precision cannot be recovered.
Older plugin versions cannot load the new sample format. Sample data uses
roughly twice the uncompressed storage of 16-bit saves.

Transfers keep input monitoring and other pads playing. A recording target or
a pad changed during import is rejected; retry after the conflicting action.

Both exports are stereo 16-bit PCM WAV at the sample's stored rate, independent
of project-state precision. A missing
`.wav` suffix is added automatically; another suffix is rejected. Exporting
over an existing file replaces it after the desktop's normal confirmation.

On Linux, the plug-in requires `libdbus-1` and Export requires a working
`xdg-desktop-portal` FileChooser with a desktop backend such as GTK, GNOME, or
KDE. Import can fall back to DPF's built-in X11 browser when the portal is not
available. If Save cannot open, the plug-in explains the requirement and
disables Export for that UI session; Import and other functions remain usable.
In the Linux VST3 build, imported audio and its waveform appear in the Sample
Editor. A Carla host test also confirmed editor settings after switching pads.
Long-song restoration, including edited Start and gain, passed in Carla VST3
after removing the source copy. See the
[verified host scope and remaining limits](../../Docs/AI/DPF-VST3-CONSTRAINTS.md).
