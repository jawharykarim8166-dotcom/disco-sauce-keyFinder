#include "PluginProcessor.h"
#include "PluginEditor.h"

KeyProcessor::KeyProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    analyzer.reset (new Analyzer (*this));
    analyzer->startThread();
}

KeyProcessor::~KeyProcessor()
{
    if (analyzer != nullptr)
    {
        analyzer->signalThreadShouldExit();
        analyzer->notify();
        analyzer->stopThread (10000);
    }
}

juce::AudioProcessorEditor* KeyProcessor::createEditor() { return new KeyEditor (*this); }

bool KeyProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        && l.getMainInputChannelSet()  == juce::AudioChannelSet::stereo();
}

void KeyProcessor::prepareToPlay (double sampleRate, int)
{
    const juce::ScopedLock sl (ringLock);
    capRate.store (sampleRate);
    ringSize = (int) (sampleRate * 120.0);
    ring.assign ((size_t) ringSize, 0.f);
    total.store (0);
    resetCounter.fetch_add (1);
}

void KeyProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int nch = buffer.getNumChannels();
    if (nch < 1 || n < 1) return;

    // input meter
    const float peak = buffer.getMagnitude (0, n);
    level.store (juce::jmax (peak, level.load() * 0.93f));

    if (ringSize == 0) return;

    bool playing = true;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            playing = pos->getIsPlaying();

    if (playing && ! wasPlaying)
    {
        total.store (0);                      // new playback pass -> start listening again
        resetCounter.fetch_add (1);
        silentSamples = 0;
    }
    wasPlaying = playing;
    if (! playing) return;

    const double sr = capRate.load();
    if (peak < 0.0005f)                       // silence: do not store it
    {
        silentSamples += n;
        if ((double) silentSamples > 2.5 * sr && total.load() > 0)
        {
            total.store (0);                  // long gap -> next sound is a new piece
            resetCounter.fetch_add (1);
        }
        return;
    }
    silentSamples = 0;

    juce::int64 t = total.load();
    int idx = (int) (t % (juce::int64) ringSize);
    const float* l = buffer.getReadPointer (0);
    const float* r = buffer.getReadPointer (nch > 1 ? 1 : 0);
    for (int i = 0; i < n; ++i)
    {
        ring[(size_t) idx] = 0.5f * (l[i] + r[i]);
        if (++idx >= ringSize) idx = 0;
    }
    total.store (t + n);
    // audio passes through untouched
}

//==============================================================================
ReadingInfo KeyProcessor::getInfo() const
{
    ReadingInfo i;
    {
        const juce::ScopedLock sl (lock);
        i.r = reading; i.source = source; i.analyzing = analyzing; i.octave = octave; i.fileName = fileName;
    }
    const double sr = capRate.load();
    i.heardSeconds = (float) juce::jmin (1.0e6, (double) total.load() / (sr > 0.0 ? sr : 44100.0));
    i.signalPresent = level.load() > 0.002f;
    return i;
}

void KeyProcessor::cycleOctave()
{
    const juce::ScopedLock sl (lock);
    octave = (octave + 1) % 3;
}

void KeyProcessor::resetReading()
{
    {
        const juce::ScopedLock sl (lock);
        reading = ds::Reading(); source = 0; analyzing = false; octave = 0; fileName.clear();
    }
    total.store (0);
    resetCounter.fetch_add (1);
    if (analyzer != nullptr) analyzer->notify();
}

bool KeyProcessor::loadFile (const juce::File& f)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (f));
    if (reader == nullptr || reader->lengthInSamples < 1000 || reader->sampleRate < 8000.0) return false;

    const juce::int64 want = (juce::int64) (reader->sampleRate * 180.0);              // analyse up to 3 minutes (middle of the song)
    const juce::int64 num = juce::jmin (want, reader->lengthInSamples);
    const juce::int64 start = juce::jmax ((juce::int64) 0, (reader->lengthInSamples - num) / 2);

    juce::AudioBuffer<float> buf (2, (int) num);
    reader->read (&buf, 0, (int) num, start, true, true);
    std::vector<float> mono ((size_t) num);
    const bool stereo = reader->numChannels > 1;
    for (int i = 0; i < (int) num; ++i)
        mono[(size_t) i] = stereo ? 0.5f * (buf.getSample (0, i) + buf.getSample (1, i)) : buf.getSample (0, i);

    {
        const juce::ScopedLock sl (lock);
        pendingFile.swap (mono);
        pendingRate = reader->sampleRate;
        pendingName = f.getFileName();
        analyzing = true; octave = 0;
    }
    fileRequest.store (true);
    if (analyzer != nullptr) analyzer->notify();
    return true;
}

void KeyProcessor::publish (const ds::Reading& r, int src)
{
    const juce::ScopedLock sl (lock);
    reading = r; source = src; analyzing = false;
    if (src == 1) fileName.clear();
}

//==============================================================================
void KeyProcessor::analyzerLoop()
{
    auto stop = [] { return juce::Thread::currentThreadShouldExit(); };
    juce::int64 lastTotal = 0;
    int seenReset = resetCounter.load();
    bool haveLive = false;

    while (! juce::Thread::currentThreadShouldExit())
    {
        analyzer->wait (600);
        if (juce::Thread::currentThreadShouldExit()) break;

        // ---- a dropped file
        if (fileRequest.exchange (false))
        {
            std::vector<float> x; double sr; juce::String nm;
            {
                const juce::ScopedLock sl (lock);
                x.swap (pendingFile); sr = pendingRate; nm = pendingName;
            }
            const auto r = ds::analyze (x, sr, stop);
            {
                const juce::ScopedLock sl (lock);
                reading = r; source = 2; analyzing = false; fileName = nm;
            }
            haveLive = false;
            continue;
        }

        // ---- live capture
        const int rc = resetCounter.load();
        if (rc != seenReset)
        {
            seenReset = rc; lastTotal = 0; haveLive = false;
            const juce::ScopedLock sl (lock);
            if (source == 1) { reading = ds::Reading(); analyzing = false; octave = 0; }
        }

        const double sr = capRate.load();
        const juce::int64 t = total.load();
        if ((double) t < 8.0 * sr) continue;                                   // need 8 s before the first reading
        if (haveLive && (double) (t - lastTotal) < 4.0 * sr) continue;         // refine every 4 s of new audio

        std::vector<float> x;
        {
            const juce::ScopedLock rl (ringLock);
            if (ringSize == 0) continue;
            const int n = (int) juce::jmin ((juce::int64) ringSize, t);
            x.resize ((size_t) n);
            int idx = (int) ((t - n) % (juce::int64) ringSize);
            for (int i = 0; i < n; ++i)
            {
                x[(size_t) i] = ring[(size_t) idx];
                if (++idx >= ringSize) idx = 0;
            }
        }
        { const juce::ScopedLock sl (lock); analyzing = true; }
        const auto r = ds::analyze (x, sr, stop);
        if (juce::Thread::currentThreadShouldExit()) break;
        if (resetCounter.load() != seenReset) { const juce::ScopedLock sl (lock); analyzing = false; continue; }
        lastTotal = t; haveLive = true;
        publish (r, 1);
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new KeyProcessor(); }
