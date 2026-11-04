#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "GuitarEngine.h"

#ifndef U
 #define U(s) juce::String(juce::CharPointer_UTF8(s))
#endif

// Sekcja Krzemowa Gitara: wtyczka MIDI przed Kontaktem (Session Guitarist
// Electric Neon w trybie Melody albo dowolna gitara). Słucha bębnów z wejścia
// MIDI oraz Piano i Bass przez link (pamięć współdzieloną).
class GuitarProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    GuitarProcessor();
    ~GuitarProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    sk::GuitarEngine& engine() { return *coreEngine; }
    sk::GuitarParams currentParams() const;

    std::atomic<uint32_t> drumFlash { 0 };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void timerCallback() override;

    sk::SharedLink& link;
    std::unique_ptr<sk::GuitarEngine> coreEngine;
    sk::MidiOutBuffer outBuffer;
    sk::DrumHit hitBuffer[512];
    double sampleRateHz = 48000.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GuitarProcessor)
};
