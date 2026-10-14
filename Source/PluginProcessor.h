#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <vector>
#include "Dsp.h"

struct ReadingInfo
{
    ds::Reading r;
    int   source = 0;          // 0 nothing yet, 1 live capture, 2 file
    bool  analyzing = false;
    float heardSeconds = 0.f;  // live audio heard so far
    bool  signalPresent = false;
    int   octave = 0;          // 0 as detected, 1 half, 2 double
    juce::String fileName;
};

class KeyProcessor : public juce::AudioProcessor
{
public:
    KeyProcessor();
    ~KeyProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Disco Sauce Key Finder"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

    // ---- editor API
    bool loadFile (const juce::File& f);
    void resetReading();
    void cycleOctave();
    ReadingInfo getInfo() const;
    float getLevel() const { return level.load(); }

private:
    class Analyzer : public juce::Thread
    {
    public:
        explicit Analyzer (KeyProcessor& o) : juce::Thread ("DiscoSauceAnalyzer"), owner (o) {}
        void run() override { owner.analyzerLoop(); }
        KeyProcessor& owner;
    };
    void analyzerLoop();
    void publish (const ds::Reading& r, int source);

    // live capture (mono ring buffer, last 120 s of non-silent audio)
    juce::CriticalSection ringLock;
    std::vector<float> ring;
    int ringSize = 0;
    std::atomic<juce::int64> total { 0 };
    std::atomic<int> resetCounter { 0 };
    std::atomic<double> capRate { 44100.0 };
    std::atomic<float> level { 0.f };
    juce::int64 silentSamples = 0;
    bool wasPlaying = false;

    // file analysis request
    std::vector<float> pendingFile;
    double pendingRate = 44100.0;
    juce::String pendingName;
    std::atomic<bool> fileRequest { false };

    mutable juce::CriticalSection lock;
    ds::Reading reading;
    int source = 0;
    bool analyzing = false;
    int octave = 0;
    juce::String fileName;

    std::unique_ptr<Analyzer> analyzer;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KeyProcessor)
};
