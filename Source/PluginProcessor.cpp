#include "PluginProcessor.h"
#include "PluginEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;

namespace
{
juce::AudioParameterFloatAttributes fmt (std::function<juce::String (float)> toText,
                                         std::function<float (const juce::String&)> fromText = nullptr)
{
    auto a = juce::AudioParameterFloatAttributes().withStringFromValueFunction ([toText] (float v, int) { return toText (v); });
    if (fromText == nullptr)
        fromText = [] (const juce::String& t) { return t.getFloatValue(); };
    return a.withValueFromStringFunction (fromText);
}
juce::String dbText (float v) { return (v > 0.0f ? "+" : "") + juce::String (v, 1) + " dB"; }
juce::String msText (float v) { return v < 10.0f ? juce::String (v, 2) + " ms" : juce::String (juce::roundToInt (v)) + " ms"; }
juce::String pctText (float v) { return juce::String (juce::roundToInt (v)) + " %"; }
const juce::Identifier kPresetNameId ("presetName");
} // namespace

APVTS::ParameterLayout MasterProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    // IDs, ranges and order are frozen from 1.0 on (saved projects, automation); new parameters are appended
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "input", 1 }, "Input",
        NormalisableRange<float> (-24.0f, 36.0f, 0.1f), 0.0f, fmt (dbText)));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "clip", 1 }, "Clip",
        NormalisableRange<float> (0.0f, 100.0f, 1.0f), 0.0f, fmt ([] (float v) { return v < 0.5f ? String ("Off") : pctText (v); })));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "clipShape", 1 }, "Clip Shape",
        NormalisableRange<float> (0.0f, 100.0f, 1.0f), 30.0f,
        fmt ([] (float v) { return v < 15.0f ? String ("Soft") : v > 85.0f ? String ("Hard") : pctText (v); })));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "ceiling", 1 }, "Ceiling",
        NormalisableRange<float> (-24.0f, 0.0f, 0.1f), -1.0f, fmt ([] (float v) { return String (v, 1) + " dBTP"; })));
    NormalisableRange<float> rel (1.0f, 1000.0f, 0.1f);
    rel.setSkewForCentre (120.0f);
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "release", 1 }, "Release", rel, 120.0f, fmt (msText)));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "style", 1 }, "Style",
        StringArray { "Transparent", "Punchy", "Loud", "Safe" }, 0));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "lookahead", 1 }, "Lookahead",
        NormalisableRange<float> (0.5f, 5.0f, 0.01f), 2.0f, fmt (msText)));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "link", 1 }, "Stereo Link",
        NormalisableRange<float> (0.0f, 100.0f, 1.0f), 100.0f, fmt (pctText)));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "os", 1 }, "Oversampling", StringArray { "1x", "2x", "4x", "8x", "16x" }, 4));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "dither", 1 }, "Dither",
        StringArray { "Off", "24-bit", "16-bit", "16-bit shaped" }, 0));
    StringArray targets;
    for (const auto& t : axm::kTargets)
        targets.add (t.name);
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "target", 1 }, "Loudness Target", targets, 1));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { "compare", 1 }, "Level-Matched Compare", false));
    // 2.0 (appended)
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { "codecCheck", 2 }, "Codec Check", true));
    // 3.0 (appended): VOICE HOLD - on a vocal (bus) the limiter holds its gain through the voice's pauses instead of
    // lifting the room and the breaths (the suite's voice decision; Axell Duo Compressor 3.0's method). Off by default
    // (a mix master); the vocal presets turn it on
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { "voiceHold", 3 }, "Voice Hold", false));
    return { p.begin(), p.end() };
}

MasterProcessor::MasterProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    pInput = apvts.getRawParameterValue ("input");
    pClip = apvts.getRawParameterValue ("clip");
    pShape = apvts.getRawParameterValue ("clipShape");
    pCeil = apvts.getRawParameterValue ("ceiling");
    pRel = apvts.getRawParameterValue ("release");
    pStyle = apvts.getRawParameterValue ("style");
    pLook = apvts.getRawParameterValue ("lookahead");
    pLink = apvts.getRawParameterValue ("link");
    pOs = apvts.getRawParameterValue ("os");
    pDither = apvts.getRawParameterValue ("dither");
    pTarget = apvts.getRawParameterValue ("target");
    pCompare = apvts.getRawParameterValue ("compare");
    pCodec = apvts.getRawParameterValue ("codecCheck");
    pVoiceHold = apvts.getRawParameterValue ("voiceHold");
    startTimerHz (20);
}

MasterProcessor::~MasterProcessor() { stopTimer(); }

bool MasterProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo()) && out == layouts.getMainInputChannelSet();
}

axm::Params MasterProcessor::readParams() const
{
    axm::Params p;
    p.inputDb = pInput->load();
    p.clipPct = pClip->load();
    p.clipShapePct = pShape->load();
    p.ceilingDb = pCeil->load();
    p.releaseMs = pRel->load();
    p.style = juce::roundToInt (pStyle->load());
    p.lookMs = pLook->load();
    p.linkPct = pLink->load();
    p.osLog2 = juce::roundToInt (pOs->load());
    p.dither = juce::roundToInt (pDither->load());
    p.compare = pCompare->load() > 0.5f;
    p.voiceHold = pVoiceHold->load() > 0.5f;
    return p;
}

int MasterProcessor::targetIndex() const { return juce::jlimit (0, axm::kNumTargets - 1, juce::roundToInt (pTarget->load())); }

void MasterProcessor::syncEngineParams (bool bypass)
{
    auto p = readParams();
    p.bypass = bypass;
    dsp.setParams (p);
}

void MasterProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    preparedChannels = juce::jlimit (1, axell::kMaxCh, getMainBusNumOutputChannels());
    syncEngineParams (false);
    dsp.prepare (sampleRate, preparedChannels, samplesPerBlock);
    dsp.handshake().setNonRealtime (isNonRealtime());
    setLatencySamples (dsp.getLatencySamples());
    dsp.handshake().setHostLatency (getLatencySamples());
    codec.setEnabled (pCodec->load() > 0.5f);
    codec.prepare (sampleRate);
    lastParams = readParams();
}

void MasterProcessor::reset()
{
    if (preparedChannels > 0)
        dsp.reset();
}

void MasterProcessor::setNonRealtime (bool isOffline) noexcept
{
    AudioProcessor::setNonRealtime (isOffline);
    dsp.handshake().setNonRealtime (isOffline);
}

void MasterProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c)
        buffer.clear (c, 0, buffer.getNumSamples());
    if (preparedChannels == 0 || buffer.getNumSamples() == 0)
        return;
    syncEngineParams (false);
    const int nch = juce::jmin (buffer.getNumChannels(), preparedChannels);
    dsp.process (buffer.getArrayOfWritePointers(), nch, buffer.getNumSamples());
    // the master as it will be exported goes to the codec check (not the level-matched unprocessed COMPARE signal)
    if (pCompare->load() < 0.5f)
        codec.push (buffer.getArrayOfReadPointers(), nch, buffer.getNumSamples());
}

// host bypass: the engine keeps running (latency-aligned dry, 10 ms crossfade instead of a click)
void MasterProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c)
        buffer.clear (c, 0, buffer.getNumSamples());
    if (preparedChannels == 0 || buffer.getNumSamples() == 0)
        return;
    syncEngineParams (true);
    dsp.process (buffer.getArrayOfWritePointers(), juce::jmin (buffer.getNumChannels(), preparedChannels), buffer.getNumSamples());
}

void MasterProcessor::timerCallback()
{
    // codec check: on / off, and a new measurement whenever the processing changes (it measures these settings)
    codec.setEnabled (pCodec->load() > 0.5f);
    const auto now = readParams();
    const auto& a = lastParams;
    const auto moved = [] (float x, float y) { return std::abs (x - y) > 1.0e-4f; };
    if (moved (now.inputDb, a.inputDb) || moved (now.clipPct, a.clipPct) || moved (now.clipShapePct, a.clipShapePct)
        || moved (now.ceilingDb, a.ceilingDb) || moved (now.releaseMs, a.releaseMs) || now.style != a.style
        || moved (now.lookMs, a.lookMs) || moved (now.linkPct, a.linkPct) || now.osLog2 != a.osLog2 || now.dither != a.dither)
        codec.requestReset();
    lastParams = now;
    if (programNamePending.exchange (false))
        setPresetName (getProgramName (currentProgram.load()));
    // latency handshake: a new latency plays only after it was reported to the host (axell::LatencyHandshake)
    auto& hs = dsp.handshake();
    hs.heartbeat();
    int lat = 0;
    if (preparedChannels > 0 && hs.take (lat))
    {
        if (lat != getLatencySamples())
            setLatencySamples (lat);
        hs.ack();
    }
}

//== presets, A/B, LEARN =======================================================================================
void MasterProcessor::applyParams (const axm::Params& p, int target)
{
    auto set = [this] (const char* id, float v)
    {
        if (auto* prm = apvts.getParameter (id))
            prm->setValueNotifyingHost (prm->convertTo0to1 (v));
    };
    set ("input", p.inputDb);
    set ("clip", p.clipPct);
    set ("clipShape", p.clipShapePct);
    set ("ceiling", p.ceilingDb);
    set ("release", p.releaseMs);
    set ("style", (float) p.style);
    set ("lookahead", p.lookMs);
    set ("link", p.linkPct);
    set ("os", (float) p.osLog2);
    set ("dither", (float) p.dither);
    set ("target", (float) target);
    set ("voiceHold", p.voiceHold ? 1.0f : 0.0f);
}

int MasterProcessor::getNumPrograms() { return (int) axm::factoryPresets().size(); }

void MasterProcessor::setCurrentProgram (int index)
{
    const auto list = axm::factoryPresets();
    if (index < 0 || index >= (int) list.size())
        return;
    currentProgram.store (index);
    auto p = list[(size_t) index].params;
    p.inputDb = pInput->load(); // presets never change the input gain (it belongs to the mix)
    applyParams (p, list[(size_t) index].target);
    if (juce::MessageManager::existsAndIsCurrentThread())
        setPresetName (list[(size_t) index].name);
    else
        programNamePending.store (true);
}

const juce::String MasterProcessor::getProgramName (int index)
{
    const auto list = axm::factoryPresets();
    return index >= 0 && index < (int) list.size() ? juce::String (list[(size_t) index].name) : juce::String();
}

juce::String MasterProcessor::getPresetName() const
{
    const juce::ScopedLock sl (nameLock);
    return presetName;
}

void MasterProcessor::setPresetName (const juce::String& n)
{
    const juce::ScopedLock sl (nameLock);
    presetName = n;
}

juce::File MasterProcessor::userPresetFolder()
{
    auto base = juce::SystemStats::getEnvironmentVariable ("XDG_DATA_HOME", {});
    juce::File root = base.isNotEmpty() ? juce::File (base)
                                        : juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile (".local/share");
    return root.getChildFile ("AxellMaster").getChildFile ("Presets");
}

bool MasterProcessor::saveUserPreset (const juce::String& name)
{
    const auto clean = juce::File::createLegalFileName (name.trim());
    auto folder = userPresetFolder();
    if (clean.isEmpty() || ! folder.createDirectory())
        return false;
    auto state = apvts.copyState();
    state.setProperty (kPresetNameId, name.trim(), nullptr);
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    if (xml == nullptr || ! xml->writeTo (folder.getChildFile (clean + ".axmpreset")))
        return false;
    setPresetName (name.trim());
    return true;
}

bool MasterProcessor::loadUserPreset (const juce::File& file)
{
    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return false;
    auto tree = juce::ValueTree::fromXml (*xml);
    restoreTree (tree);
    setPresetName (tree.getProperty (kPresetNameId, file.getFileNameWithoutExtension()).toString());
    return true;
}

void MasterProcessor::selectSlot (int slot)
{
    slot = juce::jlimit (0, 1, slot);
    if (slot == activeSlot)
        return;
    slots[activeSlot] = apvts.copyState();
    if (slots[slot].isValid())
        restoreTree (slots[slot].createCopy());
    else
        slots[slot] = apvts.copyState();
    activeSlot = slot;
}

void MasterProcessor::copyActiveToOther() { slots[1 - activeSlot] = apvts.copyState(); }

void MasterProcessor::startLearn()
{
    resetMeasurements();
    learning = true;
}

void MasterProcessor::resetMeasurements()
{
    dsp.requestMeterReset();
    codec.requestReset();
}

float MasterProcessor::codecSafeChangeDb() const
{
    if (! codec.isEnabled() || codec.measuredSeconds() < 3.0)
        return 0.0f;
    // the ceiling moves in its 0.1 dB steps, down to the next one that reaches the target
    const float c = pCeil->load(), change = codec.safeCeilingChangeDb();
    if (change >= 0.0f)
        return 0.0f;
    const float next = std::max (-24.0f, std::floor ((c + change) * 10.0f - 1.0e-3f) / 10.0f);
    return next - c;
}

void MasterProcessor::applyCodecSafe()
{
    const float d = codecSafeChangeDb();
    if (d >= 0.0f)
        return;
    if (auto* prm = apvts.getParameter ("ceiling"))
        prm->setValueNotifyingHost (prm->convertTo0to1 (juce::jlimit (-24.0f, 0.0f, pCeil->load() + d)));
    resetMeasurements();
}

float MasterProcessor::learnSuggestionDb() const
{
    const auto& t = axm::kTargets[(size_t) targetIndex()];
    const float i = dsp.outputLoudness().integratedLufs();
    if (! learning || t.lufs > -1.0f || ! std::isfinite (i) || i < -70.0f || dsp.measuredSeconds() < 3.0)
        return std::numeric_limits<float>::quiet_NaN();
    return t.lufs - i;
}

void MasterProcessor::applyLearn()
{
    const float s = learnSuggestionDb();
    learning = false;
    if (! std::isfinite (s))
        return;
    if (auto* prm = apvts.getParameter ("input"))
        prm->setValueNotifyingHost (prm->convertTo0to1 (juce::jlimit (-24.0f, 36.0f, pInput->load() + s)));
    resetMeasurements();
}

// APVTS::replaceState skips a parameter whose snapped value already matches; re-apply explicitly (pluginval scenario)
void MasterProcessor::restoreTree (const juce::ValueTree& tree)
{
    apvts.replaceState (tree);
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            const auto child = apvts.state.getChildWithProperty ("id", rp->paramID);
            if (child.isValid() && child.hasProperty ("value"))
            {
                const float norm = rp->convertTo0to1 ((float) child.getProperty ("value"));
                if (std::abs (rp->getValue() - norm) > 1.0e-6f)
                    rp->setValueNotifyingHost (norm);
            }
        }
}

void MasterProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty (kPresetNameId, getPresetName(), nullptr);
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void MasterProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml != nullptr && xml->hasTagName (apvts.state.getType()))
    {
        auto tree = juce::ValueTree::fromXml (*xml);
        setPresetName (tree.getProperty (kPresetNameId, getPresetName()).toString());
        restoreTree (tree);
    }
}

juce::AudioProcessorEditor* MasterProcessor::createEditor() { return new MasterEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MasterProcessor(); }
