# Working with WAV files

Right-click a pad in Play or Sample Editor:

- **Import WAV...** replaces that pad and resets its cut points and ADSR.
- **Export WAV...** writes the complete stored sample without processing.
- **Export Processed...** writes the selected region with ADSR applied. It is
  disabled while both region and ADSR use their defaults.

Import accepts mono or stereo WAV files up to 30 seconds long: 16-, 24-, or
32-bit integer PCM and 32-bit floating point, including common extensible WAV
headers. Mono is duplicated to stereo. Imported audio is embedded in the DAW
project, so the source file is not needed when the project is reopened.

Both exports are stereo 16-bit PCM WAV at the sample's stored rate. A missing
`.wav` suffix is added automatically; another suffix is rejected. Exporting
over an existing file replaces it after the desktop's normal confirmation.

On Linux, the plug-in requires `libdbus-1` and Export requires a working
`xdg-desktop-portal` FileChooser with a desktop backend such as GTK, GNOME, or
KDE. Import can fall back to DPF's built-in X11 browser when the portal is not
available. If Save cannot open, the plug-in explains the requirement and
disables Export for that UI session; Import and other functions remain usable.
In the current Linux VST3 build, imported audio plays and exports but its
overview waveform may not refresh because of a DPF state-transport limitation;
the primary LV2 build provides the complete workflow.
