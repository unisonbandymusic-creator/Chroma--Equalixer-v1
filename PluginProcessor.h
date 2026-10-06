#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <array>
#include <functional>
#include "Analysis.h"

struct NodeDef { const char* name; const char* tag; int type; float f, g, q, fmin, fmax; };   // type: 0 low shelf, 1 peak, 2 high shelf
inline const NodeDef kNodes[5] = {
    { "WARMTH",   "TONAL", 0, 120.f,   0.f, 0.8f, 40.f,   300.f   },
    { "MUD",      "MUD",   1, 350.f,   0.f, 1.6f, 150.f,  600.f   },
    { "PRESENCE", "TONAL", 1, 1400.f,  0.f, 1.0f, 700.f,  2500.f  },
    { "HARSH",    "HARSH", 1, 3800.f,  0.f, 2.0f, 2000.f, 7000.f  },
    { "AIR",      "TONAL", 2, 10000.f, 0.f, 0.7f, 6000.f, 16000.f } };

struct Intensity { const char* name; double th, cap; };
inline const Intensity kIntensity[3] = { { "LOW", 1.8, 2 }, { "MID", 1.2, 3 }, { "HIGH", 0.8, 4 } };

inline juce::String fmtF (double f) { return f >= 1000 ? juce::String (f / 1000, 2) + " kHz" : juce::String ((int) std::round (f)) + " Hz"; }

class ChromaEqProcessor : public juce::AudioProcessor,
                          private juce::AudioProcessorValueTreeState::Listener,
                          private juce::Timer
{
public:
    static constexpr int kFirN = 8192, kRing = 65536, kAFft = 8192;
    using Coefs = juce::dsp::IIR::Coefficients<float>;
    struct Marker { double f; juce::Colour c; juce::String t; };

    ChromaEqProcessor();
    ~ChromaEqProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Chroma-Equalizer"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- used by the editor ----
    juce::AudioProcessorValueTreeState apvts;
    std::array<Coefs::Ptr, 5> makeCoefs (double fs, bool flat) const;
    void copyLatest (bool outputTap, float* dst, int n) const;
    double getFs() const { return currentFs > 0 ? currentFs : 44100.0; }
    void setPlain (const juce::String& id, float v);
    float getPlain (const juce::String& id) const;
    void startAnalysis();
    void cancelAnalysis();
    bool isAnalyzing() const { return analyzing; }
    float progress() const { return analyzing ? juce::jmin (1.0f, (float) aCnt / (float) aTarget) : 0.0f; }
    void resetCurve();

    std::vector<Marker> markers;                                            // message thread only
    juce::String statusText { "IDLE" };                                     // message thread only
    std::function<void (const juce::String&)> onLog;                        // message thread only

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void parameterChanged (const juce::String&, float) override { irDirty = true; }
    void timerCallback() override;
    juce::AudioBuffer<float> buildIR (double fs);
    void loadIR();
    void tap (const juce::AudioBuffer<float>&, std::array<float, kRing>&, std::atomic<int64_t>&);
    void pumpAnalysis();
    void finishAnalysis();
    double matchLevel (const std::vector<double>& avgDb, double binHz);
    void log (const juce::String& s) { if (onLog) onLog (s); }

    juce::dsp::Convolution conv { juce::dsp::Convolution::NonUniform { 512 } };
    juce::dsp::Gain<float> outGain;
    std::atomic<float>* outParam = nullptr;
    std::atomic<bool> irDirty { true };
    double currentFs = 0;

    std::array<float, kRing> inRing {}, outRing {};
    std::atomic<int64_t> inCount { 0 }, outCount { 0 };

    bool analyzing = false;
    int64_t aRead = 0; int aCnt = 0, aTarget = 1;
    std::vector<double> acc;
    juce::dsp::FFT aFft { 13 };
    std::vector<float> aBuf;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChromaEqProcessor)
};
