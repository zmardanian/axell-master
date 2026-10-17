#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "CodecCheck.h"
#include "Engine.h"
#include "Presets.h"

// Thin JUCE wrapper around axm::Engine (all DSP lives in JUCE-free headers).
class MasterProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    MasterProcessor();
    ~MasterProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;
    void setNonRealtime (bool isNonRealtime) noexcept override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    using AudioProcessor::processBlockBypassed;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram.load(); }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    juce::AudioProcessorValueTreeState apvts;

    //== presets / A-B / LEARN (message thread) ================================================
    void applyParams (const axm::Params& p, int target);
    axm::Params readParams() const;
    int targetIndex() const;
    static juce::File userPresetFolder();
    bool saveUserPreset (const juce::String& name);
    bool loadUserPreset (const juce::File& file);
    juce::String getPresetName() const;
    void setPresetName (const juce::String& n);
    int getActiveSlot() const { return activeSlot; }
    void selectSlot (int slot);
    void copyActiveToOther();
    // LEARN: restart the output loudness measurement; after some playback, apply() moves INPUT by (target - integrated)
    void startLearn();
    bool isLearning() const { return learning; }
    float learnSuggestionDb() const; // NaN until enough audio was measured
    void applyLearn();

    axm::Engine& engine() { return dsp; }

    //== 2.0: streaming codec check (Opus / Vorbis encode-decode on a worker thread) ===================
    axm::CodecCheck& codecCheck() { return codec; }
    // the ceiling change SAFE would make (dB, <= 0; 0 = nothing to do) and applying it (message thread)
    float codecSafeChangeDb() const;
    void applyCodecSafe();
    // restarts the loudness, true-peak and codec measurements
    void resetMeasurements();

private:
    void timerCallback() override;
    void syncEngineParams (bool bypass);
    void restoreTree (const juce::ValueTree& tree);

    axm::Engine dsp;
    axm::CodecCheck codec;
    axm::Params lastParams;   // the codec measurement restarts when the processing changes
    int preparedChannels = 0;
    std::atomic<int> currentProgram { 0 };
    std::atomic<bool> programNamePending { false };
    juce::CriticalSection nameLock;
    juce::String presetName { "Init" };
    int activeSlot = 0;
    juce::ValueTree slots[2];
    bool learning = false;

    std::atomic<float>* pInput = nullptr;   std::atomic<float>* pClip = nullptr;   std::atomic<float>* pShape = nullptr;
    std::atomic<float>* pCeil = nullptr;    std::atomic<float>* pRel = nullptr;    std::atomic<float>* pStyle = nullptr;
    std::atomic<float>* pLook = nullptr;    std::atomic<float>* pLink = nullptr;   std::atomic<float>* pOs = nullptr;
    std::atomic<float>* pDither = nullptr;  std::atomic<float>* pTarget = nullptr; std::atomic<float>* pCompare = nullptr;
    std::atomic<float>* pCodec = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MasterProcessor)
};
