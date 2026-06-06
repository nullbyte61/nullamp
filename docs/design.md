# How Nullamp works

This is a short tour of the codebase for anyone hacking on it. For building and using the plugin, see
the [README](../README.md).

## The short version

Nullamp doesn't reimplement any DSP. It wraps two existing libraries from the NAM author:

- `NeuralAmpModelerCore` runs the neural net that turns a DI signal into an amp sound.
- `AudioDSPTools` provides the cabinet IR convolution, the biquad filters (tone stack, DC blocker),
  the noise gate, the sample-rate resampler, and WAV loading.

Both are pulled in as git submodules under `deps/` and compiled straight into a small static library
(`nam_core`). The plugin itself is built with DPF (the DISTRHO Plugin Framework), which turns one set
of source files into LV2, CLAP, VST3, and a standalone JACK app. Our own code is just the glue: a
model wrapper, an async loader, a tone-stack port, the parameter/state handling, and the GUI.

## Layout

```
plugin/
  NamModel.{hpp,cpp}   loaded model + sample-rate adaptation
  AsyncLoader.hpp      background loader with a lock-free hand-off to the audio thread
  ToneStack.{h,cpp}    3-band tone stack, ported from the NAM plugin
  NamPlugin.cpp        the DPF Plugin: parameters, state, and the run() loop
  iplug_shim.h         two constants the AudioDSPTools resampler expects from iPlug2
ui/
  NamUI.cpp            the NanoVG GUI
test/                  offline renderer, benchmark, functional tests
```

## Signal chain

`run()` processes one block at a time:

```
in -> input gain (+ calibration) -> noise gate trigger -> NAM model -> noise gate gain
   -> tone stack -> cab IR -> DC blocker -> output gain (Raw / Normalized / Calibrated) -> out
```

Everything past the host's float buffers runs in double, which is what the core and AudioDSPTools use,
so there are no extra conversions between stages.

## Things worth knowing

A few details that aren't obvious from reading the code top to bottom:

**The core does not resample.** A model expects audio at the rate it was trained at. If the session
runs at a different rate, `NamModel` wraps the model in an AudioDSPTools `ResamplingContainer` so the
model always sees its own rate.

**Loading happens off the audio thread.** Reading a `.nam` file allocates and does I/O, so it can't
run in `run()`. `AsyncLoader` does the load and prewarm on a worker thread, then hands the finished
object to the audio thread through a single atomic slot. The audio thread swaps it in and hands the
old one back to be freed off-thread. It never allocates, locks, or calls delete itself.

**Output matches the upstream tool exactly.** The core's I/O type is `double` by default, but its
internals are float either way. By keeping the double boundary and converting only at the host's float
buffers (the same thing the upstream `render` tool does), Nullamp's output is bit-identical to it.
`test/ab_test.sh` checks this against every bundled example model.

**The architectures self-register.** WaveNet, ConvNet, and LSTM register themselves with file-scope
static objects. A plain static library would drop those objects because nothing references them, so
`nam_core` is linked with whole-archive (`$<LINK_LIBRARY:WHOLE_ARCHIVE,nam_core>`). Without it you get
"No config parser registered for architecture".

**The resampler needs two iPlug2 constants.** AudioDSPTools normally builds inside iPlug2, so its
Lanczos resampler refers to `iplug::PI` and `dsp::DEFAULT_BLOCK_SIZE`. `iplug_shim.h` supplies both so
it builds standalone.

**The file browser pulls in GNU extensions.** Enabling DPF's file dialogs drags in code that uses the
GNU `typeof` keyword, so the build uses `gnu++20` rather than strict `c++20`.

## Dependencies and licenses

Everything bundled is permissive: NeuralAmpModelerCore, AudioDSPTools, and the tone-stack port are
MIT; DPF is ISC. Nullamp itself is MIT.
