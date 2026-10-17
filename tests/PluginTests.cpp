// Plugin-level tests (JUCE): parameters, state save/restore, latency reporting, presets, A/B, LEARN, editor.
// Exit code = number of failures. Registered with CTest as "plugin_suite".
#include "../Source/PluginEditor.h"
#include "../Source/PluginProcessor.h"

#include <cstdio>
#include <random>
#include <thread>

namespace
{
int failures = 0, checks = 0;
using Buf = std::vector<std::vector<float>>;

void check (bool ok, const juce::String& what)
{
    ++checks;
    if (! ok)
        ++failures;
    std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what.toRawUTF8());
}

void setParam (MasterProcessor& p, const char* id, float value)
{
    auto* prm = p.apvts.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (value));
}
float getParam (MasterProcessor& p, const char* id) { return p.apvts.getRawParameterValue (id)->load(); }

void prepare (MasterProcessor& p, double fs = 48000.0, int block = 256)
{
    p.setPlayConfigDetails (2, 2, fs, block);
    p.prepareToPlay (fs, block);
}

// beat-like stereo program (kick, snare, hats, bass, pad)
Buf program (int n, double fs, double gain, unsigned seed)
{
    Buf b (2, std::vector<float> ((size_t) n));
    std::mt19937 g (seed);
    std::normal_distribution<double> nd;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / fs, beat = std::fmod (t, 0.5), pi = juce::MathConstants<double>::pi;
        const double kick = std::exp (-beat * 12) * std::sin (2 * pi * (48 + 90 * std::exp (-beat * 35)) * beat);
        const double sn = std::exp (-std::fmod (t + 0.25, 0.5) * 22) * nd (g) * 0.45;
        const double bass = 0.35 * std::sin (2 * pi * 55 * t);
        const double pad = 0.12 * (std::sin (2 * pi * 220 * t) + std::sin (2 * pi * 329.6 * t + 2));
        b[0][(size_t) i] = (float) (gain * (kick + sn + bass + pad));
        b[1][(size_t) i] = (float) (gain * (kick + 0.85 * sn + bass + 0.9 * pad));
    }
    return b;
}

Buf render (MasterProcessor& p, const Buf& in, int block = 256)
{
    Buf out (2, std::vector<float> (in[0].size()));
    juce::AudioBuffer<float> buf (2, block);
    juce::MidiBuffer midi;
    for (size_t pos = 0; pos + (size_t) block <= in[0].size(); pos += (size_t) block)
    {
        for (int c = 0; c < 2; ++c)
            buf.copyFrom (c, 0, in[(size_t) c].data() + pos, block);
        p.processBlock (buf, midi);
        for (int c = 0; c < 2; ++c)
            std::copy (buf.getReadPointer (c), buf.getReadPointer (c) + block, out[(size_t) c].begin() + (long) pos);
    }
    return out;
}

void pumpMessages (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI init;

    // user presets go to a temporary XDG data dir, never the real one
    auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("axell-master-plugin-tests");
    tmp.deleteRecursively();
    tmp.createDirectory();
    setenv ("XDG_DATA_HOME", tmp.getFullPathName().toRawUTF8(), 1);

    //== 1. frozen parameter IDs and order (automation uses the index), version hints
    {
        MasterProcessor p;
        // 1.0's twelve keep their index and version hint 1; 2.0 appends codecCheck (version hint 2)
        const juce::StringArray frozen { "input", "clip", "clipShape", "ceiling", "release", "style", "lookahead", "link",
                                         "os", "dither", "target", "compare", "codecCheck" };
        auto& params = p.getParameters();
        check (params.size() == frozen.size(), "parameter count = " + juce::String (frozen.size()));
        for (int i = 0; i < params.size() && i < frozen.size(); ++i)
        {
            auto* wp = dynamic_cast<juce::AudioProcessorParameterWithID*> (params[i]);
            const int hint = i < 12 ? 1 : 2;
            check (wp != nullptr && wp->getParameterID() == frozen[i] && wp->getVersionHint() == hint,
                   "parameter index " + juce::String (i) + " is '" + frozen[i] + "' (version hint " + juce::String (hint) + ")");
        }
        check (getParam (p, "codecCheck") == 1.0f, "default: codec check on");
        check (getParam (p, "ceiling") == -1.0f && getParam (p, "os") == 4.0f && getParam (p, "target") == 1.0f
                   && getParam (p, "clip") == 0.0f && getParam (p, "input") == 0.0f,
               "defaults: ceiling -1 dBTP, 16x, target -14 LUFS, clipper off, input 0 dB");
    }

    //== 2. state save/restore: non-default values -> identical parameters + identical audio
    {
        MasterProcessor a;
        const std::pair<const char*, float> values[] = { { "input", 7.3f }, { "clip", 42.0f }, { "clipShape", 66.0f },
                                                         { "ceiling", -0.7f }, { "release", 77.0f }, { "style", 1.0f },
                                                         { "lookahead", 3.25f }, { "link", 55.0f }, { "os", 3.0f },
                                                         { "dither", 3.0f }, { "target", 3.0f }, { "compare", 0.0f } };
        for (auto& v : values)
            setParam (a, v.first, v.second);
        a.setPresetName ("My Album Master");
        juce::MemoryBlock state;
        a.getStateInformation (state);

        MasterProcessor b;
        b.setStateInformation (state.getData(), (int) state.getSize());
        bool same = true;
        for (auto& v : values)
            if (std::abs (getParam (a, v.first) - getParam (b, v.first)) > 1.0e-6f)
            {
                same = false;
                std::printf ("   mismatch %s: %f vs %f\n", v.first, getParam (a, v.first), getParam (b, v.first));
            }
        check (same, "state restore: all parameter values identical");
        check (b.getPresetName() == "My Album Master", "state restore: preset name restored");

        prepare (a);
        prepare (b);
        const auto sig = program (48000 * 2, 48000.0, 0.4, 5);
        const auto oa = render (a, sig);
        const auto ob = render (b, sig);
        double diff = 0.0;
        for (int c = 0; c < 2; ++c)
            for (size_t i = 0; i < oa[0].size(); ++i)
                diff = std::max (diff, (double) std::abs (oa[(size_t) c][i] - ob[(size_t) c][i]));
        check (diff <= 0.0, "state restore: restored instance renders bit-identical audio (max diff " + juce::String (diff) + ")");
        const int expectLat = b.engine().latencyOf ({ 3, (int) std::lround (3.25e-3 * 48000.0) });
        check (a.getLatencySamples() == expectLat && b.getLatencySamples() == expectLat,
               "state restore: latency = 8x + 64 + 3.25 ms lookahead (" + juce::String (b.getLatencySamples()) + " samples)");
    }

    //== 2b. pluginval-style: raw (unsnapped) normalised values on every parameter, save, scramble, restore
    {
        juce::Random rng (1234);
        bool ok = true;
        for (int round = 0; round < 50 && ok; ++round)
        {
            MasterProcessor p;
            auto& params = p.getParameters();
            for (auto* prm : params)
                prm->setValueNotifyingHost (rng.nextFloat());
            std::vector<float> saved;
            for (auto* prm : params)
                saved.push_back (prm->getValue());
            juce::MemoryBlock mb;
            p.getStateInformation (mb);
            for (auto* prm : params)
                prm->setValueNotifyingHost (rng.nextFloat());
            p.setStateInformation (mb.getData(), (int) mb.getSize());
            for (int i = 0; i < params.size(); ++i)
            {
                auto* rp = dynamic_cast<juce::RangedAudioParameter*> (params[i]);
                const float expected = rp->convertTo0to1 (rp->convertFrom0to1 (saved[(size_t) i]));
                if (std::abs (params[i]->getValue() - expected) > 0.01f)
                {
                    ok = false;
                    std::printf ("   round %d %s: %f expected %f\n", round, rp->paramID.toRawUTF8(), params[i]->getValue(), expected);
                }
            }
        }
        check (ok, "raw-value state restore (pluginval scenario), 50 random rounds");
    }

    //== 3. latency reporting follows oversampling and lookahead (handshake + message-thread timer), ceiling held meanwhile
    {
        MasterProcessor p;
        prepare (p);
        check (p.getLatencySamples() == p.engine().latencyOf ({ 4, 96 }), "prepare: latency = 16x + 64 + 2 ms (" + juce::String (p.getLatencySamples()) + ")");
        const auto sig = program (9600, 48000.0, 1.2, 1);
        bool ceilingOk = true;
        for (auto [os, look] : { std::pair<int, float> { 2, 2.0f }, { 0, 5.0f }, { 1, 0.5f }, { 3, 1.0f }, { 4, 2.0f } })
        {
            setParam (p, "os", (float) os);
            setParam (p, "lookahead", look);
            for (int k = 0; k < 4; ++k)
            {
                const auto out = render (p, sig);   // the audio thread asks for the new latency and holds
                for (auto& ch : out)
                    for (float v : ch)
                        ceilingOk = ceilingOk && std::abs (v) <= 0.8913f;
                pumpMessages (60);                  // the 20 Hz timer reports it, the next blocks play it
            }
            const int expect = p.engine().latencyOf ({ os, (int) std::lround (look * 0.001 * 48000.0) });
            check (p.getLatencySamples() == expect && p.engine().getLatencySamples() == expect,
                   "after switching to " + juce::String (1 << os) + "x / " + juce::String (look, 1) + " ms: reported latency "
                       + juce::String (p.getLatencySamples()) + " = " + juce::String (expect));
        }
        check (ceilingOk, "every sample during the switches stays below the -1 dB ceiling");
    }

    //== 4. factory programs
    {
        MasterProcessor p;
        const auto list = axm::factoryPresets();
        check (p.getNumPrograms() == (int) list.size(), "programs: " + juce::String (p.getNumPrograms()) + " factory presets");
        // 1.0's eight keep their program numbers (projects and host automation refer to them); 2.0 adds 20
        const char* v1[] { "Init", "Streaming Master (-14 LUFS)", "Apple Music (-16 LUFS)", "Boom Bap Punch", "Loud Rap / Trap",
                           "Club / CD (-9 LUFS)", "Safe / Acoustic", "Broadcast (EBU R128)" };
        bool sameV1 = list.size() == 28;
        for (int i = 0; i < 8; ++i)
            sameV1 = sameV1 && juce::String (list[(size_t) i].name) == v1[i];
        check (sameV1, "presets: 28; 1.0's eight first, in their order");
        bool cats = true, named = true;
        juce::StringArray names;
        for (const auto& fp : list)
        {
            bool known = false;
            for (const char* c : axm::kCategories)
                known = known || juce::String (fp.category) == c;
            cats = cats && known;
            named = named && juce::String (fp.description).length() > 20 && ! names.contains (fp.name);
            names.add (fp.name);
        }
        check (cats && named, "presets: every one has a menu group, a description, and a unique name");
        setParam (p, "input", 5.0f);
        bool allOk = true;
        for (int i = 0; i < p.getNumPrograms(); ++i)
        {
            p.setCurrentProgram (i);
            const auto r = p.readParams();
            const auto& e = list[(size_t) i].params;
            if (std::abs (r.clipPct - e.clipPct) > 0.51f || std::abs (r.ceilingDb - e.ceilingDb) > 0.051f
                || std::abs (r.releaseMs - e.releaseMs) > 0.051f * std::max (1.0f, e.releaseMs) || r.style != e.style
                || r.osLog2 != e.osLog2 || std::abs (r.lookMs - e.lookMs) > 0.011f || p.targetIndex() != list[(size_t) i].target
                || std::abs (r.inputDb - 5.0f) > 0.051f)
            {
                allOk = false;
                std::printf ("   program %d (%s) did not apply\n", i, list[(size_t) i].name);
            }
        }
        check (allOk, "every factory program applies its values and keeps the input gain");
        setParam (p, "input", 0.0f);
        p.setCurrentProgram (0);
        bool defaults = true;
        for (auto* prm : p.getParameters())
            if (std::abs (prm->getValue() - prm->getDefaultValue()) > 1.0e-4f)
            {
                defaults = false;
                std::printf ("   Init: %s not default\n", prm->getName (40).toRawUTF8());
            }
        check (defaults, "'Init' program = all parameter defaults");
        std::thread t ([&p] { p.setCurrentProgram (1); });
        t.join();
        check (p.getCurrentProgram() == 1 && std::abs (p.readParams().clipPct - list[1].params.clipPct) < 0.51f,
               "program change from another thread applies the values");
        pumpMessages (150);
        check (p.getPresetName() == list[1].name, "program change from another thread: name updated by the timer");
    }

    //== 5. user presets save/load
    {
        MasterProcessor p;
        setParam (p, "clip", 33.0f);
        setParam (p, "ceiling", -0.4f);
        check (p.saveUserPreset ("Test / Master: 1"), "user preset saved (name with illegal file characters)");
        auto files = MasterProcessor::userPresetFolder().findChildFiles (juce::File::findFiles, false, "*.axmpreset");
        check (files.size() == 1 && MasterProcessor::userPresetFolder().isAChildOf (tmp),
               "user preset written to $XDG_DATA_HOME/AxellMaster/Presets");
        MasterProcessor q;
        check (files.size() == 1 && q.loadUserPreset (files[0]), "user preset loads");
        check (std::abs (getParam (q, "clip") - 33.0f) < 1e-4f && std::abs (getParam (q, "ceiling") + 0.4f) < 1e-4f
                   && q.getPresetName() == "Test / Master: 1",
               "user preset restores values and name");
    }

    //== 6. A/B compare
    {
        MasterProcessor p;
        setParam (p, "clip", 10.0f);
        p.selectSlot (1);
        setParam (p, "clip", 60.0f);
        p.selectSlot (0);
        const float a = getParam (p, "clip");
        p.selectSlot (1);
        const float b = getParam (p, "clip");
        check (std::abs (a - 10.0f) < 1e-4f && std::abs (b - 60.0f) < 1e-4f, "A/B: each slot keeps its own settings");
        p.copyActiveToOther();
        p.selectSlot (0);
        check (std::abs (getParam (p, "clip") - 60.0f) < 1e-4f, "A/B: copy B > A");
    }

    //== 7. LEARN: a quiet mix is brought to the target
    {
        MasterProcessor p;
        prepare (p);
        setParam (p, "target", 1.0f); // -14 LUFS
        const auto sig = program (48000 * 6, 48000.0, 0.12, 9);
        p.startLearn();
        render (p, Buf (2, std::vector<float> (2048, 0.0f))); // the meter reset happens on the audio thread
        check (! std::isfinite (p.learnSuggestionDb()), "LEARN: no suggestion before 3 s of audio");
        render (p, sig);
        const float s = p.learnSuggestionDb();
        const float before = p.engine().outputLoudness().integratedLufs();
        check (std::isfinite (s) && std::abs (before + s + 14.0f) < 0.01f,
               "LEARN: suggestion = target - integrated (" + juce::String (before, 2) + " LUFS, +" + juce::String (s, 2) + " dB)");
        p.applyLearn();
        check (std::abs (getParam (p, "input") - s) < 0.051f && ! p.isLearning(), "LEARN: applied to the input gain");
        for (int pass = 0; pass < 2; ++pass)
        {
            p.startLearn();
            render (p, Buf (2, std::vector<float> (2048, 0.0f)));
            render (p, sig);
            const float s2 = p.learnSuggestionDb();
            const float i2 = p.engine().outputLoudness().integratedLufs();
            if (pass == 1)
                check (std::abs (i2 + 14.0f) < 0.5f, "LEARN twice: integrated " + juce::String (i2, 2) + " LUFS, within 0.5 LU of -14");
            p.applyLearn();
            juce::ignoreUnused (s2);
        }
        check (p.engine().outputTruePeakDb() <= -1.0f, "LEARN: true peak still <= -1 dBTP (" + juce::String (p.engine().outputTruePeakDb(), 2) + ")");
    }

    //== 8. compare (level matched) and host bypass keep the latency alignment
    {
        MasterProcessor p;
        prepare (p);
        setParam (p, "input", 0.0f);
        const auto sig = program (48000, 48000.0, 0.05, 4); // far below the ceiling: wet = delayed dry
        juce::AudioBuffer<float> buf (2, 256);
        juce::MidiBuffer midi;
        Buf out (2, std::vector<float> (sig[0].size()));
        for (size_t pos = 0; pos + 256 <= sig[0].size(); pos += 256)
        {
            for (int c = 0; c < 2; ++c)
                buf.copyFrom (c, 0, sig[(size_t) c].data() + pos, 256);
            p.processBlockBypassed (buf, midi);
            for (int c = 0; c < 2; ++c)
                std::copy (buf.getReadPointer (c), buf.getReadPointer (c) + 256, out[(size_t) c].begin() + (long) pos);
        }
        const int L = p.getLatencySamples();
        double err = 0;
        for (size_t i = (size_t) L + 24000; i < sig[0].size() - 512; ++i)
            err = std::max (err, (double) std::abs (out[0][i] - sig[0][i - (size_t) L]));
        check (err < 1.0e-6, "host bypass: dry output delayed by the reported latency (error " + juce::String (err) + ")");
    }

    //== 9. editor: open/close repeatedly while processing, size limits and aspect ratio
    {
        MasterProcessor p;
        prepare (p);
        const auto sig = program (2048, 48000.0, 0.5, 2);
        bool ok = true;
        for (int i = 0; i < 5; ++i)
        {
            std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
            render (p, sig);
            pumpMessages (40);
            ok = ok && ed->getWidth() == mui::kWidth;
        }
        check (ok, "editor: opens/closes 5x while processing (100 % size)");
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        auto* c = ed->getConstrainer();
        check (c != nullptr && c->getMinimumWidth() == mui::kWidth * 3 / 4 && c->getMaximumWidth() == 2 * mui::kWidth
                   && std::abs (c->getFixedAspectRatio() - (double) mui::kWidth / mui::kHeight) < 1e-9,
               "editor: resizable 75-200 %, aspect locked");
        ed->setSize (mui::kWidth * 3 / 2, mui::kHeight * 3 / 2);
        ed.reset();
        std::unique_ptr<juce::AudioProcessorEditor> ed2 (p.createEditor());
        check (ed2->getWidth() == mui::kWidth * 3 / 2, "editor: re-opens at the last used size (150 %)");
    }

    //== 2.0: streaming codec check
    {
        // a 1.0 project (no codecCheck in its state) opens with the check on and everything else as saved
        MasterProcessor a;
        setParam (a, "ceiling", -1.5f);
        juce::MemoryBlock st;
        a.getStateInformation (st);
        auto xml = juce::AudioProcessor::getXmlFromBinary (st.getData(), (int) st.getSize());
        for (auto* c : xml->getChildIterator())
            if (c->getStringAttribute ("id") == "codecCheck")
            {
                xml->removeChildElement (c, true);
                break;
            }
        juce::MemoryBlock old;
        juce::AudioProcessor::copyXmlToBinary (*xml, old);
        MasterProcessor b;
        b.setStateInformation (old.getData(), (int) old.getSize());
        check (getParam (b, "codecCheck") == 1.0f && std::abs (getParam (b, "ceiling") + 1.5f) < 1e-4f,
               "a 1.0 project opens with the codec check on and its own settings");
    }
    {
        MasterProcessor p;
        prepare (p, 48000.0, 512);
        check (p.codecSafeChangeDb() == 0.0f, "codec check: SAFE has nothing to do before 3 s were measured");
        // a loud master: pushed hard into a -0.1 dBTP ceiling, the codecs decode above full scale
        p.setCurrentProgram (4);
        setParam (p, "input", 16.0f);
        setParam (p, "ceiling", -0.1f);
        pumpMessages (120);   // the timer sees the change and restarts the measurement
        const auto x = program (48000 * 8, 48000.0, 0.5, 11);
        render (p, x, 512);
        for (int k = 0; k < 600 && p.codecCheck().measuredSeconds() < 7.5; ++k)
            pumpMessages (20);
        const float w = std::max (p.codecCheck().worstDb (0), p.codecCheck().worstDb (1));
        check (p.codecCheck().measuredSeconds() > 7.5, "codec check: the worker measured the whole render ("
               + juce::String (p.codecCheck().measuredSeconds(), 1) + " s)");
        const float d = p.codecSafeChangeDb();
        check (w > axm::CodecCheck::kTargetDb && d < 0.0f && d <= axm::CodecCheck::kTargetDb - w + 1e-4f && d > axm::CodecCheck::kTargetDb - w - 0.1f,
               "codec check: decoded " + juce::String (w, 2) + " dBTP -> SAFE proposes " + juce::String (d, 1) + " dB (0.1 dB steps, reaches the target)");
        p.applyCodecSafe();
        check (std::abs (getParam (p, "ceiling") - (-0.1f + d)) < 0.051f, "SAFE: ceiling lowered to " + juce::String (getParam (p, "ceiling"), 1) + " dBTP");
        for (int k = 0; k < 100 && p.codecCheck().measuredSeconds() > 0.0; ++k)
            pumpMessages (10);
        check (p.codecCheck().measuredSeconds() == 0.0, "SAFE: the codec measurement restarts");
        // COMPARE plays the unprocessed mix: it is not what gets exported, so it never reaches the codecs
        setParam (p, "compare", 1.0f);
        pumpMessages (60);
        p.codecCheck().requestReset();
        pumpMessages (60);
        render (p, program (48000 * 2, 48000.0, 0.5, 12), 512);
        pumpMessages (300);
        check (p.codecCheck().measuredSeconds() == 0.0, "codec check: COMPARE audio is not measured");
        setParam (p, "compare", 0.0f);
        // switched off: nothing measured
        setParam (p, "codecCheck", 0.0f);
        pumpMessages (120);
        render (p, program (48000 * 2, 48000.0, 0.5, 13), 512);
        pumpMessages (300);
        check (! p.codecCheck().isEnabled() && p.codecCheck().measuredSeconds() == 0.0, "codec check off: nothing measured");
    }

    tmp.deleteRecursively();
    std::printf ("%s: %d of %d plugin checks passed\n", failures == 0 ? "ALL PASS" : "FAILURES", checks - failures, checks);
    return failures;
}
