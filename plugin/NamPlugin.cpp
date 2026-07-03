// The DPF plugin. Signal chain in run():
//   input gain (+ calibration) -> noise gate -> model -> tone stack -> cab IR
//   -> DC blocker -> output gain (Raw/Normalized/Calibrated)
// Model and IR load on a worker thread (AsyncLoader) and swap in atomically, so run()
// stays allocation- and lock-free. Everything past the host buffers runs in double.

#include "DistrhoPlugin.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "AsyncLoader.hpp"
#include "NamModel.hpp"
#include "ToneStack.h"

#include "ImpulseResponse.h"
#include "NoiseGate.h"
#include "RecursiveLinearFilter.h"

START_NAMESPACE_DISTRHO

static constexpr double kNormalizationTargetDb = -18.0;
static constexpr double kDcBlockerFreqHz = 5.0;
static constexpr double kGateOffDb = -100.0;

class NamDpfPlugin : public Plugin
{
public:
    NamDpfPlugin()
        : Plugin(kParamCount, kProgramCount, kStateCount)
    {
        mGateTrigger.AddListener(&mGateGain);
        allocScratch(static_cast<int>(getBufferSize() > 0 ? getBufferSize() : 1024));
        // The loader thread applies the Quality (slim) value to the active model off-RT.
        mModelLoader.setPoll([this] { applySlim(); });
    }

protected:
    // --- Metadata -----------------------------------------------------------
    const char* getLabel() const override { return "Nullamp"; }
    const char* getDescription() const override
    {
        return "Neural Amp Modeler plugin (Linux-first DPF shell).";
    }
    const char* getMaker() const override { return "nullbyte61"; }
    const char* getHomePage() const override { return "https://github.com/nullbyte61/nullamp"; }
    const char* getLicense() const override { return "MIT"; }
    uint32_t getVersion() const override { return d_version(0, 3, 0); }
    int64_t getUniqueId() const override { return d_cconst('N', 'u', 'l', 'a'); }

    // --- Parameters ---------------------------------------------------------
    void initParameter(uint32_t index, Parameter& parameter) override
    {
        switch (index)
        {
        case kParamInputGain:
            parameter.hints = kParameterIsAutomatable;
            parameter.name = "Input Gain";
            parameter.symbol = "input_gain";
            parameter.unit = "dB";
            parameter.ranges = ParameterRanges(0.0f, -20.0f, 20.0f);
            break;
        case kParamGateThreshold:
            parameter.hints = kParameterIsAutomatable;
            parameter.name = "Gate Threshold";
            parameter.symbol = "gate_threshold";
            parameter.unit = "dB";
            parameter.ranges = ParameterRanges(-100.0f, -100.0f, 0.0f); // -100 = off
            break;
        case kParamBass:
            parameter.hints = kParameterIsAutomatable;
            parameter.name = "Bass";
            parameter.symbol = "bass";
            parameter.ranges = ParameterRanges(5.0f, 0.0f, 10.0f);
            break;
        case kParamMid:
            parameter.hints = kParameterIsAutomatable;
            parameter.name = "Middle";
            parameter.symbol = "middle";
            parameter.ranges = ParameterRanges(5.0f, 0.0f, 10.0f);
            break;
        case kParamTreble:
            parameter.hints = kParameterIsAutomatable;
            parameter.name = "Treble";
            parameter.symbol = "treble";
            parameter.ranges = ParameterRanges(5.0f, 0.0f, 10.0f);
            break;
        case kParamToneStackBypass:
            parameter.hints = kParameterIsAutomatable | kParameterIsBoolean;
            parameter.name = "EQ Bypass";
            parameter.symbol = "eq_bypass";
            parameter.ranges = ParameterRanges(0.0f, 0.0f, 1.0f);
            break;
        case kParamIRBypass:
            parameter.hints = kParameterIsAutomatable | kParameterIsBoolean;
            parameter.name = "IR Bypass";
            parameter.symbol = "ir_bypass";
            parameter.ranges = ParameterRanges(0.0f, 0.0f, 1.0f);
            break;
        case kParamModelBypass:
            parameter.hints = kParameterIsAutomatable | kParameterIsBoolean;
            parameter.name = "Model Bypass";
            parameter.symbol = "model_bypass";
            parameter.ranges = ParameterRanges(0.0f, 0.0f, 1.0f);
            break;
        case kParamOutputGain:
            parameter.hints = kParameterIsAutomatable;
            parameter.name = "Output Gain";
            parameter.symbol = "output_gain";
            parameter.unit = "dB";
            parameter.ranges = ParameterRanges(0.0f, -40.0f, 40.0f);
            break;
        case kParamOutputMode:
            parameter.hints = kParameterIsAutomatable | kParameterIsInteger;
            parameter.name = "Output Mode";
            parameter.symbol = "output_mode";
            parameter.ranges = ParameterRanges(0.0f, 0.0f, static_cast<float>(kOutputModeCount - 1));
            {
                ParameterEnumerationValue* const v = new ParameterEnumerationValue[3];
                v[0].value = static_cast<float>(kOutputRaw);        v[0].label = "Raw";
                v[1].value = static_cast<float>(kOutputNormalized); v[1].label = "Normalized";
                v[2].value = static_cast<float>(kOutputCalibrated); v[2].label = "Calibrated";
                parameter.enumValues.count = 3;
                parameter.enumValues.values = v;
                parameter.enumValues.restrictedMode = true;
            }
            break;
        case kParamInputCalibration:
            parameter.hints = kParameterIsAutomatable;
            parameter.name = "Input Calibration";
            parameter.symbol = "input_calibration";
            parameter.unit = "dBu";
            parameter.ranges = ParameterRanges(12.0f, -60.0f, 60.0f);
            break;
        case kParamQuality:
            parameter.hints = kParameterIsAutomatable;
            parameter.name = "Quality";
            parameter.symbol = "quality";
            parameter.ranges = ParameterRanges(1.0f, 0.0f, 1.0f); // only affects slimmable models
            break;
        case kParamMeterIn:
            parameter.hints = kParameterIsAutomatable | kParameterIsOutput;
            parameter.name = "Meter In";
            parameter.symbol = "meter_in";
            parameter.ranges = ParameterRanges(0.0f, 0.0f, 2.0f);
            break;
        case kParamMeterOut:
            parameter.hints = kParameterIsAutomatable | kParameterIsOutput;
            parameter.name = "Meter Out";
            parameter.symbol = "meter_out";
            parameter.ranges = ParameterRanges(0.0f, 0.0f, 2.0f);
            break;
        }
    }

    float getParameterValue(uint32_t index) const override
    {
        switch (index)
        {
        case kParamInputGain: return fInputGainDb;
        case kParamGateThreshold: return fGateThresholdDb;
        case kParamBass: return fBass;
        case kParamMid: return fMid;
        case kParamTreble: return fTreble;
        case kParamToneStackBypass: return fToneStackBypass ? 1.0f : 0.0f;
        case kParamIRBypass: return fIRBypass ? 1.0f : 0.0f;
        case kParamModelBypass: return fModelBypass ? 1.0f : 0.0f;
        case kParamOutputGain: return fOutputGainDb;
        case kParamOutputMode: return static_cast<float>(fOutputMode);
        case kParamInputCalibration: return fInputCalibrationDb;
        case kParamQuality: return mQuality.load(std::memory_order_relaxed);
        case kParamMeterIn: return fMeterIn;
        case kParamMeterOut: return fMeterOut;
        }
        return 0.0f;
    }

    void setParameterValue(uint32_t index, float value) override
    {
        switch (index)
        {
        case kParamInputGain: fInputGainDb = value; break;
        case kParamGateThreshold: fGateThresholdDb = value; break;
        case kParamBass: fBass = value; break;
        case kParamMid: fMid = value; break;
        case kParamTreble: fTreble = value; break;
        case kParamToneStackBypass: fToneStackBypass = value > 0.5f; break;
        case kParamIRBypass: fIRBypass = value > 0.5f; break;
        case kParamModelBypass: fModelBypass = value > 0.5f; break;
        case kParamOutputGain: fOutputGainDb = value; break;
        case kParamOutputMode: fOutputMode = static_cast<int>(value + 0.5f); break;
        case kParamInputCalibration: fInputCalibrationDb = value; break;
        case kParamQuality:
            // Stored atomically; the loader thread applies it to the model (not RT-safe).
            mQuality.store(value, std::memory_order_relaxed);
            mQualitySeq.fetch_add(1, std::memory_order_release);
            break;
        }
    }

    // --- Programs (factory presets) -----------------------------------------
    void initProgramName(uint32_t index, String& name) override
    {
        switch (index)
        {
        case kProgramInit: name = "Init"; break;
        case kProgramNormalized: name = "Normalized Out"; break;
        case kProgramCleanBoost: name = "Clean Boost"; break;
        }
    }

    void loadProgram(uint32_t index) override
    {
        // Common neutral chain; presets vary the mixer/mode. Model & IR are user state.
        fGateThresholdDb = kGateOffDb;
        fBass = fMid = fTreble = 5.0f;
        fToneStackBypass = false;
        fIRBypass = false;
        fModelBypass = false;
        fInputCalibrationDb = 12.0f;
        mQuality.store(1.0f, std::memory_order_relaxed);
        mQualitySeq.fetch_add(1, std::memory_order_release);
        switch (index)
        {
        case kProgramInit:
            fInputGainDb = 0.0f; fOutputGainDb = 0.0f; fOutputMode = kOutputRaw;
            break;
        case kProgramNormalized:
            fInputGainDb = 0.0f; fOutputGainDb = 0.0f; fOutputMode = kOutputNormalized;
            break;
        case kProgramCleanBoost:
            fInputGainDb = 4.0f; fOutputGainDb = -2.0f; fOutputMode = kOutputRaw;
            break;
        }
    }

    // --- State (model + IR paths) -------------------------------------------
    void initState(uint32_t index, State& state) override
    {
        if (index == kStateModelPath)
        {
            state.key = "model";
            state.label = "NAM model";
            state.hints = kStateIsFilenamePath;
        }
        else if (index == kStateIRPath)
        {
            state.key = "ir";
            state.label = "Cab IR";
            state.hints = kStateIsFilenamePath;
        }
    }

    String getState(const char* key) const override
    {
        if (std::strcmp(key, "model") == 0)
            return String(fModelPath.c_str());
        if (std::strcmp(key, "ir") == 0)
            return String(fIRPath.c_str());
        return String();
    }

    void setState(const char* key, const char* value) override
    {
        const std::string path = value != nullptr ? value : "";
        if (std::strcmp(key, "model") == 0)
        {
            fModelPath = path;
            requestModel();
        }
        else if (std::strcmp(key, "ir") == 0)
        {
            fIRPath = path;
            requestIR();
        }
    }

    // --- Lifecycle ----------------------------------------------------------
    void activate() override
    {
        mSampleRate = getSampleRate();
        mMaxBlock = static_cast<int>(getBufferSize());
        allocScratch(mMaxBlock);

        mGateTrigger.SetSampleRate(mSampleRate);

        mDcBlocker.SetParams(recursive_linear_filter::HighPassParams(mSampleRate, kDcBlockerFreqHz));
        primeFilter(mDcBlocker, mMaxBlock);

        mToneStack.Reset(mSampleRate, mMaxBlock);
        applyToneStack(true);
        primeToneStack(mMaxBlock);

        if (!fModelPath.empty())
            requestModel();
        if (!fIRPath.empty())
            requestIR();
    }

    void sampleRateChanged(double newRate) override
    {
        mSampleRate = newRate;
        mGateTrigger.SetSampleRate(mSampleRate);
        mDcBlocker.SetParams(recursive_linear_filter::HighPassParams(mSampleRate, kDcBlockerFreqHz));
        primeFilter(mDcBlocker, mMaxBlock);
        mToneStack.Reset(mSampleRate, mMaxBlock);
        applyToneStack(true);
        primeToneStack(mMaxBlock);
        if (!fModelPath.empty())
            requestModel();
        if (!fIRPath.empty())
            requestIR();
    }

    // --- Audio (real-time) --------------------------------------------------
    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        adoptPending();
        applyToneStack(false); // refresh biquads only if Bass/Mid/Treble changed

        // The model is one optional stage in the chain: skipped when no model is loaded
        // or when the user bypasses it. The rest of the chain (gate, EQ, IR, output) still
        // runs so an IR-only / model-bypassed setup works and lines up for A/B.
        const bool modelActive = mModel != nullptr && !fModelBypass;

        // Report the model's latency only while it is actually in the path, so a bypassed
        // (or absent) model reports zero and the host's delay compensation stays aligned.
        const uint32_t desiredLatency = modelActive ? mModelLatency : 0;
        if (desiredLatency != mLatencyReported)
        {
            mLatencyReported = desiredLatency;
            setLatency(desiredLatency);
        }

        // Input gain, plus input calibration into the model's expected level.
        double inputDb = fInputGainDb;
        if (modelActive && mModelHasInputLevel)
            inputDb += fInputCalibrationDb - mModelInputLevelDb;
        const double inGain = dbToLin(inputDb);

        // Output gain, plus the selected normalization (only meaningful with an active model).
        double normDb = 0.0;
        if (modelActive)
        {
            if (fOutputMode == kOutputNormalized && mModelHasLoudness)
                normDb = kNormalizationTargetDb - mModelLoudnessDb;
            else if (fOutputMode == kOutputCalibrated && mModelHasOutputLevel)
                normDb = mModelOutputLevelDb - fInputCalibrationDb;
        }
        const double outGain = dbToLin(fOutputGainDb + normDb);

        const float* in = inputs[0];
        float* out = outputs[0];

        if (frames <= static_cast<uint32_t>(mScratchCap))
        {
            for (uint32_t i = 0; i < frames; ++i)
                mInD[i] = static_cast<double>(in[i]) * inGain;

            // Noise gate: trigger reads the (gained) input; gain is applied post-model.
            const bool gateActive = fGateThresholdDb > kGateOffDb;
            if (gateActive)
            {
                mGateTrigger.SetParams(dsp::noise_gate::TriggerParams(
                    0.01, fGateThresholdDb, 0.1, 0.005, 0.01, 0.05));
                double* trigIn[1] = {mInD.data()};
                mGateTrigger.Process(trigIn, 1, frames);
            }

            // Model stage (or dry passthrough into the chain when inactive).
            double* sig;
            if (modelActive)
            {
                mModel->process(mInD.data(), mModelOut.data(), static_cast<int>(frames));
                sig = mModelOut.data();
            }
            else
            {
                sig = mInD.data();
            }

            if (gateActive)
            {
                double* gIn[1] = {sig};
                sig = mGateGain.Process(gIn, 1, frames)[0];
            }
            if (!fToneStackBypass)
            {
                double* tIn[1] = {sig};
                sig = mToneStack.Process(tIn, 1, frames)[0];
            }
            if (mIR != nullptr && !fIRBypass)
            {
                double* irIn[1] = {sig};
                sig = mIR->Process(irIn, 1, frames)[0];
            }
            double* dcIn[1] = {sig};
            sig = mDcBlocker.Process(dcIn, 1, frames)[0];

            for (uint32_t i = 0; i < frames; ++i)
                out[i] = static_cast<float>(sig[i] * outGain);
        }
        else
        {
            const double passGain = inGain * dbToLin(fOutputGainDb);
            for (uint32_t i = 0; i < frames; ++i)
                out[i] = static_cast<float>(static_cast<double>(in[i]) * passGain);
        }

        updateMeters(in, out, frames);
    }

private:
    static double dbToLin(double db) { return std::pow(10.0, db * 0.05); }

    void updateMeters(const float* in, const float* out, uint32_t frames)
    {
        float pin = 0.0f, pout = 0.0f;
        for (uint32_t i = 0; i < frames; ++i)
        {
            pin = std::max(pin, std::fabs(in[i]));
            pout = std::max(pout, std::fabs(out[i]));
        }
        constexpr float release = 0.85f;
        fMeterIn = pin > fMeterIn ? pin : fMeterIn * release;
        fMeterOut = pout > fMeterOut ? pout : fMeterOut * release;
    }

    void allocScratch(int n)
    {
        if (n <= mScratchCap)
            return;
        mScratchCap = n;
        mInD.assign(static_cast<size_t>(n), 0.0);
        mModelOut.assign(static_cast<size_t>(n), 0.0);
    }

    static void primeFilter(recursive_linear_filter::Base& f, int n)
    {
        std::vector<double> zeros(static_cast<size_t>(n), 0.0);
        double* in[1] = {zeros.data()};
        f.Process(in, 1, static_cast<size_t>(n));
    }

    void primeToneStack(int n)
    {
        std::vector<double> zeros(static_cast<size_t>(n), 0.0);
        double* in[1] = {zeros.data()};
        mToneStack.Process(in, 1, n);
    }

    // Recompute tone-stack biquads only when a band changed (cheap, but avoid per-block trig).
    void applyToneStack(bool force)
    {
        if (force || fBass != mBassApplied) { mToneStack.SetParam("bass", fBass); mBassApplied = fBass; }
        if (force || fMid != mMidApplied) { mToneStack.SetParam("middle", fMid); mMidApplied = fMid; }
        if (force || fTreble != mTrebleApplied) { mToneStack.SetParam("treble", fTreble); mTrebleApplied = fTreble; }
    }

    void requestModel()
    {
        if (fModelPath.empty()) { mModelLoader.request(nullptr); return; }
        const std::string path = fModelPath;
        const double sr = mSampleRate;
        const int maxBlock = mMaxBlock;
        mModelLoader.request([this, path, sr, maxBlock]() -> std::unique_ptr<NamModel> {
            auto m = NamModel::load(path);
            if (m)
            {
                m->prepare(sr, maxBlock, /*doPrewarm=*/true);
                if (m->isSlimmable())
                    m->setSlimmableSize(mQuality.load(std::memory_order_relaxed));
            }
            return m;
        });
    }

    void requestIR()
    {
        if (fIRPath.empty()) { mIRLoader.request(nullptr); return; }
        const std::string path = fIRPath;
        const double sr = mSampleRate;
        const int maxBlock = mMaxBlock;
        mIRLoader.request([path, sr, maxBlock]() -> std::unique_ptr<dsp::ImpulseResponse> {
            auto ir = std::make_unique<dsp::ImpulseResponse>(path.c_str(), sr);
            if (ir->GetWavState() != dsp::wav::LoadReturnCode::SUCCESS)
                return nullptr;
            std::vector<double> zeros(static_cast<size_t>(maxBlock), 0.0);
            double* in[1] = {zeros.data()};
            ir->Process(in, 1, static_cast<size_t>(maxBlock));
            return ir;
        });
    }

    void adoptPending()
    {
        if (NamModel* incoming = mModelLoader.popReady())
        {
            NamModel* old = mModel.release();
            mModel.reset(incoming);
            // Publish the new active model before retiring the old one, so the loader
            // thread's slim poll never dereferences a model that is being freed.
            mActiveModelPtr.store(incoming, std::memory_order_release);
            mModelLoader.retire(old);
            mModelHasLoudness = mModel->hasLoudness();
            mModelLoudnessDb = mModel->loudness();
            mModelHasInputLevel = mModel->hasInputLevel();
            mModelInputLevelDb = mModel->inputLevel();
            mModelHasOutputLevel = mModel->hasOutputLevel();
            mModelOutputLevelDb = mModel->outputLevel();
            // Cache latency; run() reports it to the host, and zeroes it when bypassed.
            mModelLatency = static_cast<uint32_t>(mModel->latencySamples());
        }
        if (mModelLoader.popClear())
        {
            NamModel* old = mModel.release();
            mActiveModelPtr.store(nullptr, std::memory_order_release);
            mModelLoader.retire(old);
            mModelHasLoudness = mModelHasInputLevel = mModelHasOutputLevel = false;
            mModelLoudnessDb = mModelInputLevelDb = mModelOutputLevelDb = 0.0;
            mModelLatency = 0;
        }
        if (dsp::ImpulseResponse* incoming = mIRLoader.popReady())
        {
            mIRLoader.retire(mIR.release());
            mIR.reset(incoming);
        }
        if (mIRLoader.popClear())
            mIRLoader.retire(mIR.release());
    }

    // Runs on the model loader thread (registered via setPoll). Applies the latest Quality
    // value to the active model when it changed. SetSlimmableSize is not RT-safe, so it must
    // not run on the audio thread; this thread also owns model freeing, so touching the active
    // model here is lifetime-safe.
    void applySlim()
    {
        const uint32_t seq = mQualitySeq.load(std::memory_order_acquire);
        if (seq == mSlimAppliedSeq)
            return;
        mSlimAppliedSeq = seq;
        if (NamModel* m = mActiveModelPtr.load(std::memory_order_acquire))
            if (m->isSlimmable())
                m->setSlimmableSize(mQuality.load(std::memory_order_relaxed));
    }

    // Parameters
    float fInputGainDb = 0.0f;
    float fGateThresholdDb = -100.0f;
    float fBass = 5.0f, fMid = 5.0f, fTreble = 5.0f;
    bool fToneStackBypass = false;
    bool fIRBypass = false;
    bool fModelBypass = false;
    float fOutputGainDb = 0.0f;
    int fOutputMode = kOutputRaw;
    float fInputCalibrationDb = 12.0f;
    float fMeterIn = 0.0f, fMeterOut = 0.0f;

    // tone-stack change tracking
    float mBassApplied = -1.0f, mMidApplied = -1.0f, mTrebleApplied = -1.0f;

    // State
    std::string fModelPath, fIRPath;

    // Audio config
    double mSampleRate = 48000.0;
    int mMaxBlock = 1024;

    // Scratch (double)
    int mScratchCap = 0;
    std::vector<double> mInD, mModelOut;

    // DSP (audio-thread-owned)
    std::unique_ptr<NamModel> mModel;
    std::unique_ptr<dsp::ImpulseResponse> mIR;
    dsp::noise_gate::Trigger mGateTrigger;
    dsp::noise_gate::Gain mGateGain;
    NamToneStack mToneStack;
    recursive_linear_filter::HighPass mDcBlocker;

    // model calibration cache
    bool mModelHasLoudness = false, mModelHasInputLevel = false, mModelHasOutputLevel = false;
    double mModelLoudnessDb = 0.0, mModelInputLevelDb = 0.0, mModelOutputLevelDb = 0.0;
    uint32_t mModelLatency = 0;    // active model's latency; 0 when none/bypassed
    uint32_t mLatencyReported = 0; // last value pushed to setLatency()

    // Quality (slim). mQuality/mQualitySeq written from the host thread, read by the loader
    // thread; mActiveModelPtr published by the audio thread; mSlimAppliedSeq is loader-only.
    std::atomic<float> mQuality{1.0f};
    std::atomic<uint32_t> mQualitySeq{0};
    std::atomic<NamModel*> mActiveModelPtr{nullptr};
    uint32_t mSlimAppliedSeq = 0;

    // Loaders
    AsyncLoader<NamModel> mModelLoader;
    AsyncLoader<dsp::ImpulseResponse> mIRLoader;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NamDpfPlugin)
};

Plugin* createPlugin()
{
    return new NamDpfPlugin();
}

END_NAMESPACE_DISTRHO
