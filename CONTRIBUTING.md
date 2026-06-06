# Contributing to Nullamp

Thanks for your interest in improving Nullamp. Bug reports, fixes, features, and documentation are
all welcome.

## Getting started

```sh
git clone --recursive https://github.com/nullbyte61/nullamp.git
cd nullamp
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

## Before opening a pull request

Please make sure the test suite still passes:

```sh
test/ab_test.sh build   # inference must stay bit-identical to upstream `render`
build/chain_test        # tone stack + noise gate behaviour
```

- The `ab_test` gate must stay green. The model path is expected to match the upstream reference
  exactly, so a failure there means inference output drifted.
- Keep the audio thread real-time safe: no allocation, locking, or file I/O in `run()` or anything it
  calls. Heavy work (loading models/IRs, prewarming) belongs on the loader thread.
- Match the surrounding code style. A `.clang-format` is provided:

  ```sh
  clang-format -i plugin/*.cpp plugin/*.h ui/*.cpp test/*.cpp
  ```

## Reporting bugs

Open an issue with your distro, host/DAW, plugin format (LV2/CLAP/VST3), and steps to reproduce.
For audio glitches, the model and sample rate involved are especially helpful.

## Scope

Nullamp deliberately reuses upstream DSP (NeuralAmpModelerCore, AudioDSPTools). Changes to inference
or core DSP behaviour are usually better directed upstream; Nullamp focuses on the plugin shell, GUI,
and Linux packaging.
