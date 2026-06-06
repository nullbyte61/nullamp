// Functional checks for the tone stack and noise gate (no model involved):
//   tone stack: bass boosts/cuts lows, treble boosts highs
//   noise gate: a signal below threshold is attenuated, above it passes
#include <cmath>
#include <cstdio>
#include <vector>

#include "NoiseGate.h"
#include "ToneStack.h"

namespace {
constexpr double kSR = 48000.0;
constexpr int kN = 4096;

double rms(const double* x, int n)
{
    double s = 0.0;
    for (int i = 0; i < n; ++i)
        s += x[i] * x[i];
    return std::sqrt(s / n);
}

std::vector<double> sine(double freq)
{
    std::vector<double> x(kN);
    for (int i = 0; i < kN; ++i)
        x[i] = std::sin(2.0 * M_PI * freq * i / kSR);
    return x;
}

double toneRms(double bass, double mid, double treble, std::vector<double> in)
{
    NamToneStack ts;
    ts.Reset(kSR, kN);
    ts.SetParam("bass", bass);
    ts.SetParam("middle", mid);
    ts.SetParam("treble", treble);
    double* p[1] = {in.data()};
    double** o = ts.Process(p, 1, kN);
    return rms(o[0], kN);
}

int fails = 0;
void check(bool ok, const char* what)
{
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok)
        ++fails;
}
} // namespace

int main()
{
    std::puts("Tone stack:");
    {
        auto low = sine(80.0);
        const double flat = toneRms(5, 5, 5, low);
        const double boost = toneRms(10, 5, 5, low);
        const double cut = toneRms(0, 5, 5, low);
        std::printf("    80 Hz  cut=%.3f flat=%.3f boost=%.3f\n", cut, flat, boost);
        check(boost > flat * 1.2 && cut < flat * 0.85, "bass boosts / cuts low end");
    }
    {
        auto high = sine(6000.0);
        const double flat = toneRms(5, 5, 5, high);
        const double boost = toneRms(5, 5, 10, high);
        std::printf("    6 kHz  flat=%.3f boost=%.3f\n", flat, boost);
        check(boost > flat * 1.1, "treble boosts highs");
    }

    std::puts("Noise gate:");
    {
        // Quiet tone (~-60 dBFS), threshold -40 dB -> should close and attenuate.
        std::vector<double> quiet(kN);
        for (int i = 0; i < kN; ++i)
            quiet[i] = 0.001 * std::sin(2.0 * M_PI * 220.0 * i / kSR);

        dsp::noise_gate::Trigger trig;
        dsp::noise_gate::Gain gain;
        trig.AddListener(&gain);
        trig.SetSampleRate(kSR);
        trig.SetParams(dsp::noise_gate::TriggerParams(0.01, -40.0, 0.1, 0.005, 0.01, 0.05));

        double outRms = 0.0;
        for (int b = 0; b < 30; ++b) // let the gate settle closed
        {
            double* p[1] = {quiet.data()};
            trig.Process(p, 1, kN);
            double** g = gain.Process(p, 1, kN);
            if (b == 29)
                outRms = rms(g[0], kN);
        }
        const double inRms = rms(quiet.data(), kN);
        std::printf("    quiet  in=%.5f out=%.5f\n", inRms, outRms);
        check(outRms < inRms * 0.25, "below-threshold signal attenuated");
    }
    {
        // Loud tone (~-6 dBFS), above threshold -> passes mostly intact.
        std::vector<double> loud(kN);
        for (int i = 0; i < kN; ++i)
            loud[i] = 0.5 * std::sin(2.0 * M_PI * 220.0 * i / kSR);

        dsp::noise_gate::Trigger trig;
        dsp::noise_gate::Gain gain;
        trig.AddListener(&gain);
        trig.SetSampleRate(kSR);
        trig.SetParams(dsp::noise_gate::TriggerParams(0.01, -40.0, 0.1, 0.005, 0.01, 0.05));

        double outRms = 0.0;
        for (int b = 0; b < 5; ++b)
        {
            double* p[1] = {loud.data()};
            trig.Process(p, 1, kN);
            double** g = gain.Process(p, 1, kN);
            if (b == 4)
                outRms = rms(g[0], kN);
        }
        const double inRms = rms(loud.data(), kN);
        std::printf("    loud   in=%.5f out=%.5f\n", inRms, outRms);
        check(outRms > inRms * 0.9, "above-threshold signal passes");
    }

    std::printf("%s\n", fails == 0 ? "chain_test: PASS" : "chain_test: FAIL");
    return fails == 0 ? 0 : 1;
}
