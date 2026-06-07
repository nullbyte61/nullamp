// A loaded .nam model plus the resampler that lets the host run at any rate.
// load() and prepare() are heavy (I/O, allocation, prewarm) and must run off the
// audio thread; process() is real-time safe.
#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>

#include "NAM/dsp.h" // NAM_SAMPLE and nam::DSP

namespace dsp {
template <typename T, int NCHANS, std::size_t A>
class ResamplingContainer;
}

namespace nam {
class SlimmableModel;
}

class NamModel
{
public:
    ~NamModel();

    // Returns nullptr on failure or for a non-mono model.
    static std::unique_ptr<NamModel> load(const std::filesystem::path& path);

    void prepare(double hostSampleRate, int maxBlockSize, bool doPrewarm = true);
    void process(const NAM_SAMPLE* in, NAM_SAMPLE* out, int numFrames) noexcept;

    int    latencySamples() const noexcept { return mLatency; }
    double expectedSampleRate() const noexcept;
    bool   isResampling() const noexcept { return mNeedsResample; }

    bool   hasLoudness() const noexcept;
    double loudness() const noexcept;

    // Non-const because the core's level getters are non-const.
    bool   hasInputLevel() noexcept;
    double inputLevel() noexcept;
    bool   hasOutputLevel() noexcept;
    double outputLevel() noexcept;

    // "Quality" / slim: A2 and other slimmable models can trade fidelity for CPU.
    // setSlimmableSize is NOT real-time safe (locks/allocates); call it off the audio thread.
    bool isSlimmable() const noexcept { return mSlim != nullptr; }
    void setSlimmableSize(double value) noexcept; // value in [0, 1], 1 = full quality

private:
    NamModel() = default;

    std::unique_ptr<nam::DSP> mDsp;
    std::unique_ptr<dsp::ResamplingContainer<NAM_SAMPLE, 1, 12>> mResampler;
    nam::SlimmableModel* mSlim = nullptr; // non-owning; points into mDsp if it's slimmable
    bool mNeedsResample = false;
    int  mLatency = 0;
};
