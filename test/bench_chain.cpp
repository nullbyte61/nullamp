// Measures how fast a model runs as a real-time factor: seconds of audio per second of
// wall clock. Above 1 means it keeps up, below 1 means it can't. Runs a few sample rates
// and block sizes so resampling cost shows up.
//
//   bench_chain <model.nam> [seconds]
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <vector>

#include "NAM/dsp.h"
#include "NamModel.hpp"

namespace {
double benchOnce(const char* modelPath, double hostRate, int block, double seconds)
{
    auto model = NamModel::load(std::filesystem::path(modelPath));
    if (!model)
        return -1.0;
    model->prepare(hostRate, block, /*doPrewarm=*/true);

    const long totalFrames = static_cast<long>(hostRate * seconds);
    std::vector<NAM_SAMPLE> in(block, 0.01), out(block, 0.0);

    auto t0 = std::chrono::high_resolution_clock::now();
    for (long done = 0; done < totalFrames; done += block)
        model->process(in.data(), out.data(), block);
    auto t1 = std::chrono::high_resolution_clock::now();

    const double wall = std::chrono::duration<double>(t1 - t0).count();
    return seconds / wall; // real-time factor
}
} // namespace

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        std::fprintf(stderr, "Usage: bench_chain <model.nam> [seconds]\n");
        return 2;
    }
    const char* modelPath = argv[1];
    const double seconds = argc > 2 ? std::atof(argv[2]) : 5.0;

    std::printf("Model: %s   (%.0fs of audio per case)\n", modelPath, seconds);
    std::printf("%-10s %-8s %-12s %s\n", "host rate", "block", "RTF", "headroom");

    const double rates[] = {48000.0, 96000.0};
    const int blocks[] = {64, 128, 256};
    for (double r : rates)
        for (int b : blocks)
        {
            const double rtf = benchOnce(modelPath, r, b, seconds);
            if (rtf < 0)
            {
                std::printf("failed to load model\n");
                return 1;
            }
            std::printf("%-10.0f %-8d %-12.1f %s\n", r, b, rtf, rtf >= 1.0 ? "OK" : "UNDERRUN");
        }
    return 0;
}
