// DPF plugin and UI configuration: names, IDs, ports, and the parameter/state lists.

#ifndef DISTRHO_PLUGIN_INFO_H_INCLUDED
#define DISTRHO_PLUGIN_INFO_H_INCLUDED

#define DISTRHO_PLUGIN_BRAND   "nullbyte61"
#define DISTRHO_PLUGIN_NAME    "Nullamp"
#define DISTRHO_PLUGIN_URI     "https://github.com/nullbyte61/nullamp"
#define DISTRHO_PLUGIN_CLAP_ID "io.nullbyte61.nullamp"

#define DISTRHO_PLUGIN_BRAND_ID  Nb61
#define DISTRHO_PLUGIN_UNIQUE_ID Nula

#define DISTRHO_PLUGIN_HAS_UI         1   // custom NanoVG GUI
#define DISTRHO_PLUGIN_IS_RT_SAFE     1
#define DISTRHO_PLUGIN_WANT_STATE     1   // model/IR paths as host-saved state (atom:Path on LV2)
#define DISTRHO_PLUGIN_WANT_FULL_STATE 1  // host queries getState() to persist the paths
#define DISTRHO_PLUGIN_WANT_LATENCY   1   // report resampler latency to the host
#define DISTRHO_PLUGIN_WANT_PROGRAMS  1   // factory presets

#define DISTRHO_UI_USE_NANOVG         1   // GUI drawn with NanoVG
#define DISTRHO_UI_FILE_BROWSER       1   // requestStateFile() for model/IR pickers
#define DISTRHO_UI_USER_RESIZABLE     1   // window can be resized (UI scales, aspect kept)
#define DISTRHO_UI_DEFAULT_WIDTH      480
#define DISTRHO_UI_DEFAULT_HEIGHT     470

// NAM models are mono-in / mono-out.
#define DISTRHO_PLUGIN_NUM_INPUTS   1
#define DISTRHO_PLUGIN_NUM_OUTPUTS  1

enum Parameters {
    kParamInputGain = 0,    // dB, pre-NAM
    kParamGateThreshold,    // dB, noise gate (-100 = off)
    kParamBass,             // tone stack, 0..10 (5 = flat)
    kParamMid,
    kParamTreble,
    kParamToneStackBypass,  // bool: bypass the 3-band EQ
    kParamIRBypass,         // bool: bypass the cab IR
    kParamOutputGain,       // dB, post-chain
    kParamOutputMode,       // Raw / Normalized / Calibrated
    kParamInputCalibration, // dBu, for input level + Calibrated output
    kParamQuality,          // 0..1 slim size for slimmable models (1 = full quality)
    kParamMeterIn,          // output: input peak (linear), for the GUI meter
    kParamMeterOut,         // output: output peak (linear), for the GUI meter
    kParamCount
};

enum OutputMode {
    kOutputRaw = 0,
    kOutputNormalized,   // -18 via model loudness
    kOutputCalibrated,   // dBu via model output level + input calibration
    kOutputModeCount
};

enum States {
    kStateModelPath = 0,
    kStateIRPath,
    kStateCount
};

enum Programs {
    kProgramInit = 0,     // unity, Raw
    kProgramNormalized,   // unity, Normalized output
    kProgramCleanBoost,   // +4 in / -2 out, Raw
    kProgramCount
};

#endif // DISTRHO_PLUGIN_INFO_H_INCLUDED
