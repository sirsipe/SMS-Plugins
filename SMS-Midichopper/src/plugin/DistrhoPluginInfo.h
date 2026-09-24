#pragma once

// DPF metadata. The DSP and UI share this file, so every format wrapper sees
// the same identity and port topology.
#define DISTRHO_PLUGIN_BRAND   "SudoMetalStudio"
#define DISTRHO_PLUGIN_NAME    "SMS-Midichopper"
#define DISTRHO_PLUGIN_URI     "https://github.com/sirsipe/SMS-Plugins/SMS-Midichopper"
#define DISTRHO_PLUGIN_CLAP_ID "io.github.sirsipe.midichopper"

#define DISTRHO_PLUGIN_BRAND_ID  SMSM
#define DISTRHO_PLUGIN_UNIQUE_ID MdCh

#define DISTRHO_PLUGIN_HAS_UI          1
#define DISTRHO_PLUGIN_IS_RT_SAFE      1
#define DISTRHO_PLUGIN_NUM_INPUTS      2
#define DISTRHO_PLUGIN_NUM_OUTPUTS     2
#define DISTRHO_PLUGIN_WANT_MIDI_INPUT 1
#define DISTRHO_PLUGIN_WANT_PARAMETER_VALUE_CHANGE_REQUEST 1
#define DISTRHO_PLUGIN_WANT_STATE      1
#define DISTRHO_PLUGIN_WANT_FULL_STATE 1
#ifdef MIDICHOPPER_VST3_UI_BRIDGE
# define DISTRHO_PLUGIN_WANT_DIRECT_ACCESS 1
#endif

#define DISTRHO_UI_FILE_BROWSER    1
#define DISTRHO_UI_USE_NANOVG      1
#define DISTRHO_UI_USER_RESIZABLE  1
#define DISTRHO_UI_DEFAULT_WIDTH   1040
#define DISTRHO_UI_DEFAULT_HEIGHT  680
