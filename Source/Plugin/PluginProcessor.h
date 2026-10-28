#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Engine.h"

// Polskie napisy w kodzie są w UTF-8.
#define U(s) juce::String(juce::CharPointer_UTF8(s))

#ifndef SK_ROLE_BASS
 #define SK_ROLE_BASS 0
#endif

class SekcjaProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    static constexpr bool isBass = SK_ROLE_BASS != 0;

    SekcjaProcessor();
    ~SekcjaProcessor() override;

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
    sk::Engine& engine() { return *coreEngine; }
    sk::EngineParams currentParams() const;

    // Ostatnie uderzenia perkusji (do wizualizacji w GUI), zapisywane z wątku audio.
    std::atomic<uint32_t> drumFlash { 0 };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void timerCallback() override;

    sk::SharedLink& link;
    std::unique_ptr<sk::Engine> coreEngine;
    sk::MidiOutBuffer outBuffer;
    sk::DrumHit hitBuffer[512];
    double sampleRateHz = 48000.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SekcjaProcessor)
};
