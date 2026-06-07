#include "NamModel.hpp"

#include <cmath>
#include <exception>

#include "NAM/get_dsp.h"
#include "NAM/slimmable.h"

#include "iplug_shim.h" // must precede ResamplingContainer.h (provides iplug::PI, dsp::DEFAULT_BLOCK_SIZE)
#include "ResamplingContainer/ResamplingContainer.h"

NamModel::~NamModel() = default;

std::unique_ptr<NamModel> NamModel::load(const std::filesystem::path& path)
{
    std::unique_ptr<nam::DSP> dsp;
    try
    {
        dsp = nam::get_dsp(path);
    }
    catch (const std::exception&)
    {
        return nullptr;
    }
    if (!dsp)
        return nullptr;
    // Mono models only.
    if (dsp->NumInputChannels() != 1 || dsp->NumOutputChannels() != 1)
        return nullptr;

    std::unique_ptr<NamModel> m(new NamModel());
    m->mSlim = dynamic_cast<nam::SlimmableModel*>(dsp.get());
    m->mDsp = std::move(dsp);
    return m;
}

void NamModel::setSlimmableSize(double value) noexcept
{
    if (mSlim != nullptr)
        mSlim->SetSlimmableSize(value);
}

double NamModel::expectedSampleRate() const noexcept
{
    return mDsp ? mDsp->GetExpectedSampleRate() : -1.0;
}

bool NamModel::hasLoudness() const noexcept
{
    return mDsp && mDsp->HasLoudness();
}

double NamModel::loudness() const noexcept
{
    return (mDsp && mDsp->HasLoudness()) ? mDsp->GetLoudness() : 0.0;
}

bool NamModel::hasInputLevel() noexcept
{
    return mDsp && mDsp->HasInputLevel();
}

double NamModel::inputLevel() noexcept
{
    return (mDsp && mDsp->HasInputLevel()) ? mDsp->GetInputLevel() : 0.0;
}

bool NamModel::hasOutputLevel() noexcept
{
    return mDsp && mDsp->HasOutputLevel();
}

double NamModel::outputLevel() noexcept
{
    return (mDsp && mDsp->HasOutputLevel()) ? mDsp->GetOutputLevel() : 0.0;
}

void NamModel::prepare(double hostSampleRate, int maxBlockSize, bool doPrewarm)
{
    const double modelRate = mDsp->GetExpectedSampleRate();

    if (modelRate > 0.0 && std::abs(modelRate - hostSampleRate) > 0.5)
    {
        mResampler = std::make_unique<dsp::ResamplingContainer<NAM_SAMPLE, 1, 12>>(modelRate);
        mResampler->Reset(hostSampleRate, maxBlockSize);

        // The encapsulated DSP can be handed blocks larger than the host block when
        // upsampling. ResamplingContainer::MaxEncapsulatedBlockSize == ceil(block * modelRate/hostRate).
        const int innerMax =
            static_cast<int>(std::ceil(static_cast<double>(maxBlockSize) * modelRate / hostSampleRate)) + 8;
        mDsp->Reset(modelRate, innerMax);
        mLatency = mResampler->GetLatency();
        mNeedsResample = true;
    }
    else
    {
        const double rate = modelRate > 0.0 ? modelRate : hostSampleRate;
        mDsp->Reset(rate, maxBlockSize);
        mLatency = 0;
        mNeedsResample = false;
    }

    if (doPrewarm)
        mDsp->prewarm();
}

void NamModel::process(const NAM_SAMPLE* in, NAM_SAMPLE* out, int numFrames) noexcept
{
    NAM_SAMPLE* inPtr[1] = {const_cast<NAM_SAMPLE*>(in)};
    NAM_SAMPLE* outPtr[1] = {out};

    if (!mNeedsResample)
    {
        mDsp->process(inPtr, outPtr, numFrames);
    }
    else
    {
        mResampler->ProcessBlock(inPtr, outPtr, numFrames,
                                 [this](NAM_SAMPLE** i, NAM_SAMPLE** o, int nf) { mDsp->process(i, o, nf); });
    }
}
