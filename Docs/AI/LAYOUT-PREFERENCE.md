# Layout and bank-mode startup preferences

Audience: agents changing layout/bank-mode selection, startup defaults, or host restoration.
User behavior and storage live in the [plugin guide](../../SMS-AnvilleSampler/README.md#menus);
the [skin guide](../../SMS-AnvilleSampler/Docs/SKINS.md#remembering-a-choice) owns platform folder paths.

## Contract

`pad_layout` and `midi_bank_mode` remain host parameters with unchanged IDs,
bounds, and factory defaults: zero (16 pads) and one (All Banks). Every
host-supplied value takes precedence, including zero. No sentinel, timing
heuristic, editor-open action, or preference watch overrides host state.

[LayoutPreference.hpp](../../SMS-AnvilleSampler/src/plugin/LayoutPreference.hpp)
stores `SMS-LAYOUT-PREFERENCE 1`, a newline, the pad count (16, 12, or 8), and a
final newline.
[BankModePreference.hpp](../../SMS-AnvilleSampler/src/plugin/BankModePreference.hpp)
stores `SMS-BANK-MODE-PREFERENCE 1`, a newline, `selected` or `all`, and a final
newline in `selected-bank-mode`. Each reader rejects anything else and files
over 64 bytes, falling back to its factory default. The files are independent.
Only accepted explicit menu choices write them, including reselection.
Automation, project restoration, and parameter callbacks never write them. Failed
writes leave the selected setting usable and report through the UI status.

[MidichopperPlugin.cpp](../../SMS-AnvilleSampler/src/plugin/MidichopperPlugin.cpp)
reads once during non-dummy construction and uses the immutable snapshot for both
the current parameter and runtime metadata default. Pinned DPF VST3 initializes
its cache from metadata defaults, then suppresses matching boolean/integer setters;
seeding only current values could discard restored host values.
Dummy discovery/export instances never read preferences, keeping generated
LV2 metadata independent of the build user's configuration.

LV2 hosts normally send the fixed metadata default on initial processing. This
overrides the preference. The wrapper cannot distinguish a new host default from
a restored factory value; host authority takes priority. VST3/CLAP hosts explicitly
sending their own initialization also override the remembered default.

Native config discovery and atomic replacement use shared
[FileIO](../../Common-Src/IO/FileIO.hpp). No filesystem operation runs in audio
callbacks. Existing instances retain their settings and initial metadata defaults
when another instance saves new preferences.

## Validation

`layout-preference` covers both preferences: Unicode paths, all choices, exact
serialization, malformed/oversized files, invalid writes, failures, independent
files, and skin sibling paths.
The native [filesystem suite](DEVELOPMENT.md#native-skin-filesystem-tests) includes
it on Linux, Windows, and macOS.

`layout-initialization-*` covers the normal and VST3 bridge adapters through DPF
PluginExporter: missing preference, remembered current/default coherence,
host overrides (16 pads and both bank modes), unchanged preferences, immutable
instance snapshots, independent files, and dummy factory metadata. It runs on
Unix; Windows native Roaming AppData cannot be redirected by environment overrides for isolated adapter testing.

In an isolated user config, select 8/12/16 pads through the menu and create a new
VST3 instance. Restore a project/preset with a different layout, including 16,
and confirm the host wins without changing the preference. Reopen an existing
editor after another instance changes the preference; its layout must persist.
Repeat for Selected Bank/All Banks, restoring the opposite mode. Verify LV2
host initialization wins and check missing/invalid files and failed writes.
These host checks supplement CTest; a launch alone is insufficient.

Linux Carla 2.5.10 at 48 kHz/2048 frames verified VST3 menu persistence and a
fresh process reopening with 12 pads. A fresh VST3 instance with an 8-pad
preference restored a saved 16-pad state; both the UI and host parameter showed
16 while the preference stayed at 8. LV2 initialized to the host's 16-pad value
despite a remembered 12, and explicitly reselecting 16 saved it. Bank-mode menu
selection persisted Selected Bank across fresh VST3 processes. Fresh VST3
instances restored each saved mode over the opposite preference without changing
the file; UI selections matched host parameters. LV2 initialized to All Banks
over a Selected Bank preference. Native Windows/macOS hosts and a CLAP host
remain untested.
