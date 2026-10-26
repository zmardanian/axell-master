#pragma once
#include <axell/gui/LookAndFeel.h>
#include "PluginProcessor.h"

namespace mui
{
constexpr int kWidth = 1200;
constexpr int kHeight = 760;
}

// scrolling history (~7 s): input / output peak (dBFS) and short-term loudness against the target (bottom-up), limiter
// and clipper gain reduction (top-down)
class HistoryView : public juce::Component, public juce::SettableTooltipClient
{
public:
    HistoryView();
    void push (const axm::DisplayFrame& f);
    void setTarget (float lufs) { target = lufs; }
    void paint (juce::Graphics&) override;

private:
    std::vector<axm::DisplayFrame> ring;
    int pos = 0;
    float target = 0.0f;
};

// numbers: integrated (large), short-term, momentary, LRA, true peak, PLR, distance to the target
class LoudnessPanel : public juce::Component
{
public:
    explicit LoudnessPanel (MasterProcessor& p);
    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    MasterProcessor& proc;
    ui::Choice target;
    juce::TextButton learn { "LEARN" }, resetBtn { "RESET" };
    float I = -INFINITY, S = -INFINITY, M = -INFINITY, lra = 0, tp = -120, iIn = -INFINITY;
    juce::String learnText;
};

// 2.0: the master through the streaming codecs (Opus = YouTube, Vorbis = Spotify): decoded true peak and clipped
// samples since the last change, and SAFE (lowers the ceiling so the decoded peaks stay below full scale)
class CodecView : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit CodecView (MasterProcessor& p);
    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    MasterProcessor& proc;
    ui::Toggle onOff { "STREAMING CODECS", ui::col::global, "" };
    juce::TextButton safe { "SAFE" };
    juce::Rectangle<int> rows;
    float worst[2] { -200.0f, -200.0f };
    int clips[2] { 0, 0 };
    double secs = 0.0;
    bool on = true, incomplete = false;
};

// output peak L/R (with the ceiling) and limiter / clipper gain reduction, peak-hold lines
class Meters : public juce::Component, public juce::SettableTooltipClient
{
public:
    void set (const float outDb[2], float limDb, float clipDb, float ceilingDb);
    void paint (juce::Graphics&) override;

private:
    struct Hold { float v = -120, peak = -120; int hold = 0; void set (float x, float floor); };
    Hold out[2], lim, clip;
    float ceiling = -1.0f;
};

// the clipper's transfer curve (input vs output, linear around the ceiling) with the live input level on it
class ClipCurve : public juce::Component, public juce::SettableTooltipClient
{
public:
    void set (const axm::Params& p, float inPeakDb);
    void paint (juce::Graphics&) override;

private:
    axm::Params params;
    float in = -120.0f, inHold = -120.0f;
    int hold = 0;
};

class MasterEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit MasterEditor (MasterProcessor&);
    ~MasterEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void refreshNow() { timerCallback(); }

private:
    class Content : public juce::Component
    {
    public:
        explicit Content (MasterEditor& e) : owner (e) {}
        void paint (juce::Graphics&) override;
        void resized() override;
        MasterEditor& owner;
    };
    void timerCallback() override;
    void layout();
    void showPresetMenu();
    void promptSavePreset();

    MasterProcessor& proc;
    ui::LookAndFeel lnf;
    Content content { *this };
    juce::TooltipWindow tooltips { this, 700 };

    juce::TextButton presetPrev { "<" }, presetNext { ">" }, presetSave { "SAVE" };
    ui::MenuButton presetName;
    juce::TextButton slotA { "A" }, slotB { "B" }, slotCopy { "COPY" };
    ui::Toggle compare { "COMPARE", ui::col::warn, "pill" };
    ui::Toggle voiceHold { "VOICE HOLD", ui::col::teal, "pill" };
    ui::Toggle essHold { "ESS HOLD", ui::col::teal, "pill" };

    HistoryView history;
    LoudnessPanel loudness { proc };
    ui::Knob gain { "GAIN", ui::col::accent, 124 }, ceiling { "CEILING", ui::col::accent, 74 }, release { "RELEASE", ui::col::accent, 74 },
        look { "LOOKAHEAD", ui::col::accent, 74 }, link { "LINK", ui::col::accent, 74 };
    ui::Segmented style { { "TRANSPARENT", "PUNCHY", "LOUD", "SAFE" }, ui::col::accent };
    ui::Knob clip { "CLIP", ui::col::teal, 72 }, shape { "SHAPE", ui::col::teal, 60 };
    ui::Segmented os { { "1x", "2x", "4x", "8x", "16x" }, ui::col::global };
    ui::Choice dither;
    Meters meters;
    ClipCurve curve;
    CodecView codecView { proc };

    juce::Rectangle<int> headerArea, clipArea, limArea, outArea, styleLabel, osLabel, ditherLabel, compareLabel;
    std::unique_ptr<juce::AlertWindow> saveDialog;
    bool constructed = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MasterEditor)
};
