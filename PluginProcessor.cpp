#include "PluginProcessor.h"
#include "PluginEditor.h"

static const char* kIds[] = { "f0","f1","f2","f3","f4","g0","g1","g2","g3","g4","q0","q1","q2","q3","q4","out","wet","mode","inten" };

juce::AudioProcessorValueTreeState::ParameterLayout ChromaEqProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout L;
    for (int i = 0; i < 5; ++i) {
        const auto& n = kNodes[i]; const juce::String nm (n.name);
        juce::NormalisableRange<float> fr (n.fmin, n.fmax, 0.01f); fr.setSkewForCentre (std::sqrt (n.fmin * n.fmax));
        L.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "f" + juce::String (i), 1 }, nm + " Freq", fr, n.f));
        L.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "g" + juce::String (i), 1 }, nm + " Gain", juce::NormalisableRange<float> (-18.f, 12.f, 0.1f), 0.f));
        L.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "q" + juce::String (i), 1 }, nm + " Q", juce::NormalisableRange<float> (0.1f, 12.f, 0.01f), n.q));
    }
    L.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "out", 1 }, "Output", juce::NormalisableRange<float> (-24.f, 12.f, 0.1f), 0.f));
    L.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "wet", 1 }, "EQ On (Wet)", true));
    L.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "mode", 1 }, "Mode", juce::StringArray { "A", "B" }, 0));
    L.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "inten", 1 }, "Intensity", juce::StringArray { "LOW", "MID", "HIGH" }, 1));
    return L;
}

ChromaEqProcessor::ChromaEqProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    for (auto* id : kIds) apvts.addParameterListener (id, this);
    outParam = apvts.getRawParameterValue ("out");
    aBuf.assign (2 * kAFft, 0.f);
    startTimerHz (30);
}

ChromaEqProcessor::~ChromaEqProcessor()
{
    stopTimer();
    for (auto* id : kIds) apvts.removeParameterListener (id, this);
}

bool ChromaEqProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto o = l.getMainOutputChannelSet();
    return (o == juce::AudioChannelSet::mono() || o == juce::AudioChannelSet::stereo()) && o == l.getMainInputChannelSet();
}

void ChromaEqProcessor::setPlain (const juce::String& id, float v)
{
    if (auto* p = apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v));
}
float ChromaEqProcessor::getPlain (const juce::String& id) const { return apvts.getRawParameterValue (id)->load(); }

std::array<ChromaEqProcessor::Coefs::Ptr, 5> ChromaEqProcessor::makeCoefs (double fs, bool flat) const
{
    std::array<Coefs::Ptr, 5> c;
    for (int i = 0; i < 5; ++i) {
        const float f = juce::jmin ((float) (0.49 * fs), getPlain ("f" + juce::String (i)));
        const float q = getPlain ("q" + juce::String (i));
        const float gl = juce::Decibels::decibelsToGain (flat ? 0.f : getPlain ("g" + juce::String (i)));
        c[(size_t) i] = kNodes[i].type == 0 ? Coefs::makeLowShelf (fs, f, q, gl)
                      : kNodes[i].type == 2 ? Coefs::makeHighShelf (fs, f, q, gl)
                                            : Coefs::makePeakFilter (fs, f, q, gl);
    }
    return c;
}

// Linear-phase FIR: zero-phase magnitude from the 5 bands -> IFFT -> centre -> Hann window.
juce::AudioBuffer<float> ChromaEqProcessor::buildIR (double fs)
{
    const int N = kFirN, H = N / 2;
    const auto cs = makeCoefs (fs, getPlain ("wet") < 0.5f);
    juce::dsp::FFT fft (13);
    std::vector<std::complex<float>> in ((size_t) N), out ((size_t) N);
    double mag0 = 1;
    for (int k = 0; k <= H; ++k) {
        const double f = std::max (1.0, k * fs / N); double m = 1;
        for (auto& c : cs) m *= c->getMagnitudeForFrequency (f, fs);
        if (k == 0) mag0 = m;
        in[(size_t) k] = (float) m; if (k > 0 && k < H) in[(size_t) (N - k)] = (float) m;
    }
    fft.perform (in.data(), out.data(), true);
    double sum = 0; for (auto& v : out) sum += v.real();
    const double scale = std::abs (sum) > 1e-12 ? mag0 / sum : 1.0;          // self-calibrating, independent of FFT scaling convention
    juce::AudioBuffer<float> ir (1, N); auto* d = ir.getWritePointer (0);
    for (int i = 0; i < N; ++i)
        d[i] = (float) (out[(size_t) ((i + H) % N)].real() * scale * (0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * i / N)));
    return ir;
}

void ChromaEqProcessor::loadIR()
{
    const double fs = getFs();
    conv.loadImpulseResponse (buildIR (fs), fs, juce::dsp::Convolution::Stereo::yes,
                              juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
}

void ChromaEqProcessor::prepareToPlay (double sr, int bs)
{
    currentFs = sr;
    juce::dsp::ProcessSpec spec { sr, (juce::uint32) bs, (juce::uint32) juce::jmax (1, getTotalNumOutputChannels()) };
    conv.prepare (spec); conv.reset();
    outGain.prepare (spec); outGain.setRampDurationSeconds (0.02);
    setLatencySamples (kFirN / 2);
    irDirty = false; loadIR();
}

void ChromaEqProcessor::tap (const juce::AudioBuffer<float>& b, std::array<float, kRing>& ring, std::atomic<int64_t>& cnt)
{
    const int64_t c = cnt.load (std::memory_order_relaxed); const int n = b.getNumSamples();
    const float* l = b.getReadPointer (0); const float* r = b.getNumChannels() > 1 ? b.getReadPointer (1) : l;
    for (int i = 0; i < n; ++i) ring[(size_t) ((c + i) & (kRing - 1))] = 0.5f * (l[i] + r[i]);
    cnt.store (c + n, std::memory_order_release);
}

void ChromaEqProcessor::copyLatest (bool outputTap, float* dst, int n) const
{
    const auto& ring = outputTap ? outRing : inRing;
    const int64_t c = (outputTap ? outCount : inCount).load (std::memory_order_acquire);
    for (int i = 0; i < n; ++i) dst[i] = ring[(size_t) ((c - n + i) & (kRing - 1))];
}

void ChromaEqProcessor::processBlock (juce::AudioBuffer<float>& buf, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals nd;
    const int n = buf.getNumSamples();
    for (int i = getTotalNumInputChannels(); i < getTotalNumOutputChannels(); ++i) buf.clear (i, 0, n);
    tap (buf, inRing, inCount);
    juce::dsp::AudioBlock<float> block (buf);
    juce::dsp::ProcessContextReplacing<float> ctx (block);
    conv.process (ctx);
    outGain.setGainDecibels (outParam->load());
    outGain.process (ctx);
    tap (buf, outRing, outCount);
}

void ChromaEqProcessor::timerCallback()
{
    if (irDirty.exchange (false)) loadIR();
    if (analyzing) pumpAnalysis();
}

// ---------------- live analysis ----------------
void ChromaEqProcessor::startAnalysis()
{
    analyzing = true; aRead = inCount.load(); aCnt = 0;
    aTarget = juce::jmax (1, (int) std::ceil (5.0 * getFs() / kAFft));
    acc.assign (kAFft / 2, 0.0); statusText = "ANALYZING";
    log ("Listening to the input (about 5 s of audible signal). Press play in your DAW, ideally on the loudest section.");
}
void ChromaEqProcessor::cancelAnalysis() { analyzing = false; statusText = "IDLE"; log ("Analysis cancelled."); }

void ChromaEqProcessor::pumpAnalysis()
{
    int64_t wp = inCount.load (std::memory_order_acquire);
    if (wp - aRead > kRing - 2 * kAFft) aRead = wp - kAFft;                       // fell behind: resync
    while (analyzing && wp - aRead >= kAFft) {
        double en = 0;
        for (int i = 0; i < kAFft; ++i) {
            const float x = inRing[(size_t) ((aRead + i) & (kRing - 1))]; en += (double) x * x;
            aBuf[(size_t) i] = x * (float) (0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * i / kAFft));
        }
        aRead += kAFft;
        if (en / kAFft < 1e-9) continue;                                           // skip digital silence
        std::fill (aBuf.begin() + kAFft, aBuf.end(), 0.f);
        aFft.performFrequencyOnlyForwardTransform (aBuf.data(), true);
        for (int k = 0; k < kAFft / 2; ++k) acc[(size_t) k] += (double) aBuf[(size_t) k] * aBuf[(size_t) k];
        if (++aCnt >= aTarget) { finishAnalysis(); break; }
    }
}

double ChromaEqProcessor::matchLevel (const std::vector<double>& avg, double binHz)
{
    const auto cs = makeCoefs (getFs(), false); double num = 0, den = 0;
    for (size_t k = 0; k < avg.size(); ++k) {
        double m = 1; for (auto& c : cs) m *= c->getMagnitudeForFrequency (std::max (1.0, k * binHz), getFs());
        const double w = std::pow (10.0, avg[k] / 10); num += w * m * m; den += w;
    }
    return juce::jlimit (-6.0, 6.0, -10.0 * std::log10 (num / den));
}

void ChromaEqProcessor::finishAnalysis()
{
    analyzing = false;
    std::vector<double> avg (acc.size());
    for (size_t k = 0; k < acc.size(); ++k) avg[k] = 10 * std::log10 (acc[k] / aCnt + 1e-14);
    const double binHz = getFs() / kAFft;
    const auto& I = kIntensity[(int) getPlain ("inten")];
    const auto r = chroma::planFromSpectrum (avg, binHz, I.th, I.cap);
    const bool modeB = getPlain ("mode") > 0.5f;
    const auto RED = juce::Colour::fromRGB (255, 42, 61), WHT = juce::Colour::fromRGB (244, 255, 244);
    auto cf = [](int i, double f) { return (float) juce::jlimit ((double) kNodes[i].fmin, (double) kNodes[i].fmax, f); };
    markers.clear();

    auto cut = [&](const chroma::Zone& z, int i, const juce::String& name) {
        if (z.present) {
            const float q = (float) (std::round (z.q * 100) / 100);
            setPlain ("f" + juce::String (i), cf (i, z.f)); setPlain ("g" + juce::String (i), (float) z.gain); setPlain ("q" + juce::String (i), q);
            markers.push_back ({ z.f, RED, name + " " + fmtF (z.f) });
            log ("> " + name + " at " + fmtF (z.f) + " (+" + juce::String (z.score, 1) + " dB over norm) -> cut " + juce::String (z.gain, 1) + " dB, Q " + juce::String (q, 2));
        } else { setPlain ("g" + juce::String (i), 0.f); log ("> No significant " + name.toLowerCase() + " found, left untouched"); }
    };
    cut (r.mud, 1, "MUD"); cut (r.harsh, 3, "HARSH");

    auto ton = [&](const chroma::Tone& t, int i, const juce::String& name, double bst) {
        if (t.found) markers.push_back ({ t.f, WHT, name + " " + fmtF (t.f) });
        if (modeB) {
            setPlain ("f" + juce::String (i), cf (i, t.f)); setPlain ("g" + juce::String (i), (float) bst);
            log ("> " + name + (t.found ? " peak" : " zone") + " at " + fmtF (cf (i, t.f)) + " -> boost +" + juce::String (bst, 1) + " dB");
        } else { setPlain ("g" + juce::String (i), 0.f); if (t.found) log ("> " + name + " character at " + fmtF (t.f) + " (preserved, Mode A)"); }
    };
    ton (r.warm, 0, "WARMTH", r.bWarm); ton (r.pres, 2, "PRESENCE", r.bPres); ton (r.air, 4, "AIR", r.bAir);

    setPlain ("wet", 1.f); setPlain ("out", 0.f);
    const float trim = (float) (std::round (matchLevel (avg, binHz) * 10) / 10);
    setPlain ("out", trim);
    statusText = "DONE";
    log ("[Profile done: mode " + juce::String (modeB ? "B" : "A") + ", intensity " + I.name + ", output trim " + juce::String (trim, 1) + " dB for level-matched A/B.]");
}

void ChromaEqProcessor::resetCurve()
{
    for (int i = 0; i < 5; ++i) {
        setPlain ("f" + juce::String (i), kNodes[i].f); setPlain ("g" + juce::String (i), 0.f); setPlain ("q" + juce::String (i), kNodes[i].q);
    }
    setPlain ("out", 0.f); setPlain ("wet", 1.f); markers.clear(); log ("[Curve reset to flat]");
}

void ChromaEqProcessor::getStateInformation (juce::MemoryBlock& d)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, d);
}
void ChromaEqProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType())) apvts.replaceState (juce::ValueTree::fromXml (*xml));
    irDirty = true;
}

juce::AudioProcessorEditor* ChromaEqProcessor::createEditor() { return new ChromaEqEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ChromaEqProcessor(); }
