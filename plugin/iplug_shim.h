// AudioDSPTools' resampler normally builds inside iPlug2 and expects these two constants
// from it. We build it standalone, so define them here. Include before ResamplingContainer.h.
#pragma once

namespace iplug {
static constexpr double PI = 3.14159265358979323846264338327950288;
}

namespace dsp {
static constexpr int DEFAULT_BLOCK_SIZE = 1024;
}
