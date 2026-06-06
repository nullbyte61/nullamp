# Changelog

All notable changes to this project are documented here. The format is based on
[Keep a Changelog](https://keepachangelog.com/), and the project follows
[Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added
- Resizable GUI window (uniform scaling, aspect ratio preserved).
- Knob interaction: double-click resets to default, scroll-wheel adjusts.

## [0.2.0] - 2026-06-06

Matches the reference NAM plugin's signal chain.

### Added
- Noise gate (trigger reads the input; gain is applied after the model).
- 3-band tone stack: Bass (low shelf, 150 Hz), Mid (peaking, 425 Hz), Treble (high shelf, 1800 Hz).
- Calibrated output mode and per-model input-level calibration (`Input Calibration`, dBu).
- Functional test suite (`chain_test`) covering the tone stack and noise gate.

### Changed
- Output mode now offers Raw / Normalized / Calibrated.

## [0.1.0] - 2026-06-05

First release: a Linux NAM player with a GUI.

### Added
- Neural amp inference via NeuralAmpModelerCore, validated bit-identical to the upstream `render` tool.
- Automatic host↔model sample-rate conversion.
- Real-time-safe background loading of models and IRs with lock-free hand-off.
- Cabinet IR loader, DC blocker, input/output gain, and Raw/Normalized output modes.
- Custom NanoVG GUI with model/IR file pickers, knobs, and I/O meters.
- LV2, CLAP, VST3, and standalone JACK builds from a single codebase.
- Factory presets, Arch `PKGBUILD`, CPU-headroom benchmark, and CI.
