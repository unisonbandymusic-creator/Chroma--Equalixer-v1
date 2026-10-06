#include "PluginEditor.h"

namespace {
struct ZoneDef { float a, b; int col; const char* n; };      // col 0 green, 1 white, 2 red
const ZoneDef kZones[] = { {20,100,0,"SUB/LOW"}, {100,200,1,"WARM"}, {200,500,2,"MUD"}, {500,2000,1,"PRESENCE"},
                           {2000,6000,2,"HARSH"}, {6000,8000,0,""}, {8000,16000,1,"AIR"}, {16000,20000,0,""} };
juce::Colour zc (int c) { return c == 2 ? kRed : (c == 1 ? kWht : kG); }
juce::Colour zoneColour (double f) { for (auto& z : kZones) if (f >= z.a && f < z.b) return zc (z.col); return kG; }
}

SpectrumView::SpectrumView (ChromaEqProcessor& p) : proc (p)
{
    for (int i = 0; i < 4096; ++i) win[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * i / 4096));
    smooth.fill (-100.f); dry.fill (-100.f); startTimerHz (30);
}

void SpectrumView::timerCallback()
{
    for (int pass = 0; pass < 2; ++pass) {
        proc.copyLatest (pass == 0, buf.data(), 4096);
        for (int i = 0; i < 4096; ++i) buf[(size_t) i] *= win[(size_t) i];
        std::fill (buf.begin() + 4096, buf.end(), 0.f);
        fft.performFrequencyOnlyForwardTransform (buf.data(), true);
        auto& s = pass == 0 ? smooth : dry;
        for (int k = 0; k < 2048; ++k) {
            const float v = juce::jlimit (-100.f, 0.f, 20.f * std::log10 (buf[(size_t) k] / 4096.f + 1e-9f));
            s[(size_t) k] = v > s[(size_t) k] ? v : s[(size_t) k] * 0.86f + v * 0.14f;
        }
    }
    repaint();
}

void SpectrumView::paint (juce::Graphics& g)
{
    const float W = (float) getWidth(), H = (float) getHeight(), L = 36.f, R = 8.f, T = 30.f, B = 24.f, PW = W - L - R, PH = H - T - B;
    const double LMIN = std::log10 (20.0), LMAX = std::log10 (20000.0), fs = proc.getFs();
    auto fx = [&](double f) { return L + (float) ((std::log10 (f) - LMIN) / (LMAX - LMIN)) * PW; };
    auto xf = [&](float x) { return std::pow (10.0, LMIN + (x - L) / PW * (LMAX - LMIN)); };
    auto yDb = [&](float db) { return T + PH * (1.f - (db + 100.f) / 90.f); };
    g.fillAll (juce::Colours::black);

    g.setFont (9.f);
    for (auto& z : kZones) {
        const float x0 = fx (z.a), x1 = fx (z.b);
        if (z.col != 0) { g.setColour (zc (z.col).withAlpha (0.07f)); g.fillRect (x0, T, x1 - x0, PH); }
        g.setColour (zc (z.col).withAlpha (0.9f)); g.fillRect (x0 + 1, T - 6, x1 - x0 - 2, 3.f);
        if (*z.n) g.drawText (z.n, juce::Rectangle<float> (x0, T - 22, x1 - x0, 12), juce::Justification::centred);
    }
    g.setFont (10.f);
    for (int db = -90; db <= -20; db += 10) {
        const float y = yDb ((float) db);
        g.setColour (kG.withAlpha (db % 20 == 0 ? 0.22f : 0.1f)); g.drawHorizontalLine ((int) y, L, W - R);
        if (db % 20 == 0) { g.setColour (kG.withAlpha (0.7f)); g.drawText (juce::String (db), 0, (int) y - 6, (int) L - 5, 12, juce::Justification::right); }
    }
    g.setColour (kG.withAlpha (0.7f)); g.drawText ("dB", 0, (int) T - 4, (int) L - 5, 12, juce::Justification::right);
    static const float grid[] = { 20,30,40,50,60,70,80,90,100,200,300,400,500,600,700,800,900,1000,2000,3000,4000,5000,6000,7000,8000,9000,10000,20000 };
    for (float f : grid) { const bool maj = f == 20 || f == 50 || f == 100 || f == 200 || f == 500 || f == 1000 || f == 2000 || f == 5000 || f == 10000 || f == 20000;
        g.setColour (kG.withAlpha (maj ? 0.3f : 0.1f)); g.drawVerticalLine ((int) fx (f), T, T + PH); }
    g.setColour (kG.withAlpha (0.85f));
    const std::pair<float, const char*> majors[] = { {20,"20"},{50,"50"},{100,"100"},{200,"200"},{500,"500"},{1000,"1k"},{2000,"2k"},{5000,"5k"},{10000,"10k"},{20000,"20k"} };
    for (auto& m : majors) { const float x = fx (m.first); g.drawText (m.second, (int) x - 20, (int) H - 18, 40, 12,
        m.first == 20 ? juce::Justification::left : (m.first == 20000 ? juce::Justification::right : juce::Justification::centred)); }

    // spectrum bars
    const double nyq = fs * 0.5; const int N = 2048;
    auto binMax = [&](const std::array<float, 2048>& a, float x) { const double f0 = xf (x), f1 = xf (x + 2);
        const int b0 = (int) std::floor (f0 / nyq * N), b1 = std::max (b0, (int) std::ceil (f1 / nyq * N)); float m = -100.f;
        for (int b = std::max (0, b0); b <= std::min (N - 1, b1); ++b) m = std::max (m, a[(size_t) b]); return m; };
    g.saveState(); g.reduceClipRegion ((int) L, (int) T, (int) PW, (int) PH);
    for (float x = L; x < W - R; x += 2.f) {
        const float y = std::min (T + PH, yDb (binMax (smooth, x))), h = T + PH - y; if (h < 1.f) continue;
        const auto col = zoneColour ((xf (x) + xf (x + 2)) * 0.5);
        g.setColour (col.withAlpha (0.55f)); g.fillRect (x, y, 2.f, h); g.setColour (col); g.fillRect (x, y, 2.f, 2.f);
    }
    const bool wet = proc.getPlain ("wet") > 0.5f;
    if (wet) {
        juce::Path p, dashed; bool first = true;
        for (float x = L; x < W - R; x += 2.f) { const float y = yDb (binMax (dry, x)); if (first) { p.startNewSubPath (x, y); first = false; } else p.lineTo (x, y); }
        const float dl[] = { 3.f, 2.f }; juce::PathStrokeType (1.2f).createDashedStroke (dashed, p, dl, 2);
        g.setColour (juce::Colours::white.withAlpha (0.75f)); g.fillPath (dashed);
    }
    g.restoreState();

    // EQ curve +/-12 dB
    const auto cs = proc.makeCoefs (fs, !wet);
    auto dbAt = [&](double f) { double m = 1; for (auto& c : cs) m *= c->getMagnitudeForFrequency (f, fs); return (float) (20 * std::log10 (m)); };
    const float y0 = T + PH / 2, k = PH / 2 / 12 * 0.95f;
    auto yE = [&](float db) { return y0 - juce::jlimit (-12.f, 12.f, db) * k; };
    g.setColour (juce::Colours::white.withAlpha (0.35f)); g.drawHorizontalLine ((int) y0, L, W - R);
    std::vector<float> dbs; for (float x = L; x < W - R; x += 2.f) dbs.push_back (dbAt (xf (x)));
    for (int sg = 1; sg >= -1; sg -= 2) {
        juce::Path fill; fill.startNewSubPath (L, y0); float x = L;
        for (float d : dbs) { fill.lineTo (x, sg > 0 ? std::min (y0, yE (d)) : std::max (y0, yE (d))); x += 2.f; }
        fill.lineTo (x - 2.f, y0); fill.closeSubPath();
        g.setColour (sg > 0 ? juce::Colours::white.withAlpha (0.22f) : kRed.withAlpha (0.30f)); g.fillPath (fill);
    }
    juce::Path line; float x = L; for (float d : dbs) { x == L ? line.startNewSubPath (x, yE (d)) : line.lineTo (x, yE (d)); x += 2.f; }
    g.setColour (juce::Colours::white); g.strokePath (line, juce::PathStrokeType (2.5f));
    g.setFont (9.f); g.setColour (juce::Colours::white.withAlpha (0.8f));
    g.drawText ("EQ +12", (int) (W - R - 60), (int) yE (12) + 1, 56, 12, juce::Justification::right);
    g.drawText ("-12", (int) (W - R - 60), (int) yE (-12) - 13, 56, 12, juce::Justification::right);
    if (wet) for (int i = 0; i < 5; ++i) {
        const float gn = proc.getPlain ("g" + juce::String (i)); if (std::abs (gn) < 0.05f) continue;
        const double f = proc.getPlain ("f" + juce::String (i)); const float px = fx (f), py = yE (dbAt (f));
        g.setColour (gn < 0 ? kRed : juce::Colours::white); g.fillEllipse (px - 5, py - 5, 10, 10);
        const juce::String t = (gn > 0 ? "+" : "") + juce::String (gn, 1); const float ty = gn < 0 ? py + 9 : py - 21;
        g.setColour (juce::Colours::black.withAlpha (0.85f)); g.fillRect (px - 18, ty, 36.f, 13.f);
        g.setColour (gn < 0 ? kRed : juce::Colours::white); g.drawText (t, (int) px - 18, (int) ty, 36, 13, juce::Justification::centred);
    }
    // analysis markers
    int mi = 0; for (auto& m : proc.markers) {
        const float mx = fx (m.f); g.setColour (m.c.withAlpha (0.95f)); g.drawVerticalLine ((int) mx, T, T + PH);
        const int tw = 8 * m.t.length() + 8; const float ty = T + 4 + (mi++ % 3) * 14;
        const float tx = juce::jlimit (L + tw / 2.f, W - R - tw / 2.f, mx);
        g.setColour (juce::Colours::black.withAlpha (0.8f)); g.fillRect (tx - tw / 2.f, ty, (float) tw, 13.f);
        g.setColour (m.c); g.drawText (m.t, (int) (tx - tw / 2.f), (int) ty, tw, 13, juce::Justification::centred);
    }
    g.setColour (kG.withAlpha (0.5f)); g.drawRect (L, T, PW, PH, 1.f);
}

// ---------------------------------------------------------------- editor
ChromaEqEditor::ChromaEqEditor (ChromaEqProcessor& p) : AudioProcessorEditor (&p), proc (p), spec (p)
{
    setLookAndFeel (&lnf); setSize (780, 840);
    addAndMakeVisible (spec);
    for (auto* b : { &swDry, &swWet, &modeA, &modeB, &iLow, &iMid, &iHigh, &analyzeBtn, &resetBtn }) addAndMakeVisible (*b);
    swDry.onClick = [this] { proc.setPlain ("wet", 0.f); };  swWet.onClick = [this] { proc.setPlain ("wet", 1.f); };
    modeA.onClick = [this] { proc.setPlain ("mode", 0.f); }; modeB.onClick = [this] { proc.setPlain ("mode", 1.f); };
    iLow.onClick = [this] { proc.setPlain ("inten", 0.f); }; iMid.onClick = [this] { proc.setPlain ("inten", 1.f); }; iHigh.onClick = [this] { proc.setPlain ("inten", 2.f); };
    analyzeBtn.onClick = [this] { proc.isAnalyzing() ? proc.cancelAnalysis() : proc.startAnalysis(); };
    resetBtn.onClick = [this] { proc.resetCurve(); };

    for (int i = 0; i < 5; ++i) {
        fS[i].setName ("f" + juce::String (i)); gS[i].setName ("g" + juce::String (i));
        for (auto* s : { &fS[i], &gS[i] }) { s->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            s->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 74, 18); addAndMakeVisible (*s); }
        atts.push_back (std::make_unique<SA> (proc.apvts, "f" + juce::String (i), fS[i]));
        atts.push_back (std::make_unique<SA> (proc.apvts, "g" + juce::String (i), gS[i]));
        fS[i].textFromValueFunction = [](double v) { return fmtF (v); };
        fS[i].valueFromTextFunction = [](const juce::String& t) { const double v = t.getDoubleValue(); return t.containsIgnoreCase ("k") ? v * 1000.0 : v; };
        gS[i].textFromValueFunction = [](double v) { return juce::String (v > 0.04 ? "+" : "") + juce::String (v, 1) + " dB"; };
        fS[i].updateText(); gS[i].updateText();
    }
    outS.setSliderStyle (juce::Slider::LinearHorizontal); outS.setTextBoxStyle (juce::Slider::TextBoxRight, false, 74, 20); addAndMakeVisible (outS);
    atts.push_back (std::make_unique<SA> (proc.apvts, "out", outS));
    outS.textFromValueFunction = [](double v) { return juce::String (v, 1) + " dB"; }; outS.updateText();

    logBox.setMultiLine (true); logBox.setReadOnly (true); logBox.setScrollbarsShown (true); logBox.setFont (juce::FontOptions (13.f));
    logBox.setText ("[Ready. Put the plugin on a track, press play in your DAW, then press ANALYZE AUDIO. It listens for about 5 seconds, finds mud, harshness and tonal character, and sets a level-matched EQ.]\n");
    addAndMakeVisible (logBox);
    proc.onLog = [this] (const juce::String& s) { logBox.moveCaretToEnd(); logBox.insertTextAtCaret (s + "\n"); };
    startTimerHz (20);
}

ChromaEqEditor::~ChromaEqEditor() { proc.onLog = nullptr; setLookAndFeel (nullptr); }

void ChromaEqEditor::timerCallback()
{
    const bool wet = proc.getPlain ("wet") > 0.5f; const int m = (int) proc.getPlain ("mode"), iv = (int) proc.getPlain ("inten");
    swWet.setToggleState (wet, juce::dontSendNotification); swDry.setToggleState (! wet, juce::dontSendNotification);
    modeA.setToggleState (m == 0, juce::dontSendNotification); modeB.setToggleState (m == 1, juce::dontSendNotification);
    iLow.setToggleState (iv == 0, juce::dontSendNotification); iMid.setToggleState (iv == 1, juce::dontSendNotification); iHigh.setToggleState (iv == 2, juce::dontSendNotification);
    analyzeBtn.setButtonText (proc.isAnalyzing() ? "CANCEL ANALYSIS" : "ANALYZE AUDIO");
    repaint (0, 380, getWidth(), 80);
}

void ChromaEqEditor::paint (juce::Graphics& g)
{
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff04200c), getWidth() / 2.f, -40.f, juce::Colour (0xff010a03), getWidth() / 2.f, 400.f, false));
    g.fillAll();
    g.setColour (kG); g.setFont (juce::Font (juce::FontOptions (22.f, juce::Font::bold))); g.drawText ("CHROMA-EQUALIZER", 14, 8, 400, 28, juce::Justification::left);
    g.setFont (11.f); g.setColour (kG.withAlpha (0.55f)); g.drawText ("INTELLIGENT MUD / HARSH REMOVER  -  HIM'Z DSP  -  LINEAR-PHASE FIR, 93 MS LATENCY (COMPENSATED)", 14, 36, 700, 14, juce::Justification::left);
    g.setColour (kG); g.fillRoundedRectangle (700.f, 10.f, 66.f, 34.f, 10.f); g.setColour (juce::Colour (0xff021a07));
    g.setFont (juce::Font (juce::FontOptions (20.f, juce::Font::bold))); g.drawText ("v1", 700, 10, 66, 34, juce::Justification::centred);

    g.setFont (12.f); g.setColour (kG); g.drawText ("INTENSITY", 12, 404, 90, 30, juce::Justification::left);
    g.setColour (kG.withAlpha (0.55f)); g.drawText (proc.statusText, 590, 406, 180, 12, juce::Justification::right);
    const float pr = proc.progress(); g.setColour (kG.withAlpha (0.18f)); g.fillRoundedRectangle (400.f, 440.f, 368.f, 5.f, 2.f);
    g.setColour (kG); g.fillRoundedRectangle (400.f, 440.f, 368.f * pr, 5.f, 2.f);

    for (int i = 0; i < 5; ++i) {
        const int x = 15 + i * 150; const auto& n = kNodes[i];
        const juce::Colour tc = n.type == 1 && juce::String (n.tag) != "TONAL" ? kRed : kWht;
        g.setColour (kG); g.setFont (12.f); g.drawText (juce::String (i + 1) + ". " + n.name, x, 452, 110, 16, juce::Justification::left);
        g.setColour (tc); g.drawRoundedRectangle ((float) x + 100, 453.f, 44.f, 14.f, 4.f, 1.f); g.setFont (9.f); g.drawText (n.tag, x + 100, 453, 44, 14, juce::Justification::centred);
        g.setColour (kG.withAlpha (0.55f)); g.drawText ("FREQ", x + 5, 470, 70, 10, juce::Justification::centred); g.drawText ("GAIN", x + 75, 470, 70, 10, juce::Justification::centred);
    }
    g.setColour (kG); g.setFont (12.f); g.drawText ("OUT", 12, 592, 50, 26, juce::Justification::left);
    g.setColour (kG.withAlpha (0.55f)); g.setFont (11.f); g.drawText ("DSP SUGGESTIONS", 12, 624, 300, 14, juce::Justification::left);
}

void ChromaEqEditor::resized()
{
    spec.setBounds (12, 58, 756, 300);
    swDry.setBounds (12, 366, 180, 30);  swWet.setBounds (200, 366, 180, 30);
    modeA.setBounds (396, 366, 184, 30); modeB.setBounds (584, 366, 184, 30);
    iLow.setBounds (110, 404, 80, 30); iMid.setBounds (196, 404, 80, 30); iHigh.setBounds (282, 404, 80, 30);
    analyzeBtn.setBounds (400, 402, 368, 34);
    for (int i = 0; i < 5; ++i) { const int x = 15 + i * 150; fS[i].setBounds (x + 5, 480, 70, 106); gS[i].setBounds (x + 75, 480, 70, 106); }
    outS.setBounds (60, 592, 560, 26); resetBtn.setBounds (640, 592, 128, 26);
    logBox.setBounds (12, 642, 756, 186);
}
