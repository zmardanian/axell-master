#include "PluginEditor.h"

#include <cmath>

using namespace ui;

namespace
{
const juce::Identifier kUiWidth ("uiWidth");
namespace tip
{
const char* gain = "How hard you drive the mix into the clipper and limiter: more GAIN = louder master. Watch the integrated "
                   "loudness against your target, or let LEARN set it.";
const char* ceiling = "The highest level the output may reach, in dB true peak (including the peaks between samples that appear "
                      "after conversion and streaming encodes). -1 dBTP is the streaming standard.";
const char* release = "How fast the limiter lets go. Short = louder and denser, more pumping and bass distortion; long = cleaner. "
                      "The STYLE adapts it to the music: single peaks recover fast, dense passages slowly.";
const char* style = "TRANSPARENT: clean, natural (default). PUNCHY: drums keep their snap. LOUD: maximum density for loud "
                    "releases. SAFE: lowest distortion for acoustic music, vocals, 808-heavy mixes.";
const char* look = "How far ahead the limiter looks (0.5-5 ms). Longer = smoother limiting of transients, shorter = more snap. "
                   "Adds latency (compensated by the DAW).";
const char* link = "Stereo link. 100 % = both sides limited together (a stable image). Lower = each side on its own for "
                   "more loudness on wide mixes; the ceiling holds on both sides either way.";
const char* clip = "Oversampled soft clipper before the limiter: it shaves the fastest peaks (snares, kicks) so the limiter works "
                   "less and pumps less. More CLIP = more of the peak reduction done by clipping. Off at 0 %.";
const char* shape = "Clipper knee: SOFT rounds the peaks (warmer, gentler), HARD cuts them flat (cleaner on short peaks, harsher "
                    "on long ones).";
const char* os = "Oversampling for the clipper. 16x (default): even hard clipping of bright mixes stays clean (aliasing below -99 dB). Lower settings save a little CPU; 1x skips oversampling (the ceiling still holds).";
const char* dither = "Only for the final export at that bit depth: 24-bit or 16-bit TPDF dither, or 16-bit shaped: the noise moved "
                     "out of the 1-6 kHz range where hearing is most sensitive (about 20 dB quieter there). Off while mixing.";
const char* compare = "COMPARE plays the unprocessed mix at the same loudness as the master (measured like a loudness meter). "
                      "Louder always sounds better - this shows what the processing really does.";
const char* history = "The last seconds: peaks going in after GAIN (grey) and coming out (white), short-term loudness "
                      "(yellow) against the target (dashed), and the gain reduction from the top: clipper (teal) and limiter (orange).";
const char* target = "Your delivery target. The integrated loudness turns green within 1 LU of it. LEARN measures your master "
                     "while it plays and moves GAIN to hit the target.";
const char* learn = "LEARN: play the loudest typical part of the song (at least 3 s), then press APPLY: GAIN moves by the "
                    "difference between the target and the measured integrated loudness. Repeat once to fine-tune.";
const char* reset = "Restart the loudness, true-peak and codec measurements.";
const char* codec = "Streaming services re-encode your master, and a lossy codec's decoded peaks land higher than the master's: "
                    "a loud master at -1 dBTP can still clip on YouTube or Spotify. This runs the master through the real "
                    "Opus (YouTube) and Vorbis (Spotify) encoders and decoders while it plays, and shows the decoded true peak "
                    "and the clipped samples since the last change. Green: safe. Amber: within 0.3 dB of full scale. Red: "
                    "clips. Play the whole song (or its loudest part), then press SAFE.";
const char* safeBtn = "SAFE lowers the ceiling just enough that both codecs decode at -0.3 dBTP or below, in the ceiling's "
                      "0.1 dB steps, and starts a new measurement. Play the song again: a lower ceiling also means denser "
                      "limiting, so a codec can still be above - then SAFE offers another, smaller step (usually 1-3 in all). "
                      "It costs a little loudness (typically 0.3-1 LU on a -9 LUFS master).";
const char* gr = "OUT: output peak per side against the ceiling (yellow line). GR: limiter (orange) and clipper (teal) "
                 "gain reduction in dB. The lines hold the recent peaks.";
const char* curve = "The clipper's curve: input level (after GAIN) left to right, output bottom to top, around the ceiling. "
                    "The dot is your loudest recent peak.";
} // namespace tip

juce::String lufsText (float v) { return std::isfinite (v) && v > -70.0f ? juce::String (v, 1) : juce::String ("--"); }
} // namespace

//==============================================================================
HistoryView::HistoryView() : ring (900) {}

void HistoryView::push (const axm::DisplayFrame& f)
{
    ring[(size_t) pos] = f;
    pos = (pos + 1) % (int) ring.size();
}

void HistoryView::paint (juce::Graphics& g)
{
    using namespace juce;
    const auto r = getLocalBounds().toFloat();
    g.setColour (col::well);
    g.fillRoundedRectangle (r, 10.0f);
    const auto plot = r.reduced (12.0f, 10.0f).withTrimmedLeft (22.0f);
    const float lo = -42.0f, hi = 0.0f;
    auto yL = [&] (float d) { return plot.getBottom() - (jlimit (lo, hi, d) - lo) / (hi - lo) * plot.getHeight(); };
    // gain reduction hangs from the top: 0 .. 12 dB over the top 40 %
    auto yG = [&] (float d) { return plot.getY() + jlimit (0.0f, 12.0f, d) / 12.0f * plot.getHeight() * 0.4f; };
    g.setFont (ui::font (10.0f));
    for (float d = -6.0f; d > lo; d -= 6.0f)
    {
        g.setColour (col::grid);
        g.drawHorizontalLine ((int) yL (d), plot.getX(), plot.getRight());
        g.setColour (col::faint);
        g.drawText (String ((int) d), Rectangle<float> (r.getX() + 6.0f, yL (d) - 6.0f, 26.0f, 12.0f), Justification::right);
    }
    const int n = (int) plot.getWidth(), N = (int) ring.size();
    auto at = [&] (int i) -> const axm::DisplayFrame& { return ring[(size_t) ((pos - n + i + N * 8) % N)]; };
    auto x = [&] (int i) { return plot.getX() + (float) i; };
    auto fillFromBottom = [&] (auto v, Colour fill, Colour edge)
    {
        Path p, e;
        p.startNewSubPath (x (0), plot.getBottom());
        for (int i = 0; i < n; ++i)
        {
            const float y = yL (v (at (i)));
            p.lineTo (x (i), y);
            if (i == 0) e.startNewSubPath (x (i), y); else e.lineTo (x (i), y);
        }
        p.lineTo (x (n - 1), plot.getBottom());
        p.closeSubPath();
        g.setColour (fill);
        g.fillPath (p);
        g.setColour (edge);
        g.strokePath (e, PathStrokeType (1.0f));
    };
    // what goes in (after GAIN) behind what comes out: the difference is the peak reduction
    fillFromBottom ([] (const axm::DisplayFrame& f) { return f.inPeakDb; }, col::dim.withAlpha (0.10f), col::dim.withAlpha (0.45f));
    fillFromBottom ([] (const axm::DisplayFrame& f) { return f.outPeakDb; }, col::text.withAlpha (0.13f), col::text.withAlpha (0.55f));
    auto area = [&] (auto v, Colour c)
    {
        float mx = 0;
        for (int i = 0; i < n; ++i)
            mx = std::max (mx, v (at (i)));
        if (mx < 0.05f)
            return;
        Path p, e;
        p.startNewSubPath (x (0), plot.getY());
        for (int i = 0; i < n; ++i)
        {
            const float y = yG (v (at (i)));
            p.lineTo (x (i), y);
            if (i == 0) e.startNewSubPath (x (i), y); else e.lineTo (x (i), y);
        }
        p.lineTo (x (n - 1), plot.getY());
        p.closeSubPath();
        g.setColour (c.withAlpha (0.55f));
        g.fillPath (p);
        g.setColour (c);
        g.strokePath (e, PathStrokeType (1.2f));
    };
    area ([] (const axm::DisplayFrame& f) { return f.limGrDb + f.clipGrDb; }, col::accent);
    area ([] (const axm::DisplayFrame& f) { return f.clipGrDb; }, col::teal);
    // short-term loudness (only where it exists: the first 3 s after a reset have none)
    {
        Path p;
        bool open = false;
        for (int i = 0; i < n; ++i)
        {
            const float v = at (i).outShortLufs;
            if (! std::isfinite (v) || v < lo)
            {
                open = false;
                continue;
            }
            if (! open) p.startNewSubPath (x (i), yL (v)); else p.lineTo (x (i), yL (v));
            open = true;
        }
        g.setColour (col::warn);
        g.strokePath (p, PathStrokeType (2.2f, PathStrokeType::curved, PathStrokeType::rounded));
    }
    if (target < 0.0f)
    {
        const float y = yL (target);
        Path d, dashed;
        d.startNewSubPath (plot.getX(), y);
        d.lineTo (plot.getRight(), y);
        float dash[] = { 5.0f, 4.0f };
        PathStrokeType (1.2f).createDashedStroke (dashed, d, dash, 2);
        g.setColour (col::warn.withAlpha (0.75f));
        g.fillPath (dashed);
        g.setFont (ui::font (10.0f, 1));
        g.drawText ("TARGET " + String (target, 0) + " LUFS", Rectangle<float> (plot.getRight() - 140.0f, y + 2.0f, 136.0f, 12.0f), Justification::right);
    }
    // legend
    g.setFont (ui::caps (9.5f, 1));
    auto lg = Rectangle<float> (plot.getX() + 6.0f, plot.getBottom() - 16.0f, 520.0f, 12.0f);
    auto item = [&] (Colour c, const String& t)
    {
        g.setColour (c);
        g.fillRoundedRectangle (lg.removeFromLeft (10.0f).reduced (0.0f, 2.0f), 2.0f);
        lg.removeFromLeft (5.0f);
        g.setColour (col::dim);
        const float w = (float) GlyphArrangement::getStringWidthInt (g.getCurrentFont(), t) + 14.0f;
        g.drawText (t, lg.removeFromLeft (w), Justification::centredLeft);
    };
    item (col::dim.withAlpha (0.6f), "IN");
    item (col::text.withAlpha (0.7f), "OUT");
    item (col::warn, "SHORT-TERM");
    item (col::accent, "LIMITER GR");
    item (col::teal, "CLIPPER GR");
}

//==============================================================================
LoudnessPanel::LoudnessPanel (MasterProcessor& p) : proc (p)
{
    target.bind (proc.apvts, "target", tip::target);
    learn.setTooltip (tip::learn);
    resetBtn.setTooltip (tip::reset);
    learn.onClick = [this]
    {
        if (! proc.isLearning())
            proc.startLearn();
        else
            proc.applyLearn();
        refresh();
    };
    resetBtn.onClick = [this] { proc.resetMeasurements(); };
    for (auto* c : std::initializer_list<juce::Component*> { &target, &learn, &resetBtn })
        addAndMakeVisible (c);
}

//==============================================================================
CodecView::CodecView (MasterProcessor& p) : proc (p)
{
    onOff.bind (proc.apvts, "codecCheck", tip::codec);
    setTooltip (tip::codec);
    safe.setTooltip (tip::safeBtn);
    safe.onClick = [this] { proc.applyCodecSafe(); refresh(); };
    addAndMakeVisible (onOff);
    addAndMakeVisible (safe);
}

void CodecView::refresh()
{
    auto& c = proc.codecCheck();
    on = c.isEnabled();
    for (int k = 0; k < 2; ++k)
    {
        worst[k] = c.available (k) ? c.worstDb (k) : -200.0f;
        clips[k] = c.clippedSamples (k);
    }
    secs = c.measuredSeconds();
    incomplete = c.incomplete();
    const float d = proc.codecSafeChangeDb();
    safe.setEnabled (on && d < 0.0f);
    safe.setButtonText (d < 0.0f ? "SAFE  " + juce::String (d, 1) + " dB" : "SAFE");
    repaint();
}

void CodecView::resized()
{
    auto b = getLocalBounds();
    onOff.setBounds (b.removeFromTop (18));
    safe.setBounds (b.removeFromBottom (24));
    rows = b.reduced (0, 3);
}

void CodecView::paint (juce::Graphics& g)
{
    using namespace juce;
    const int rh = rows.getHeight() / 2;
    for (int k = 0; k < 2; ++k)
    {
        auto r = rows.withY (rows.getY() + k * rh).withHeight (rh);
        g.setFont (ui::caps (10.0f, 1));
        g.setColour (on ? col::dim : col::faint);
        g.drawText (axm::CodecCheck::name (k), r, Justification::centredLeft);
        String v = "--";
        Colour cv = col::faint;
        if (on && secs > 0.5 && worst[k] > -150.0f)
        {
            v = (worst[k] > 0.05f ? "+" : "") + String (worst[k], 1) + (clips[k] > 0 ? "  " + String (clips[k]) + " CLIP" : String());
            cv = worst[k] > 0.0f || clips[k] > 0 ? col::warn.interpolatedWith (Colours::red, 0.6f)
                 : worst[k] > axm::CodecCheck::kTargetDb ? col::warn : col::teal;
        }
        g.setFont (ui::font (12.0f, 1));
        g.setColour (cv);
        g.drawText (v, r, Justification::centredRight);
    }
    if (on && secs < 3.0)
    {
        g.setColour (col::faint);
        g.setFont (ui::caps (8.5f, 1));
        g.drawText ("PLAY THE SONG TO MEASURE", rows.withHeight (8).translated (0, rows.getHeight() - 2), Justification::centredLeft);
    }
    else if (on && incomplete)
    {
        g.setColour (col::faint);
        g.setFont (ui::caps (8.5f, 1));
        g.drawText ("INCOMPLETE (FAST RENDER)", rows.withHeight (8).translated (0, rows.getHeight() - 2), Justification::centredLeft);
    }
}

//==============================================================================
void LoudnessPanel::refresh()
{
    const auto& o = proc.engine().outputLoudness();
    I = o.integratedLufs();
    S = o.shortTermLufs();
    M = o.momentaryLufs();
    lra = o.loudnessRangeLu();
    tp = proc.engine().outputTruePeakDb();
    iIn = proc.engine().inputLoudness().integratedLufs();
    const float s = proc.learnSuggestionDb();
    learn.setButtonText (! proc.isLearning() ? "LEARN" : std::isfinite (s) ? "APPLY " + juce::String (s > 0 ? "+" : "") + juce::String (s, 1) + " dB" : "MEASURING...");
    learn.setToggleState (proc.isLearning(), juce::dontSendNotification);
    repaint();
}

void LoudnessPanel::resized()
{
    auto b = getLocalBounds().reduced (16, 12);
    auto bottom = b.removeFromBottom (30);
    resetBtn.setBounds (bottom.removeFromRight (80));
    bottom.removeFromRight (8);
    learn.setBounds (bottom);
    b.removeFromBottom (10);
    target.setBounds (b.removeFromBottom (28));
}

void LoudnessPanel::paint (juce::Graphics& g)
{
    using namespace juce;
    const auto r = getLocalBounds().toFloat();
    g.setGradientFill (ColourGradient (col::panelTop, 0, r.getY(), col::panel, 0, r.getBottom(), false));
    g.fillRoundedRectangle (r, 10.0f);
    g.setColour (col::warn.withAlpha (0.45f));
    g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.2f);
    auto b = getLocalBounds().reduced (16, 12);
    g.setColour (col::text);
    g.setFont (ui::caps (13.0f, 2));
    g.drawText ("LOUDNESS", b.removeFromTop (20), Justification::centredLeft);
    const float tgt = axm::kTargets[(size_t) proc.targetIndex()].lufs;
    const bool onTarget = tgt < 0.0f && std::isfinite (I) && std::abs (I - tgt) <= 1.0f;
    b.removeFromTop (4);
    g.setColour (onTarget ? col::ok : col::text);
    g.setFont (ui::font (54.0f, 2));
    auto big = b.removeFromTop (60);
    g.drawText (lufsText (I), big.removeFromLeft (180), Justification::centredLeft);
    g.setFont (ui::caps (12.0f, 1));
    g.setColour (col::dim);
    g.drawText ("INTEGRATED LUFS", big.removeFromTop (30), Justification::bottomLeft);
    if (tgt < 0.0f && std::isfinite (I) && I > -70.0f)
    {
        g.setColour (onTarget ? col::ok : col::warn);
        g.drawText (onTarget ? String ("ON TARGET (") + (I - tgt >= 0 ? "+" : "") + String (I - tgt, 1) + " LU)"
                              : String (std::abs (I - tgt), 1) + (I > tgt ? " LU ABOVE TARGET" : " LU BELOW TARGET"),
                    big, Justification::topLeft);
    }
    b.removeFromTop (8);
    struct Row { const char* name; String value; Colour c; };
    const Row rows[] = { { "SHORT-TERM", lufsText (S) + " LUFS", col::warn }, { "MOMENTARY", lufsText (M) + " LUFS", col::text },
                         { "RANGE (LRA)", String (lra, 1) + " LU", col::text },
                         { "TRUE PEAK MAX", tp > -100.0f ? String (tp, 1) + " dBTP" : String ("--"), tp > proc.readParams().ceilingDb + 0.01f ? col::clip : col::text },
                         { "PLR", std::isfinite (I) && I > -70.0f && tp > -100.0f ? String (tp - I, 1) + " LU" : String ("--"), col::text },
                         { "INPUT INTEGRATED", lufsText (iIn) + " LUFS", col::dim } };
    for (const auto& row : rows)
    {
        auto line = b.removeFromTop (24);
        g.setFont (ui::caps (11.0f, 1));
        g.setColour (col::dim);
        g.drawText (row.name, line, Justification::centredLeft);
        g.setFont (ui::font (15.0f, 1));
        g.setColour (row.c);
        g.drawText (row.value, line, Justification::centredRight);
    }
}

//==============================================================================
void Meters::Hold::set (float x, float floor)
{
    v = x;
    if (x >= peak) { peak = x; hold = 45; }
    else if (--hold <= 0) peak = std::max (floor, peak - 0.4f);
}

void Meters::set (const float outDb[2], float limDb, float clipDb, float ceilingDb)
{
    out[0].set (outDb[0], -120.0f);
    out[1].set (outDb[1], -120.0f);
    lim.set (limDb, 0.0f);
    clip.set (clipDb, 0.0f);
    ceiling = ceilingDb;
    repaint();
}

void Meters::paint (juce::Graphics& g)
{
    using namespace juce;
    const auto r = getLocalBounds().toFloat();
    g.setColour (col::well);
    g.fillRoundedRectangle (r, 6.0f);
    auto inner = r.reduced (6.0f, 0.0f).withTrimmedTop (20.0f).withTrimmedBottom (20.0f);
    const float gap = 3.0f, w = (inner.getWidth() - 3.0f * gap - 6.0f) / 4.0f;
    const float lo = -30.0f;
    auto yOut = [&] (float d) { return inner.getBottom() - (jlimit (lo, 0.0f, d) - lo) / -lo * inner.getHeight(); };
    auto yGr = [&] (float d) { return inner.getY() + jlimit (0.0f, 12.0f, d) / 12.0f * inner.getHeight(); };
    float xx = inner.getX();
    for (int c = 0; c < 2; ++c)
    {
        const Rectangle<float> b (xx, inner.getY(), w, inner.getHeight());
        g.setColour (col::track);
        g.fillRect (b);
        const bool over = out[c].peak > ceiling + 0.01f;
        g.setColour (col::text.withAlpha (0.75f));
        g.fillRect (b.withTop (yOut (out[c].v)));
        g.setColour (over ? col::clip : col::text);
        g.drawHorizontalLine ((int) yOut (out[c].peak), b.getX(), b.getRight());
        xx += w + gap;
    }
    g.setColour (col::warn);
    g.drawHorizontalLine ((int) yOut (ceiling), inner.getX() - 2.0f, xx - gap + 2.0f);
    xx += 6.0f;
    for (auto* m : { &lim, &clip })
    {
        const Rectangle<float> b (xx, inner.getY(), w, inner.getHeight());
        g.setColour (col::track);
        g.fillRect (b);
        g.setColour (m == &lim ? col::accent : col::teal);
        g.fillRect (b.withBottom (yGr (m->v)));
        g.setColour (col::text);
        g.drawHorizontalLine ((int) yGr (m->peak), b.getX(), b.getRight());
        xx += w + gap;
    }
    g.setFont (ui::caps (9.5f, 1));
    g.setColour (col::dim);
    const float half = (inner.getWidth() - 6.0f) / 2.0f;
    g.drawText ("OUT", Rectangle<float> (inner.getX(), r.getY() + 3.0f, half, 14.0f), Justification::centred);
    g.drawText ("GR", Rectangle<float> (inner.getX() + half + 6.0f, r.getY() + 3.0f, half, 14.0f), Justification::centred);
    g.setFont (ui::font (10.0f, 1));
    const float pk = std::max (out[0].peak, out[1].peak);
    g.setColour (pk > ceiling + 0.01f ? col::clip : col::text);
    g.drawText (pk > -100.0f ? String (pk, 1) : String ("--"), Rectangle<float> (inner.getX(), inner.getBottom() + 3.0f, half, 14.0f), Justification::centred);
    g.setColour (col::text);
    g.drawText (String (lim.peak + clip.peak, 1), Rectangle<float> (inner.getX() + half + 6.0f, inner.getBottom() + 3.0f, half, 14.0f), Justification::centred);
}

//==============================================================================
void ClipCurve::set (const axm::Params& p, float inPeakDb)
{
    params = p;
    in = inPeakDb;
    if (in >= inHold) { inHold = in; hold = 30; }
    else if (--hold <= 0) inHold = std::max (-120.0f, inHold - 0.5f);
    repaint();
}

void ClipCurve::paint (juce::Graphics& g)
{
    using namespace juce;
    const auto r = getLocalBounds().toFloat();
    g.setColour (col::well);
    g.fillRoundedRectangle (r, 8.0f);
    const auto plot = r.reduced (10.0f);
    // axes: dB relative to the ceiling, -12 .. +9 in and -12 .. +3 out
    const float ceil = params.ceilingDb, inLo = ceil - 12.0f, inHi = ceil + 9.0f, outLo = ceil - 12.0f, outHi = ceil + 3.0f;
    auto px = [&] (float d) { return plot.getX() + (jlimit (inLo, inHi, d) - inLo) / (inHi - inLo) * plot.getWidth(); };
    auto py = [&] (float d) { return plot.getBottom() - (jlimit (outLo, outHi, d) - outLo) / (outHi - outLo) * plot.getHeight(); };
    g.setColour (col::grid);
    for (float d = inLo; d <= inHi + 0.01f; d += 3.0f)
        g.drawVerticalLine ((int) px (d), plot.getY(), plot.getBottom());
    for (float d = outLo; d <= outHi + 0.01f; d += 3.0f)
        g.drawHorizontalLine ((int) py (d), plot.getX(), plot.getRight());
    g.setColour (col::warn.withAlpha (0.7f));
    g.drawHorizontalLine ((int) py (ceil), plot.getX(), plot.getRight());
    g.setFont (ui::font (9.5f, 1));
    g.drawText ("CEILING", Rectangle<float> (plot.getX() + 2.0f, py (ceil) - 13.0f, 80.0f, 12.0f), Justification::left);
    // identity, dashed
    {
        Path d, dashed;
        d.startNewSubPath (px (inLo), py (inLo));
        d.lineTo (px (outHi), py (outHi));
        float dash[] = { 3.0f, 3.0f };
        PathStrokeType (1.0f).createDashedStroke (dashed, d, dash, 2);
        g.setColour (col::faint);
        g.fillPath (dashed);
    }
    const bool on = params.clipPct > 0.0f;
    float thr = 1.0f, knee = 0.0f;
    axm::Engine::clipShape (params, thr, knee);
    Path c;
    for (int i = 0; i <= 120; ++i)
    {
        const float d = inLo + (inHi - inLo) * (float) i / 120.0f;
        const float y = on ? axm::Engine::clipCurve (std::pow (10.0f, d / 20.0f), thr, knee) : std::pow (10.0f, d / 20.0f);
        const float yd = 20.0f * std::log10 (std::max (y, 1.0e-6f));
        if (i == 0) c.startNewSubPath (px (d), py (yd)); else c.lineTo (px (d), py (yd));
    }
    g.setColour (on ? col::teal : col::dim);
    g.strokePath (c, PathStrokeType (2.0f));
    if (inHold > inLo)
    {
        const float y = on ? axm::Engine::clipCurve (std::pow (10.0f, inHold / 20.0f), thr, knee) : std::pow (10.0f, inHold / 20.0f);
        const float yd = 20.0f * std::log10 (std::max (y, 1.0e-6f));
        g.setColour (col::text);
        g.fillEllipse (Rectangle<float> (7.0f, 7.0f).withCentre ({ px (inHold), py (yd) }));
    }
    g.setFont (ui::caps (9.0f, 1));
    g.setColour (col::dim);
    g.drawText (on ? "IN >" : "CLIPPER OFF", plot.withTop (plot.getBottom() - 12.0f), Justification::bottomRight);
}

//==============================================================================
MasterEditor::MasterEditor (MasterProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    const int savedWidth = (int) proc.apvts.state.getProperty (kUiWidth, mui::kWidth);
    setLookAndFeel (&lnf);
    addAndMakeVisible (content);
    gain.bind (proc.apvts, "input", tip::gain);
    ceiling.bind (proc.apvts, "ceiling", tip::ceiling);
    release.bind (proc.apvts, "release", tip::release);
    look.bind (proc.apvts, "lookahead", tip::look);
    link.bind (proc.apvts, "link", tip::link);
    clip.bind (proc.apvts, "clip", tip::clip);
    shape.bind (proc.apvts, "clipShape", tip::shape);
    style.bind (*proc.apvts.getParameter ("style"), tip::style);
    os.bind (*proc.apvts.getParameter ("os"), tip::os);
    dither.bind (proc.apvts, "dither", tip::dither);
    compare.bind (proc.apvts, "compare", tip::compare);
    history.setTooltip (tip::history);
    meters.setTooltip (tip::gr);
    curve.setTooltip (tip::curve);
    for (auto* c : std::initializer_list<juce::Component*> { &presetPrev, &presetNext, &presetName, &presetSave, &slotA, &slotB, &slotCopy,
                                                            &compare, &history, &loudness, &gain, &ceiling, &release, &look, &link,
                                                            &style, &clip, &shape, &os, &dither, &meters, &curve, &codecView })
        content.addAndMakeVisible (c);
    for (auto* b : { &slotA, &slotB })
        b->setColour (juce::TextButton::buttonOnColourId, col::global);
    presetPrev.onClick = [this] { proc.setCurrentProgram ((proc.getCurrentProgram() + proc.getNumPrograms() - 1) % proc.getNumPrograms()); };
    presetNext.onClick = [this] { proc.setCurrentProgram ((proc.getCurrentProgram() + 1) % proc.getNumPrograms()); };
    presetName.onClick = [this] { showPresetMenu(); };
    presetSave.onClick = [this] { promptSavePreset(); };
    slotA.onClick = [this] { proc.selectSlot (0); };
    slotB.onClick = [this] { proc.selectSlot (1); };
    slotCopy.onClick = [this] { proc.copyActiveToOther(); };
    presetName.setTooltip ("Presets: starting points for streaming, loud rap, club, acoustic and broadcast masters. Input gain is "
                           "never changed by a preset. Your own presets live in ~/.local/share/AxellMaster/Presets.");
    slotA.setTooltip ("A/B: two complete settings to compare.");
    slotB.setTooltip ("A/B: two complete settings to compare.");
    slotCopy.setTooltip ("Copy the current slot to the other one.");

    setResizable (true, true);
    setResizeLimits (mui::kWidth * 3 / 4, mui::kHeight * 3 / 4, mui::kWidth * 2, mui::kHeight * 2);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) mui::kWidth / mui::kHeight);
    const int w = juce::jlimit (mui::kWidth * 3 / 4, mui::kWidth * 2, savedWidth);
    setSize (w, juce::roundToInt (w * (double) mui::kHeight / mui::kWidth));
    constructed = true;
    timerCallback();
    startTimerHz (30);
}

MasterEditor::~MasterEditor()
{
    stopTimer();
    saveDialog.reset();
    setLookAndFeel (nullptr);
}

void MasterEditor::paint (juce::Graphics& g) { g.fillAll (col::bg); }

void MasterEditor::resized()
{
    content.setBounds (0, 0, mui::kWidth, mui::kHeight);
    content.setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) mui::kWidth));
    if (constructed)
        proc.apvts.state.setProperty (kUiWidth, getWidth(), nullptr);
}

void MasterEditor::Content::resized() { owner.layout(); }

void MasterEditor::layout()
{
    using juce::Rectangle;
    const int m = 16;
    headerArea = { 0, 0, mui::kWidth, 58 };
    {
        auto pb = Rectangle<int> (330, 14, 400, 30);
        presetPrev.setBounds (pb.removeFromLeft (30));
        pb.removeFromLeft (4);
        presetSave.setBounds (pb.removeFromRight (62));
        pb.removeFromRight (4);
        presetNext.setBounds (pb.removeFromRight (30));
        pb.removeFromRight (4);
        presetName.setBounds (pb);
        auto ab = Rectangle<int> (mui::kWidth - m - 160, 14, 160, 30);
        slotA.setBounds (ab.removeFromLeft (34));
        ab.removeFromLeft (4);
        slotB.setBounds (ab.removeFromLeft (34));
        ab.removeFromLeft (8);
        slotCopy.setBounds (ab);
    }
    const int top = 70, displayH = 330;
    loudness.setBounds (mui::kWidth - m - 360, top, 360, displayH);
    history.setBounds (m, top, mui::kWidth - 2 * m - 360 - 10, displayH);

    const int top2 = top + displayH + 12, h2 = mui::kHeight - top2 - m;
    clipArea = { m, top2, 250, h2 };
    outArea = { mui::kWidth - m - 300, top2, 300, h2 };
    limArea = Rectangle<int>::leftTopRightBottom (clipArea.getRight() + 10, top2, outArea.getX() - 10, top2 + h2);
    {
        auto a = clipArea.reduced (14, 0).withTrimmedTop (46).withTrimmedBottom (12);
        curve.setBounds (a.removeFromTop (150));
        a.removeFromTop (6);
        clip.setBounds (a.removeFromLeft (a.getWidth() / 2));
        shape.setBounds (a);
    }
    {
        auto a = limArea.reduced (14, 0).withTrimmedTop (44).withTrimmedBottom (10);
        styleLabel = a.removeFromTop (16);
        style.setBounds (a.removeFromTop (28));
        a.removeFromTop (10);
        auto big = a.removeFromLeft (a.getWidth() * 28 / 100);
        gain.setBounds (big);
        const int w = a.getWidth() / 4;
        for (auto* k : { &ceiling, &release, &look, &link })
            k->setBounds (a.removeFromLeft (w));
    }
    {
        auto a = outArea.reduced (14, 0).withTrimmedTop (44).withTrimmedBottom (12);
        meters.setBounds (a.removeFromRight (100).withTrimmedBottom (4));
        a.removeFromRight (12);
        osLabel = a.removeFromTop (16);
        os.setBounds (a.removeFromTop (28));
        a.removeFromTop (14);
        ditherLabel = a.removeFromTop (16);
        dither.setBounds (a.removeFromTop (28));
        a.removeFromBottom (22);
        compare.setBounds (a.removeFromBottom (34));
        compareLabel = a.removeFromBottom (20);
        a.removeFromTop (12);
        a.removeFromBottom (8);
        codecView.setBounds (a);
    }
}

void MasterEditor::Content::paint (juce::Graphics& g)
{
    using namespace juce;
    auto& e = owner;
    g.fillAll (col::bg);
    g.setColour (col::header);
    g.fillRect (e.headerArea);
    g.setColour (col::grid);
    g.drawHorizontalLine (e.headerArea.getBottom() - 1, 0.0f, (float) mui::kWidth);
    g.setColour (col::text);
    g.setFont (ui::caps (21.0f, 2));
    g.drawText ("AXELL MASTER", 18, 8, 300, 26, Justification::centredLeft);
    g.setFont (ui::caps (10.0f, 1));
    g.setColour (col::dim);
    g.drawText ("CLIPPER  +  TRUE-PEAK LIMITER  +  LOUDNESS", 19, 33, 300, 14, Justification::centredLeft);
    auto panel = [&g] (Rectangle<int> a, Colour accent, const String& title)
    {
        const auto r = a.toFloat();
        g.setGradientFill (ColourGradient (col::panelTop, 0, r.getY(), col::panel, 0, r.getBottom(), false));
        g.fillRoundedRectangle (r, 10.0f);
        g.setColour (accent.withAlpha (0.5f));
        g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.2f);
        g.setColour (col::text);
        g.setFont (ui::caps (13.0f, 2));
        g.drawText (title, a.getX() + 16, a.getY() + 10, 300, 22, Justification::centredLeft);
        g.setColour (accent.withAlpha (0.9f));
        g.fillRoundedRectangle (r.getX() + 14.0f, r.getY() + 35.0f, 26.0f, 2.0f, 1.0f);
    };
    panel (e.clipArea, col::teal, "CLIPPER");
    panel (e.limArea, col::accent, "LIMITER");
    panel (e.outArea, col::global, "OUTPUT");
    g.setColour (col::dim);
    g.setFont (ui::caps (10.5f, 1));
    g.drawText ("STYLE", e.styleLabel, Justification::centredLeft);
    g.drawText ("OVERSAMPLING (CLIPPER)", e.osLabel, Justification::centredLeft);
    g.drawText ("DITHER (EXPORT ONLY)", e.ditherLabel, Justification::centredLeft);
    g.drawText ("LEVEL-MATCHED A/B", e.compareLabel, Justification::centredLeft);
    g.setFont (ui::font (11.0f));
    g.setColour (col::faint);
    g.drawText ("latency " + String (e.proc.getLatencySamples()) + " smp", e.outArea.reduced (14, 0).withTrimmedBottom (8),
                Justification::bottomLeft);
}

void MasterEditor::timerCallback()
{
    axm::DisplayFrame f;
    float lim = 0.0f, cl = 0.0f, in = -120.0f, out[2] { -120.0f, -120.0f };
    bool any = false;
    while (proc.engine().popDisplay (f))
    {
        history.push (f);
        lim = std::max (lim, f.limGrDb);
        cl = std::max (cl, f.clipGrDb);
        in = std::max (in, f.inPeakDb);
        out[0] = std::max (out[0], f.outPeakChDb[0]);
        out[1] = std::max (out[1], f.outPeakChDb[1]);
        any = true;
    }
    const auto params = proc.readParams();
    history.setTarget (axm::kTargets[(size_t) proc.targetIndex()].lufs);
    if (any)
        history.repaint();
    meters.set (out, lim, cl, params.ceilingDb);
    curve.set (params, in);
    loudness.refresh();
    codecView.refresh();
    presetName.setButtonText (proc.getPresetName());
    {
        // a factory preset's description as the tooltip: what it is for and how to drive it
        juce::String desc;
        for (const auto& fp : axm::factoryPresets())
            if (proc.getPresetName() == fp.name)
                desc = fp.description;
        if (presetName.getTooltip() != desc)
            presetName.setTooltip (desc);
    }
    slotA.setToggleState (proc.getActiveSlot() == 0, juce::dontSendNotification);
    slotB.setToggleState (proc.getActiveSlot() == 1, juce::dontSendNotification);
    slotCopy.setButtonText (proc.getActiveSlot() == 0 ? "COPY A>B" : "COPY B>A");
    content.repaint (outArea);
}

void MasterEditor::showPresetMenu()
{
    juce::PopupMenu menu, user;
    const auto list = axm::factoryPresets();
    // factory presets by group (MASTER, YOUR STUDIO, VOCAL BUS, BEAT / DRUM BUS); the item ids stay the program numbers
    for (const char* cat : axm::kCategories)
    {
        menu.addSectionHeader (cat);
        for (int i = 0; i < (int) list.size(); ++i)
            if (juce::String (list[(size_t) i].category) == cat)
                menu.addItem (1 + i, list[(size_t) i].name, true, i == proc.getCurrentProgram());
    }
    auto files = MasterProcessor::userPresetFolder().findChildFiles (juce::File::findFiles, false, "*.axmpreset");
    files.sort();
    for (int i = 0; i < files.size(); ++i)
        user.addItem (1000 + i, files[i].getFileNameWithoutExtension());
    menu.addSeparator();
    menu.addSectionHeader ("USER");
    if (files.isEmpty())
        menu.addItem (-1, "(no user presets yet)", false);
    else
        menu.addSubMenu ("Your presets", user);
    juce::Component::SafePointer<MasterEditor> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetName), [safe, files] (int id)
    {
        if (safe == nullptr || id <= 0)
            return;
        if (id >= 1000)
            safe->proc.loadUserPreset (files[id - 1000]);
        else
            safe->proc.setCurrentProgram (id - 1);
    });
}

void MasterEditor::promptSavePreset()
{
    using namespace juce;
    saveDialog = std::make_unique<AlertWindow> ("Save preset", "Name for this preset:", MessageBoxIconType::NoIcon, this);
    auto* w = saveDialog.get();
    w->setLookAndFeel (&lnf);
    w->addTextEditor ("name", proc.getPresetName(), {});
    w->addButton ("Save", 1, KeyPress (KeyPress::returnKey));
    w->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
    Component::SafePointer<MasterEditor> safe (this);
    w->enterModalState (true, ModalCallbackFunction::create ([safe] (int result)
    {
        if (safe == nullptr || safe->saveDialog == nullptr)
            return;
        const auto name = safe->saveDialog->getTextEditorContents ("name").trim();
        if (result == 1 && name.isNotEmpty() && ! safe->proc.saveUserPreset (name))
            AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Save preset",
                                              "Could not write to " + MasterProcessor::userPresetFolder().getFullPathName());
        MessageManager::callAsync ([safe] { if (safe != nullptr) safe->saveDialog.reset(); });
    }), false);
}
