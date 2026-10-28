#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "VocalEngine.h"

// Polskie napisy w kodzie są w UTF-8.
#define U(s) juce::String(juce::CharPointer_UTF8(s))

// Sekcja Krzemowa Wokal: syntezator granularny, który gra wyłącznie z sampli wokalu
// wrzuconych do "pieca". Słucha bębnów (MIDI) i harmonii Piano z linku.
class VocalProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    VocalProcessor();
    ~VocalProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 1.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // --- piec (wątek wiadomości) ---
    static bool isAudioFile(const juce::File&);
    static juce::String audioWildcard() { return "*.wav;*.wave;*.aif;*.aiff;*.flac;*.mp3;*.m4a;*.ogg;*.caf"; }
    void addFiles(const juce::StringArray& paths);        // pliki albo foldery
    void removeSample(int index);
    void clearPool();
    void setFurnaceFolder(const juce::File& folder);
    juce::File getFurnaceFolder() const { return furnaceFolder; }
    sk::SamplePool& samplePool() { return pool; }
    int pendingLoads() const { return loading.load(); }
    int poolVersion() const { return version; }            // rośnie przy każdej zmianie pieca
    juce::String lastError() const { return loadError; }

    juce::AudioProcessorValueTreeState apvts;
    sk::VocalEngine& engine() { return vocal; }
    sk::VocalParams currentParams() const;

    std::atomic<uint32_t> drumFlash { 0 };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void timerCallback() override;
    void queueLoad(const juce::File& file, float weight);
    void scanFurnaceFolder();

    sk::SharedLink& link;
    sk::SamplePool pool;
    sk::VocalEngine vocal;
    double sampleRateHz = 48000.0;
    std::vector<float> scratchR;
    sk::VocalHit hitBuffer[512];

    // wczytywanie w tle
    juce::ThreadPool loader { 1 };
    juce::CriticalSection doneLock;
    std::vector<std::pair<std::shared_ptr<sk::VocalSample>, float>> done;
    std::atomic<int> loading { 0 };
    juce::StringArray known;          // ścieżki w piecu lub w trakcie wczytywania
    juce::File furnaceFolder;
    juce::StringArray seenInFolder;   // "ścieżka|czas modyfikacji" plików już obejrzanych w folderze pieca
    juce::int64 folderBaseline = 0;   // pliki starsze niż zapis projektu nie wracają do pieca
    int folderTick = 0, version = 0;
    juce::String loadError;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VocalProcessor)
};
