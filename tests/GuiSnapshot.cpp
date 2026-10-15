// Headless GUI snapshot tool: builds the real processor + editor, feeds a loud beat-like program so the meters and the
// history move, and saves PNGs.   MasterGuiSnapshot [outputDir]
#include "../Source/PluginEditor.h"
#include "../Source/PluginProcessor.h"

#include <functional>
#include <random>

namespace
{
void setParam (MasterProcessor& p, const char* id, float value)
{
    if (auto* prm = p.apvts.getParameter (id))
        prm->setValueNotifyingHost (prm->convertTo0to1 (value));
}

void shoot (const juce::File& dir, const juce::String& name, float uiScale, float renderScale,
            const std::function<void (MasterProcessor&)>& setup)
{
    MasterProcessor proc;
    proc.setPlayConfigDetails (2, 2, 48000.0, 512);
    proc.prepareToPlay (48000.0, 512);
    setup (proc);
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    auto* editor = dynamic_cast<MasterEditor*> (ed.get());
    ed->setSize (juce::roundToInt (mui::kWidth * uiScale), juce::roundToInt (mui::kHeight * uiScale));

    // ~9 s of program (verse level, then a louder chorus), GUI refreshed like the 30 Hz timer
    const double fs = 48000.0, pi = juce::MathConstants<double>::pi;
    const int n = (int) (9.0 * fs);
    std::mt19937 g (7);
    std::normal_distribution<double> nd;
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    for (int pos = 0, blk = 0; pos + 512 <= n; pos += 512, ++blk)
    {
        for (int i = 0; i < 512; ++i)
        {
            const double t = (pos + i) / fs, beat = std::fmod (t, 0.5), level = t < 4.5 ? 0.25 : 0.42;
            const double kick = std::exp (-beat * 12) * std::sin (2 * pi * (48 + 90 * std::exp (-beat * 35)) * beat);
            const double sn = std::exp (-std::fmod (t + 0.25, 0.5) * 22) * nd (g) * 0.5;
            const double hat = std::exp (-std::fmod (t, 0.125) * 70) * nd (g) * 0.15;
            const double bass = 0.35 * std::sin (2 * pi * 55 * t), pad = 0.15 * std::sin (2 * pi * 220 * t);
            buf.setSample (0, i, (float) (level * (kick + sn + hat + bass + pad)));
            buf.setSample (1, i, (float) (level * (kick + 0.9 * sn + 1.1 * hat + bass + 0.9 * pad)));
        }
        proc.processBlock (buf, midi);
        if (blk % 3 == 0)
            editor->refreshNow();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (1);
    }
    editor->refreshNow();
    const auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, renderScale);
    const auto file = dir.getChildFile (name + ".png");
    file.deleteFile();
    juce::FileOutputStream os (file);
    juce::PNGImageFormat png;
    if (os.openedOk() && png.writeImageToStream (img, os))
        std::printf ("wrote %s (%dx%d)\n", file.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
    else
        std::printf ("FAILED to write %s\n", file.getFullPathName().toRawUTF8());
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File dir (argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[1])
                                   : juce::File::getCurrentWorkingDirectory().getChildFile ("docs/screenshots"));
    dir.createDirectory();
    auto loud = [] (MasterProcessor& p)
    {
        p.setCurrentProgram (4); // Loud Rap / Trap
        setParam (p, "input", 9.0f);
    };
    shoot (dir, "master-100pct", 1.0f, 1.0f, loud);
    shoot (dir, "master-100pct-hidpi2x", 1.0f, 2.0f, loud);
    shoot (dir, "master-75pct", 0.75f, 1.0f, loud);
    shoot (dir, "master-streaming", 1.0f, 1.0f, [] (MasterProcessor& p)
    {
        p.setCurrentProgram (1);
        setParam (p, "input", 6.0f);
        setParam (p, "os", 4.0f);
        setParam (p, "dither", 2.0f);
        p.selectSlot (1);
    });
    return 0;
}
