// Ported from sdatkinson/NeuralAmpModelerPlugin ToneStack.cpp (MIT).
#include "ToneStack.h"

DSP_SAMPLE** NamToneStack::Process(DSP_SAMPLE** inputs, int numChannels, int numFrames)
{
    DSP_SAMPLE** bass = mBass.Process(inputs, numChannels, numFrames);
    DSP_SAMPLE** mid = mMid.Process(bass, numChannels, numFrames);
    DSP_SAMPLE** treble = mTreble.Process(mid, numChannels, numFrames);
    return treble;
}

void NamToneStack::Reset(double sampleRate, int maxBlockSize)
{
    mSampleRate = sampleRate;
    mMaxBlockSize = maxBlockSize;
    // Refresh the params at the new sample rate.
    SetParam("bass", mBassVal);
    SetParam("middle", mMiddleVal);
    SetParam("treble", mTrebleVal);
}

void NamToneStack::SetParam(const std::string& name, double val)
{
    if (name == "bass")
    {
        mBassVal = val;
        const double gainDB = 4.0 * (val - 5.0); // +/- 20
        recursive_linear_filter::BiquadParams p(mSampleRate, 150.0, 0.707, gainDB);
        mBass.SetParams(p);
    }
    else if (name == "middle")
    {
        mMiddleVal = val;
        const double gainDB = 3.0 * (val - 5.0); // +/- 15
        const double q = gainDB < 0.0 ? 1.5 : 0.7; // wider on boost to sound less honky
        recursive_linear_filter::BiquadParams p(mSampleRate, 425.0, q, gainDB);
        mMid.SetParams(p);
    }
    else if (name == "treble")
    {
        mTrebleVal = val;
        const double gainDB = 2.0 * (val - 5.0); // +/- 10
        recursive_linear_filter::BiquadParams p(mSampleRate, 1800.0, 0.707, gainDB);
        mTreble.SetParams(p);
    }
}
