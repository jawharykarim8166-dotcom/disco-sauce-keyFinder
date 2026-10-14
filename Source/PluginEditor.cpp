#include "PluginEditor.h"
#include <BinaryData.h>

using juce::Colour;
using juce::Rectangle;
using juce::Justification;

static const Colour kCyan  (0xff69f6ff);
static const Colour kDimCy (0xff2fb0bd);
static const Colour kGold  (0xfff6d365);
static const Colour kPink  (0xffff8fd0);

static juce::String noteName (int pc, bool minor)
{
    static const char* mjr[12] = { "C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    static const char* mnr[12] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "G#", "A", "Bb", "B" };
    if (pc < 0 || pc > 11) return "--";
    return minor ? mnr[pc] : mjr[pc];
}

static juce::String camelot (int pc, bool minor)
{
    static const int mjr[12] = { 8, 3, 10, 5, 12, 7, 2, 9, 4, 11, 6, 1 };
    static const int mnr[12] = { 5, 12, 7, 2, 9, 4, 11, 6, 1, 8, 3, 10 };
    if (pc < 0 || pc > 11) return "--";
    return juce::String (minor ? mnr[pc] : mjr[pc]) + (minor ? "A" : "B");
}

static juce::String mmss (double seconds)
{
    const int s = (int) seconds;
    return juce::String (s / 60) + ":" + juce::String (s % 60).paddedLeft ('0', 2);
}

KeyEditor::KeyEditor (KeyProcessor& p) : juce::AudioProcessorEditor (&p), proc (p)
{
    bg = juce::ImageCache::getFromMemory (BinaryData::bg_jpg, BinaryData::bg_jpgSize);
    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio (1000.0 / 985.0);
    setResizeLimits (500, 492, 1500, 1477);
    setSize (800, 788);
    t0 = juce::Time::getMillisecondCounterHiRes();
    startTimerHz (30);
}

KeyEditor::~KeyEditor() { stopTimer(); }

void KeyEditor::resized()
{
    if (bg.isValid() && getWidth() > 0)
        scaledBg = bg.rescaled (getWidth(), getHeight(), juce::Graphics::highResamplingQuality);
}

void KeyEditor::text (juce::Graphics& g, const juce::String& s, Rectangle<float> r, float size, Colour c,
                      Justification j, float kern)
{
    juce::Font f ("Consolas", size, juce::Font::bold);
    f.setExtraKerningFactor (kern);
    g.setFont (f);
    g.setColour (c);
    g.drawText (s, r, j, false);
}

void KeyEditor::glow (juce::Graphics& g, const juce::String& s, Rectangle<float> r, float size, Justification j)
{
    text (g, s, r.translated (0.f, 2.f), size, kCyan.withAlpha (0.22f), j);
    text (g, s, r, size, kCyan, j);
}

//==============================================================================
void KeyEditor::paint (juce::Graphics& g)
{
    if (scaledBg.isValid()) g.drawImageAt (scaledBg, 0, 0);
    else g.fillAll (juce::Colours::black);
    g.addTransform (juce::AffineTransform::scale (scale()));

    const ReadingInfo info = proc.getInfo();
    const ds::Reading& r = info.r;
    const bool hasBpm = r.valid && r.bpm > 0.0;
    const bool hasKey = r.valid && r.root >= 0;
    const double now = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;

    // BPM shown according to the half / double switch
    double bpmShown = r.bpm;
    if (info.octave == 1) bpmShown = r.bpm / 2.0;
    if (info.octave == 2) bpmShown = r.bpm * 2.0;

    // ---- plates
    text (g, "KEY FINDER", { 430.f, 380.f, 172.f, 40.f }, 18.f, kGold, Justification::centred, 0.12f);
    text (g, "INPUT", { 832.f, 893.f, 146.f, 44.f }, 16.f, Colour (0xffe8ecf8), Justification::centred, 0.1f);

    // ---- upper screen: the readings
    text (g, "BPM",   { 60.f, 443.f, 100.f, 14.f }, 10.f, kDimCy, Justification::left, 0.3f);
    text (g, "KEY",   { 292.f, 443.f, 100.f, 14.f }, 10.f, kDimCy, Justification::left, 0.3f);
    text (g, "SCALE", { 462.f, 443.f, 100.f, 14.f }, 10.f, kDimCy, Justification::left, 0.3f);
    text (g, "CAMELOT", { 462.f, 497.f, 100.f, 14.f }, 10.f, kDimCy, Justification::left, 0.3f);

    glow (g, hasBpm ? juce::String (bpmShown, 1) : juce::String ("---.-"), { 60.f, 455.f, 240.f, 84.f }, 72.f, Justification::left);
    glow (g, hasKey ? noteName (r.root, r.minor) : juce::String ("--"), { 292.f, 455.f, 170.f, 84.f }, 68.f, Justification::left);
    glow (g, hasKey ? (r.minor ? "MINOR" : "MAJOR") : "------", { 462.f, 458.f, 160.f, 34.f }, 28.f, Justification::left);
    glow (g, hasKey ? camelot (r.root, r.minor) : juce::String ("--"), { 462.f, 510.f, 160.f, 34.f }, 28.f, Justification::left);

    // second row: same notes / alternatives
    juce::String rel = hasKey ? ("SAME NOTES AS " + noteName (r.relRoot, ! r.minor) + (r.minor ? " MAJOR" : " MINOR")) : juce::String ("");
    text (g, rel, { 60.f, 552.f, 330.f, 16.f }, 12.f, kDimCy, Justification::left, 0.08f);
    juce::String alt;
    if (hasBpm)
    {
        const double opts[3] = { r.bpm, r.bpm / 2.0, r.bpm * 2.0 };
        alt = "ALSO " + juce::String (opts[(info.octave + 1) % 3], 1) + " / " + juce::String (opts[(info.octave + 2) % 3], 1) + "  (CLICK BPM)";
    }
    text (g, alt, { 330.f, 552.f, 290.f, 16.f }, 12.f, kDimCy, Justification::right, 0.05f);

    // confidence bars
    auto bar = [&] (float x, float y, float w, const juce::String& label, float conf)
    {
        text (g, label, { x, y - 1.f, 78.f, 14.f }, 10.f, kDimCy, Justification::left, 0.2f);
        const Rectangle<float> tr (x + 80.f, y + 2.f, w - 128.f, 8.f);
        g.setColour (Colour (0xff0b2a33));
        g.fillRoundedRectangle (tr, 4.f);
        if (conf > 0.f)
        {
            g.setGradientFill (juce::ColourGradient (Colour (0xffff8fa0), tr.getX(), 0.f, Colour (0xff9dff6b), tr.getRight(), 0.f, false));
            g.fillRoundedRectangle (tr.withWidth (tr.getWidth() * juce::jlimit (0.f, 1.f, (conf - 30.f) / 69.f)), 4.f);
        }
        text (g, conf > 0.f ? juce::String (juce::roundToInt (conf)) + "%" : juce::String ("--"), { x + w - 44.f, y - 1.f, 44.f, 14.f }, 11.f, kCyan, Justification::right);
    };
    bar (60.f, 586.f, 270.f, "BPM CONF", hasBpm ? r.bpmConf : 0.f);
    bar (350.f, 586.f, 270.f, "KEY CONF", hasKey ? r.keyConf : 0.f);

    // status line
    juce::String status;
    Colour statusCol = kCyan;
    if (info.source == 2 && r.valid)       status = "FILE: " + info.fileName.toUpperCase();
    else if (info.source == 2)             status = "FILE HAS NO CLEAR TEMPO OR KEY";
    else if (info.analyzing && ! r.valid)  status = "ANALYZING...";
    else if (info.source == 1 && r.valid)
    {
        status = "LIVE - " + mmss (juce::jmin ((double) info.heardSeconds, 120.0)) + " OF YOUR TRACK HEARD";
        if (r.seconds >= 20.f && r.bpmConf >= 85.f && r.keyConf >= 70.f) status += " - LOCKED";
        else if (info.analyzing) status += " - REFINING";
    }
    else if (info.heardSeconds > 0.1f && info.signalPresent) status = "LISTENING " + mmss (info.heardSeconds) + " / 0:08 - KEEP PLAYING";
    else if (info.heardSeconds > 0.1f)     status = "LISTENING " + mmss (info.heardSeconds) + " / 0:08";
    else { status = "PLAY YOUR TRACK THROUGH ME OR DROP AN AUDIO FILE"; statusCol = kDimCy; }
    text (g, status, { 60.f, 640.f, 560.f, 18.f }, 12.f, statusCol, Justification::left, 0.06f);
    text (g, "DOUBLE-CLICK THE SCREEN TO RESTART", { 60.f, 662.f, 560.f, 14.f }, 9.f, kDimCy, Justification::left, 0.1f);

    // ---- lower screen: note energy graph + beat light
    text (g, "NOTE ENERGY", { 60.f, 708.f, 200.f, 14.f }, 10.f, kDimCy, Justification::left, 0.3f);
    {
        double mx = 1e-9;
        for (int i = 0; i < 12; ++i) mx = juce::jmax (mx, r.chroma[i]);
        static const int majScale[7] = { 0, 2, 4, 5, 7, 9, 11 }, minScale[7] = { 0, 2, 3, 5, 7, 8, 10 };
        for (int i = 0; i < 12; ++i)
        {
            const float x = 62.f + (float) i * 42.f;
            const float h = hasKey ? (float) (r.chroma[i] / mx) * 128.f : 3.f;
            bool inScale = false, tonic = false;
            if (hasKey)
            {
                const int d = (i - r.root + 12) % 12;
                for (int k = 0; k < 7; ++k) if ((r.minor ? minScale[k] : majScale[k]) == d) inScale = true;
                tonic = (d == 0);
            }
            Colour c = tonic ? kGold : (inScale ? kCyan : Colour (0xff3b4a66));
            const Rectangle<float> b (x, 878.f - h - 2.f, 34.f, h + 2.f);
            g.setColour (c.withAlpha (0.25f));
            g.fillRoundedRectangle (b.expanded (2.f), 5.f);
            g.setColour (c);
            g.fillRoundedRectangle (b, 4.f);
            static const char* nn[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
            text (g, nn[i], { x - 4.f, 884.f, 42.f, 16.f }, 12.f, tonic ? kGold : kDimCy, Justification::centred);
        }
    }

    // beat light (follows the detected tempo)
    {
        const float cx = 696.f, cy = 800.f;
        g.setColour (Colour (0xff0b2a33));
        g.fillEllipse (cx - 56.f, cy - 56.f, 112.f, 112.f);
        g.setColour (kDimCy);
        g.drawEllipse (cx - 56.f, cy - 56.f, 112.f, 112.f, 3.f);
        if (hasBpm)
        {
            const double ph = std::fmod (now * bpmShown / 60.0, 1.0);
            const float a = (float) std::exp (-ph * 5.0);
            g.setColour (kGold.withAlpha (0.15f + 0.85f * a));
            g.fillEllipse (cx - 40.f * (0.7f + 0.3f * a), cy - 40.f * (0.7f + 0.3f * a), 80.f * (0.7f + 0.3f * a), 80.f * (0.7f + 0.3f * a));
        }
        text (g, "TEMPO", { cx - 50.f, cy + 62.f, 100.f, 16.f }, 11.f, kDimCy, Justification::centred, 0.25f);
    }

    // ---- input meter inside the slider groove of the artwork
    {
        const float lv = juce::jlimit (0.f, 1.f, std::sqrt (juce::jmin (1.f, proc.getLevel())));
        peakHold = juce::jmax (lv, peakHold - 0.012f);
        const float top = 437.f, bottom = 845.f, h = (bottom - top) * lv;
        const Rectangle<float> m (896.f, bottom - h, 12.f, h);
        if (h > 1.f)
        {
            g.setGradientFill (juce::ColourGradient (Colour (0xffff5a5a), 0.f, top, Colour (0xff6bff9d), 0.f, bottom, false));
            g.fillRoundedRectangle (m, 5.f);
        }
        g.setColour (kGold);
        g.fillRect (894.f, bottom - (bottom - top) * peakHold - 1.5f, 16.f, 3.f);
    }

    if (dropHover)
    {
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.fillRoundedRectangle (36.f, 425.f, 736.f, 511.f, 26.f);
        text (g, "DROP AUDIO FILE TO ANALYZE", { 36.f, 425.f, 736.f, 511.f }, 28.f, kGold, Justification::centred);
    }
}

//==============================================================================
void KeyEditor::mouseDown (const juce::MouseEvent& e)
{
    const auto p = e.position / scale();
    if (Rectangle<float> (56.f, 450.f, 240.f, 95.f).contains (p))
        proc.cycleOctave();
}

void KeyEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto p = e.position / scale();
    if (Rectangle<float> (36.f, 425.f, 736.f, 511.f).contains (p))
    {
        proc.resetReading();
        repaint();
    }
}

bool KeyEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
    {
        const auto ext = juce::File (f).getFileExtension().toLowerCase();
        if (ext == ".wav" || ext == ".mp3" || ext == ".flac" || ext == ".aif" || ext == ".aiff" || ext == ".ogg" || ext == ".m4a" || ext == ".wma")
            return true;
    }
    return false;
}

void KeyEditor::filesDropped (const juce::StringArray& files, int, int)
{
    dropHover = false;
    if (files.size() > 0) proc.loadFile (juce::File (files[0]));
    repaint();
}
