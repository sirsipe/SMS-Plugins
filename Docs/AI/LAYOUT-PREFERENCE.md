# Startup preferences and host precedence

Audience: agents changing layout, bank mode, skin or global knob defaults.
User behavior lives in the [plugin guide](../../SMS-AnvilleSampler/README.md#menus).
The [skin guide](../../SMS-AnvilleSampler/Docs/SKINS.md#remembering-a-choice)
owns platform config paths.

## Shared defaults contract

[DefaultsFile](../../Common-Src/IO/DefaultsFile.hpp) stores all choices in one
flat `defaults.ini`, without sections or a version header:

| Key | Value | Missing/invalid fallback |
| --- | --- | --- |
| `layout` | `16`, `12`, `8` (pad count, not parameter index) | 16 pads |
| `bank_mode` | `selected`, `all` | All Banks |
| `skin` | Collection subfolder ID; empty means Default | Default |
| `knob.output`, `knob.pan`, `knob.tune`, `knob.lowpass`, `knob.highpass` | `KC1;mode;minimum;maximum` | Full-range Normal per control |

Knob mode is `0` Normal, `1` Quantized or `2` Zero Snap. Bounds use native
parameter units, finite locale-independent floats, and each parameter's range;
minimum must be less than maximum. Quantized requires an available grid point;
Zero Snap requires a range containing zero. The
[mixing contract](MIXING.md) owns mapping behavior. Validators live in
[GlobalValueMode.hpp](../../SMS-AnvilleSampler/src/plugin/GlobalValueMode.hpp).

Keys are case-sensitive ASCII letters/digits plus `_`, `.` or `-`, at most 128
bytes. Values are at most 4096 bytes without NUL/CR/LF; files are bounded to
64 KiB and 256 distinct valid keys. Blank lines and whole-line `#`/`;` comments
are ignored. Spaces around keys/unquoted values are trimmed; inline comment
characters and `=` remain literal. Double-quoted values preserve surrounding
spaces; only backslash and double-quote escapes are accepted. The final valid duplicate wins.
Malformed individual lines are ignored without losing valid siblings.

`set()` serializes cooperating writers across threads/processes, rereads the
latest file, merges one key and atomically replaces it. Unknown valid keys and
sibling settings survive; formatting/comments are rewritten. Oversized,
unreadable or excessive-key files reject writes without replacement. Reads
return available valid keys, capped at 256; unreadable/oversized files fall back.
No persistent lock or temporary file remains after successful replacement.
Legacy separate preference files are no longer read or migrated.

[LayoutPreference](../../SMS-AnvilleSampler/src/plugin/LayoutPreference.hpp),
[BankModePreference](../../SMS-AnvilleSampler/src/plugin/BankModePreference.hpp),
[KnobPreference](../../SMS-AnvilleSampler/src/plugin/KnobPreference.hpp) and
[SkinPreference](../../Common-UI/SkinCatalog.hpp) validate their own values.
Accepted explicit menu choices and knob mode/range edits write defaults,
including reselection. Automation, restoration and parameter callbacks never write them.
Failures leave the setting usable and report through UI status. All preference
I/O is outside audio callbacks.

## Startup and restoration

`pad_layout` and `midi_bank_mode` retain their host identities, ranges and factory
values: zero (16 pads) and one (All Banks). Every host-supplied value wins,
including zero. No timing heuristic, sentinel, editor-open action or preference
watch overrides host state.

[MidichopperPlugin](../../SMS-AnvilleSampler/src/plugin/MidichopperPlugin.cpp)
reads construction-time defaults for each non-dummy instance. Layout/bank values
seed current parameters and immutable runtime metadata defaults. Pinned DPF
VST3 seeds its cache from metadata and suppresses matching integer setters;
seeding only current parameters could lose restored host values. LV2 hosts
normally send fixed metadata defaults on initial processing, overriding remembered
layout/bank mode. VST3/CLAP hosts that explicitly initialize parameters also win.

Knob settings seed instance-owned desired/audio settings and private durable
`global_*_settings` state metadata. Valid host state replaces them, including
factory Normal/full-range values, without writing defaults. Missing keys retain
the startup choice; invalid restoration retains the accepted setting. Raw host
parameters remain unchanged. Control-side state publication uses
`RealtimeLatestMailbox`; the callback consumes settings without locks or I/O.

Dummy discovery/export uses factory values without preference reads. Existing
instances retain their accepted settings and immutable initial metadata when
another instance saves defaults. Skin remains editor-local, outside project
state. **CLEAR PADS** preserves every global control, knob mode/range, skin and
startup preference.

## Validation

`defaults-file` covers bounds, malformed lines, quotes/Unicode, unknown-key
merging, failure preservation and concurrent writers/readers; process writers
are exercised on POSIX. `layout-preference` covers all choices, invalid values,
Unicode paths and sibling-key preservation. Both join the
[native filesystem suite](DEVELOPMENT.md#native-skin-filesystem-tests).

`layout-initialization-*` exercises normal/VST3 adapters through DPF
PluginExporter: current/default coherence, host precedence, immutable snapshots,
dummy metadata and CLEAR PADS preservation of globals/defaults.
`global-value-mode-*` covers knob codecs, preferences, host restoration, startup
audio, effective values and state transport. Adapter suites run on Unix;
Windows Roaming AppData cannot be isolated through environment overrides.

In isolated host configs, verify fresh instances, opposite project defaults,
reopened existing editors and failed writes. Linux Carla checks at 48 kHz/2048 frames verified VST3 remembered layout, bank,
skin and knob choices in one file, fresh-instance startup, host state winning
without preference writes, and CLEAR PADS preserving globals/defaults. VST3
menu, numeric-entry and range-restore checks supplement adapter tests. LV2 host
initialization overrides remembered layout/bank mode. Native Windows/macOS DAW
and CLAP host checks remain unverified. See [testing](TESTING.md) for procedures.
