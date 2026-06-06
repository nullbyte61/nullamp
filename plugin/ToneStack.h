// 3-band Bass/Mid/Treble tone stack, ported from NeuralAmpModelerPlugin (MIT) so it
// matches the official EQ. Each control is 0..10 with 5 being flat. Frequencies and
// gains are in ToneStack.cpp.
#pragma once

#include <string>

#include "RecursiveLinearFilter.h"

class NamToneStack
{
public:
    NamToneStack()
    {
        SetParam("bass", 5.0);
        SetParam("middle", 5.0);
        SetParam("treble", 5.0);
    }

    DSP_SAMPLE** Process(DSP_SAMPLE** inputs, int numChannels, int numFrames);
    void Reset(double sampleRate, int maxBlockSize);
    void SetParam(const std::string& name, double val); // val in [0, 10], 5 = noon

private:
    double mSampleRate = 0.0;
    int mMaxBlockSize = 0;
    recursive_linear_filter::LowShelf mBass;
    recursive_linear_filter::Peaking mMid;
    recursive_linear_filter::HighShelf mTreble;
    double mBassVal = 5.0;
    double mMiddleVal = 5.0;
    double mTrebleVal = 5.0;
};
