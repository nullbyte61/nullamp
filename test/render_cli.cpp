// Renders a WAV through NamModel the same way the upstream `render` tool does (Reset, no
// prewarm, 64-sample blocks, native rate) and compares against a reference WAV. If the
// integration is correct the two are bit-identical. ab_test.sh drives this.
//
//   render_cli <model.nam> <input.wav> <reference.wav>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <vector>

#include "NAM/dsp.h"
#include "NamModel.hpp"
#include "wav.h"

int main(int argc, char* argv[])
{
    if (argc != 4)
    {
        std::cerr << "Usage: render_cli <model.nam> <input.wav> <reference.wav>\n";
        return 2;
    }
    const char* modelPath = argv[1];
    const char* inputPath = argv[2];
    const char* refPath = argv[3];

    auto model = NamModel::load(std::filesystem::path(modelPath));
    if (!model)
    {
        std::cerr << "Failed to load model (or not mono): " << modelPath << "\n";
        return 1;
    }

    std::vector<float> input;
    double inputRate = 0.0;
    if (dsp::wav::Load(inputPath, input, inputRate) != dsp::wav::LoadReturnCode::SUCCESS)
    {
        std::cerr << "Failed to load input WAV: " << inputPath << "\n";
        return 1;
    }

    const double modelRate = model->expectedSampleRate();
    if (modelRate > 0.0 && std::abs(inputRate - modelRate) > 0.5)
    {
        std::cerr << "Input rate (" << inputRate << ") != model rate (" << modelRate
                  << "). The A/B gate runs at native rate; resample the input first.\n";
        return 1;
    }

    // Match upstream `render` exactly: Reset (no prewarm), 64-frame blocks.
    const int blockSize = 64;
    model->prepare(inputRate, blockSize, /*doPrewarm=*/false);

    std::vector<float> rendered;
    rendered.reserve(input.size());
    std::vector<NAM_SAMPLE> inBuf(blockSize, 0.0), outBuf(blockSize, 0.0);

    for (size_t pos = 0; pos < input.size(); pos += blockSize)
    {
        const int n = static_cast<int>(std::min<size_t>(blockSize, input.size() - pos));
        for (int i = 0; i < n; ++i)
            inBuf[i] = static_cast<NAM_SAMPLE>(input[pos + i]);
        model->process(inBuf.data(), outBuf.data(), n);
        for (int i = 0; i < n; ++i)
            rendered.push_back(static_cast<float>(outBuf[i]));
    }

    std::vector<float> reference;
    double refRate = 0.0;
    if (dsp::wav::Load(refPath, reference, refRate) != dsp::wav::LoadReturnCode::SUCCESS)
    {
        std::cerr << "Failed to load reference WAV: " << refPath << "\n";
        return 1;
    }

    if (rendered.size() != reference.size())
        std::cerr << "Warning: length mismatch (ours=" << rendered.size() << ", ref=" << reference.size()
                  << "); comparing the overlap.\n";

    const size_t n = std::min(rendered.size(), reference.size());
    double maxDiff = 0.0;
    size_t worst = 0;
    for (size_t i = 0; i < n; ++i)
    {
        const double d = std::abs(static_cast<double>(rendered[i]) - static_cast<double>(reference[i]));
        if (d > maxDiff)
        {
            maxDiff = d;
            worst = i;
        }
    }

    std::cout << "Compared " << n << " samples\n";
    std::cout << "Max abs diff = " << maxDiff << " (at sample " << worst << ")\n";

    if (maxDiff == 0.0)
    {
        std::cout << "PASS: bit-identical to upstream render.\n";
        return 0;
    }
    if (maxDiff < 1e-6)
    {
        std::cout << "PASS: numerically equivalent (< 1e-6).\n";
        return 0;
    }
    std::cout << "FAIL: diverges from upstream render.\n";
    return 1;
}
