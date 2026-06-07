// The GUI, drawn with NanoVG. Everything is laid out in a fixed 480x470 design space
// (see kDesignW/kDesignH) and scaled to the window; mouse coordinates are scaled back.
// Widgets are plain rectangles and circles with hand-rolled hit-testing in onMouse.

#include "DistrhoUI.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

START_NAMESPACE_DISTRHO

namespace {
// Design coordinate system; the whole UI scales uniformly from this.
constexpr float kDesignW = 480.0f;
constexpr float kDesignH = 470.0f;

struct Rect
{
    float x, y, w, h;
    bool contains(double px, double py) const { return px >= x && px <= x + w && py >= y && py <= y + h; }
};

struct KnobDef
{
    float cx, cy, r;
    uint32_t param;
    float min, max, neutral; // neutral = where the value arc starts (0 dB, 5 for tone, -100 for gate)
    float def;               // default value (double-click reset)
    const char* label;
    const char* fmt;
};

// Buttons
const Rect kModelBtn  = {360.0f,  70.0f, 100.0f, 26.0f};
const Rect kIRBtn     = {300.0f, 110.0f,  76.0f, 26.0f};
const Rect kIRBypass  = {382.0f, 110.0f,  78.0f, 26.0f};
const Rect kModeBtn   = {20.0f,  410.0f, 180.0f, 32.0f};
const Rect kEqBypass  = {210.0f, 410.0f, 120.0f, 32.0f};

// Knobs
const KnobDef kKnobs[] = {
    //  cx     cy     r    param                    min      max    neutral    def   label     fmt
    { 70.0f, 215.0f, 38.0f, kParamInputGain,        -20.0f,  20.0f,    0.0f,   0.0f, "Input",  "%+.1f dB"},
    {165.0f, 215.0f, 38.0f, kParamOutputGain,       -40.0f,  40.0f,    0.0f,   0.0f, "Output", "%+.1f dB"},
    {260.0f, 215.0f, 38.0f, kParamGateThreshold,   -100.0f,   0.0f, -100.0f,-100.0f, "Gate",   "%.0f dB"},
    {355.0f, 215.0f, 38.0f, kParamInputCalibration, -60.0f,  60.0f,    0.0f,  12.0f, "Calib",  "%+.0f dBu"},
    { 70.0f, 335.0f, 38.0f, kParamBass,               0.0f,  10.0f,    5.0f,   5.0f, "Bass",   "%.1f"},
    {165.0f, 335.0f, 38.0f, kParamMid,                0.0f,  10.0f,    5.0f,   5.0f, "Mid",    "%.1f"},
    {260.0f, 335.0f, 38.0f, kParamTreble,             0.0f,  10.0f,    5.0f,   5.0f, "Treble", "%.1f"},
    {355.0f, 335.0f, 38.0f, kParamQuality,            0.0f,   1.0f,    0.0f,   1.0f, "Quality","%.2f"},
};
const int kNumKnobs = (int)(sizeof(kKnobs) / sizeof(kKnobs[0]));

const Rect kMeterIn  = {410.0f, 280.0f, 22.0f, 110.0f};
const Rect kMeterOut = {440.0f, 280.0f, 22.0f, 110.0f};

std::string baseName(const std::string& path)
{
    if (path.empty())
        return "(none)";
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

float meterFraction(float linear)
{
    if (linear <= 1.0e-6f)
        return 0.0f;
    const float db = 20.0f * std::log10(linear);
    const float f = (db + 48.0f) / 48.0f;
    return std::clamp(f, 0.0f, 1.0f);
}

const char* kModeNames[] = {"Raw", "Normalized", "Calibrated"};
} // namespace

class NamUI : public UI
{
public:
    NamUI()
        : UI(static_cast<uint>(kDesignW), static_cast<uint>(kDesignH))
    {
        loadSharedResources();
        // Resizable, keeping aspect ratio; we scale the canvas ourselves. Min = 70%.
        setGeometryConstraints(static_cast<uint>(kDesignW * 0.7f), static_cast<uint>(kDesignH * 0.7f),
                               /*keepAspectRatio=*/true, /*automaticallyScale=*/false);
    }

protected:
    void parameterChanged(uint32_t index, float value) override
    {
        switch (index)
        {
        case kParamInputGain: fInputGain = value; break;
        case kParamGateThreshold: fGate = value; break;
        case kParamBass: fBass = value; break;
        case kParamMid: fMid = value; break;
        case kParamTreble: fTreble = value; break;
        case kParamToneStackBypass: fEqBypass = value > 0.5f; break;
        case kParamIRBypass: fIRBypass = value > 0.5f; break;
        case kParamOutputGain: fOutputGain = value; break;
        case kParamOutputMode: fOutputMode = static_cast<int>(value + 0.5f); break;
        case kParamInputCalibration: fCalib = value; break;
        case kParamQuality: fQuality = value; break;
        case kParamMeterIn: fMeterIn = value; break;
        case kParamMeterOut: fMeterOut = value; break;
        default: return;
        }
        repaint();
    }

    void stateChanged(const char* key, const char* value) override
    {
        if (std::strcmp(key, "model") == 0)
            fModelName = baseName(value ? value : "");
        else if (std::strcmp(key, "ir") == 0)
            fIRName = baseName(value ? value : "");
        repaint();
    }

    void onNanoDisplay() override
    {
        // Scale the whole design (kDesignW x kDesignH) to the current window size.
        const float s = uiScale();
        resetTransform();
        scale(s, s);

        beginPath();
        rect(0, 0, kDesignW, kDesignH);
        fillColor(Color(28, 30, 34));
        fill();

        fontFaceId(0);
        fontFace(NANOVG_DEJAVU_SANS_TTF);

        fillColor(Color(220, 225, 230));
        fontSize(26.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(20, 16, "Nullamp", nullptr);
        fontSize(12.0f);
        fillColor(Color(120, 125, 135));
        text(20, 46, "Neural Amp Modeler", nullptr);

        drawLabel(20, 80, "MODEL");
        drawValue(90, 80, fModelName.c_str());
        drawButton(kModelBtn, "Load", false);

        drawLabel(20, 120, "IR");
        drawValue(60, 120, fIRName.c_str());
        drawButton(kIRBtn, "Load", false);
        drawButton(kIRBypass, fIRBypass ? "Bypassed" : "Active", fIRBypass);

        for (int i = 0; i < kNumKnobs; ++i)
            drawKnob(kKnobs[i], paramValue(kKnobs[i].param));

        char mode[48];
        std::snprintf(mode, sizeof(mode), "Output: %s", kModeNames[std::clamp(fOutputMode, 0, 2)]);
        drawButton(kModeBtn, mode, fOutputMode != 0);
        drawButton(kEqBypass, fEqBypass ? "EQ: Bypassed" : "EQ: On", fEqBypass);

        drawMeter(kMeterIn, fMeterIn, "In");
        drawMeter(kMeterOut, fMeterOut, "Out");
    }

    bool onMouse(const MouseEvent& ev) override
    {
        if (ev.button != 1)
            return false;

        if (ev.press)
        {
            const float s = uiScale();
            const double x = ev.pos.getX() / s;
            const double y = ev.pos.getY() / s;

            if (kModelBtn.contains(x, y)) { requestStateFile("model"); return true; }
            if (kIRBtn.contains(x, y))    { requestStateFile("ir");    return true; }
            if (kIRBypass.contains(x, y)) { toggleBool(kParamIRBypass, fIRBypass); return true; }
            if (kEqBypass.contains(x, y)) { toggleBool(kParamToneStackBypass, fEqBypass); return true; }
            if (kModeBtn.contains(x, y))  { cycleMode(); return true; }

            for (int i = 0; i < kNumKnobs; ++i)
            {
                const KnobDef& k = kKnobs[i];
                const double dx = x - k.cx, dy = y - k.cy;
                if (dx * dx + dy * dy > k.r * k.r)
                    continue;

                // Double-click resets the knob to its default.
                const bool dbl = (ev.time - fLastClickTime) <= kDoubleClickMs && fLastClickParam == k.param;
                fLastClickTime = ev.time;
                fLastClickParam = k.param;
                if (dbl)
                {
                    commitParam(k.param, k.def);
                    fLastClickTime = 0; // don't chain into a triple-click
                    return true;
                }

                // Otherwise begin a vertical drag.
                fDragParam = k.param;
                fDragMin = k.min;
                fDragMax = k.max;
                fDragStartY = y;
                fDragStartValue = paramValue(k.param);
                editParameter(k.param, true);
                return true;
            }
        }
        else if (fDragParam != kParamCount)
        {
            editParameter(fDragParam, false);
            fDragParam = kParamCount;
            return true;
        }
        return false;
    }

    bool onScroll(const ScrollEvent& ev) override
    {
        const float s = uiScale();
        const double x = ev.pos.getX() / s, y = ev.pos.getY() / s;
        for (int i = 0; i < kNumKnobs; ++i)
        {
            const KnobDef& k = kKnobs[i];
            const double dx = x - k.cx, dy = y - k.cy;
            if (dx * dx + dy * dy > k.r * k.r)
                continue;
            const float step = (k.max - k.min) * 0.02f; // ~2% of range per notch
            float v = paramValue(k.param) + static_cast<float>(ev.delta.getY()) * step;
            v = std::clamp(v, k.min, k.max);
            commitParam(k.param, v);
            return true;
        }
        return false;
    }

    bool onMotion(const MotionEvent& ev) override
    {
        if (fDragParam == kParamCount)
            return false;

        const double dy = fDragStartY - ev.pos.getY() / uiScale();
        const float range = fDragMax - fDragMin;
        float v = fDragStartValue + static_cast<float>(dy) * range / 200.0f;
        v = std::clamp(v, fDragMin, fDragMax);
        paramValue(fDragParam) = v;
        setParameterValue(fDragParam, v);
        repaint();
        return true;
    }

private:
    float uiScale() const { return static_cast<float>(getWidth()) / kDesignW; }

    float& paramValue(uint32_t param)
    {
        switch (param)
        {
        case kParamInputGain: return fInputGain;
        case kParamOutputGain: return fOutputGain;
        case kParamGateThreshold: return fGate;
        case kParamInputCalibration: return fCalib;
        case kParamBass: return fBass;
        case kParamMid: return fMid;
        case kParamTreble: return fTreble;
        case kParamQuality: return fQuality;
        }
        return fDummy;
    }

    // One-shot parameter change (begin, set, end), for reset and scroll.
    void commitParam(uint32_t param, float v)
    {
        paramValue(param) = v;
        editParameter(param, true);
        setParameterValue(param, v);
        editParameter(param, false);
        repaint();
    }

    void toggleBool(uint32_t param, bool& field)
    {
        field = !field;
        editParameter(param, true);
        setParameterValue(param, field ? 1.0f : 0.0f);
        editParameter(param, false);
        repaint();
    }

    void cycleMode()
    {
        fOutputMode = (fOutputMode + 1) % 3;
        editParameter(kParamOutputMode, true);
        setParameterValue(kParamOutputMode, static_cast<float>(fOutputMode));
        editParameter(kParamOutputMode, false);
        repaint();
    }

    void drawLabel(float x, float y, const char* s)
    {
        fillColor(Color(120, 125, 135));
        fontSize(13.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        text(x, y, s, nullptr);
    }

    void drawValue(float x, float y, const char* s)
    {
        fillColor(Color(200, 205, 212));
        fontSize(13.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        text(x, y, s, nullptr);
    }

    void drawButton(const Rect& r, const char* label, bool active)
    {
        beginPath();
        roundedRect(r.x, r.y, r.w, r.h, 5.0f);
        fillColor(active ? Color(70, 120, 200) : Color(52, 56, 62));
        fill();
        fillColor(active ? Color(240, 245, 250) : Color(190, 195, 202));
        fontSize(13.0f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        text(r.x + r.w * 0.5f, r.y + r.h * 0.5f, label, nullptr);
    }

    void drawKnob(const KnobDef& k, float value)
    {
        const float aStart = 2.4f, aSweep = 4.45f;
        const float frac = (value - k.min) / (k.max - k.min);
        const float centerFrac = (k.neutral - k.min) / (k.max - k.min);
        const float aVal = aStart + frac * aSweep;
        const float aCenter = aStart + centerFrac * aSweep;

        beginPath();
        arc(k.cx, k.cy, k.r, aStart, aStart + aSweep, NanoVG::CW);
        strokeColor(Color(52, 56, 62));
        strokeWidth(5.5f);
        stroke();
        beginPath();
        arc(k.cx, k.cy, k.r, std::min(aCenter, aVal), std::max(aCenter, aVal), NanoVG::CW);
        strokeColor(Color(70, 150, 230));
        strokeWidth(5.5f);
        stroke();
        beginPath();
        moveTo(k.cx + std::cos(aVal) * (k.r - 12.0f), k.cy + std::sin(aVal) * (k.r - 12.0f));
        lineTo(k.cx + std::cos(aVal) * (k.r - 2.0f), k.cy + std::sin(aVal) * (k.r - 2.0f));
        strokeColor(Color(210, 220, 235));
        strokeWidth(2.5f);
        stroke();
        beginPath();
        circle(k.cx, k.cy, k.r - 11.0f);
        fillColor(Color(40, 43, 48));
        fill();

        char buf[32];
        std::snprintf(buf, sizeof(buf), k.fmt, value);
        fillColor(Color(220, 225, 230));
        fontSize(13.0f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        text(k.cx, k.cy, buf, nullptr);
        fillColor(Color(120, 125, 135));
        fontSize(12.0f);
        text(k.cx, k.cy + k.r + 13.0f, k.label, nullptr);
    }

    void drawMeter(const Rect& r, float linear, const char* name)
    {
        beginPath();
        roundedRect(r.x, r.y, r.w, r.h, 3.0f);
        fillColor(Color(18, 20, 23));
        fill();
        const float frac = meterFraction(linear);
        const float bh = r.h * frac;
        if (bh > 0.5f)
        {
            beginPath();
            roundedRect(r.x, r.y + (r.h - bh), r.w, bh, 3.0f);
            fillColor(frac > 0.92f ? Color(220, 70, 60) : Color(80, 200, 110));
            fill();
        }
        fillColor(Color(120, 125, 135));
        fontSize(11.0f);
        textAlign(ALIGN_CENTER | ALIGN_TOP);
        text(r.x + r.w * 0.5f, r.y + r.h + 4.0f, name, nullptr);
    }

    // param mirror
    float fInputGain = 0.0f, fOutputGain = 0.0f, fGate = -100.0f, fCalib = 12.0f, fQuality = 1.0f;
    float fBass = 5.0f, fMid = 5.0f, fTreble = 5.0f;
    bool fEqBypass = false, fIRBypass = false;
    int fOutputMode = 0;
    float fMeterIn = 0.0f, fMeterOut = 0.0f;
    float fDummy = 0.0f;
    std::string fModelName = "(none)", fIRName = "(none)";

    uint32_t fDragParam = kParamCount;
    float fDragMin = 0.0f, fDragMax = 0.0f, fDragStartValue = 0.0f;
    double fDragStartY = 0.0;

    static constexpr uint kDoubleClickMs = 350;
    uint fLastClickTime = 0;
    uint32_t fLastClickParam = kParamCount;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NamUI)
};

UI* createUI()
{
    return new NamUI();
}

END_NAMESPACE_DISTRHO
