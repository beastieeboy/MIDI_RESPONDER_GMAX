#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace {
    // Jedno połączenie z pamięcią współdzieloną na moduł wtyczki.
    sk::SharedLink& sharedLink()
    {
        static sk::SharedLink l;
        return l;
    }

    juce::StringArray progressionNames()
    {
        juce::StringArray s;
        for (const auto& p : sk::progressions)
            s.add(juce::String(p.minorKey ? "moll: " : "dur: ") + juce::String::fromUTF8(p.name));
        return s;
    }

    juce::StringArray keyNames()
    {
        juce::StringArray s;
        for (auto* n : sk::noteNames) s.add(n);
        return s;
    }

    int intParam(const juce::AudioProcessorValueTreeState& a, const char* id)
    {
        if (auto* p = a.getRawParameterValue(id)) return juce::roundToInt(p->load());
        return 0;
    }

    float floatParam(const juce::AudioProcessorValueTreeState& a, const char* id)
    {
        if (auto* p = a.getRawParameterValue(id)) return p->load();
        return 0.0f;
    }
}

SekcjaProcessor::SekcjaProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput(U("Wejście"), juce::AudioChannelSet::stereo(), true)
                         .withOutput(U("Wyjście"), juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "SekcjaKrzemowa", createLayout()),
      link(sharedLink())
{
    coreEngine = std::make_unique<sk::Engine>(isBass ? sk::Role::Bass : sk::Role::Piano, link);
    startTimerHz(5);
}

SekcjaProcessor::~SekcjaProcessor()
{
    stopTimer();
}

juce::AudioProcessorValueTreeState::ParameterLayout SekcjaProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto pct = AudioParameterFloatAttributes().withStringFromValueFunction(
        [](float v, int) { return String(roundToInt(v * 100.0f)) + " %"; });

    l.add(std::make_unique<AudioParameterInt>(ParameterID { "midiCh", 1 }, U("Kanał MIDI wyjścia"), 1, 16, 1));
    l.add(std::make_unique<AudioParameterChoice>(ParameterID { "group", 1 }, U("Zespół"),
                                                 StringArray { U("Zespół 1"), U("Zespół 2"), U("Zespół 3"), U("Zespół 4") }, 0));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID { "reactivity", 1 }, U("Reaktywność"), NormalisableRange<float>(0.0f, 1.0f), 0.6f, pct));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID { "variation", 1 }, "Wariacje", NormalisableRange<float>(0.0f, 1.0f), 0.4f, pct));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID { "dynamics", 1 }, "Dynamika", NormalisableRange<float>(0.0f, 1.0f), 0.7f, pct));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID { "feel", 1 }, U("Feel bębniarza"), NormalisableRange<float>(0.0f, 1.0f), 0.5f, pct));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID { "density", 1 }, U("Gęstość"), NormalisableRange<float>(-1.0f, 1.0f), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction(
            [](float v, int) { return (v > 0 ? "+" : "") + String(roundToInt(v * 100.0f)) + " %"; })));

    l.add(std::make_unique<AudioParameterChoice>(ParameterID { "drumMap", 1 }, "Mapa perkusji",
                                                 StringArray { U("Własne nuty / GM"), "GGD Modern & Massive 2" }, 0));
    l.add(std::make_unique<AudioParameterInt>(ParameterID { "kickNote", 1 }, "Nuta stopy", 0, 127, 36));
    l.add(std::make_unique<AudioParameterInt>(ParameterID { "snareNote", 1 }, "Nuta werbla", 0, 127, 38));
    l.add(std::make_unique<AudioParameterInt>(ParameterID { "tom1Note", 1 }, "Nuta tomu 1", 0, 127, 48));
    l.add(std::make_unique<AudioParameterInt>(ParameterID { "tom2Note", 1 }, "Nuta tomu 2", 0, 127, 45));
    l.add(std::make_unique<AudioParameterInt>(ParameterID { "hhNote", 1 }, "Nuta hi-hatu", 0, 127, 42));

    if (! isBass)
    {
        l.add(std::make_unique<AudioParameterChoice>(ParameterID { "key", 1 }, "Tonacja", keyNames(), 0));
        l.add(std::make_unique<AudioParameterChoice>(ParameterID { "progA", 1 }, "Progresja zwrotki", progressionNames(), 1));
        l.add(std::make_unique<AudioParameterChoice>(ParameterID { "progB", 1 }, "Progresja refrenu", progressionNames(), 0));
        l.add(std::make_unique<AudioParameterBool>(ParameterID { "fillSwitch", 1 }, U("Przejście zmienia sekcję"), true));
        l.add(std::make_unique<AudioParameterBool>(ParameterID { "giveWay", 1 }, U("Ustępuj basowi"), true));
        l.add(std::make_unique<AudioParameterInt>(ParameterID { "register", 1 }, "Rejestr", 52, 76, 64));
    }
    else
    {
        l.add(std::make_unique<AudioParameterInt>(ParameterID { "octave", 1 }, "Oktawa", -1, 1, 0));
        l.add(std::make_unique<AudioParameterFloat>(ParameterID { "kickLock", 1 }, U("Trzymaj stopę"), NormalisableRange<float>(0.0f, 1.0f), 0.6f, pct));
    }
    return l;
}

sk::EngineParams SekcjaProcessor::currentParams() const
{
    sk::EngineParams p;
    p.midiChannel = intParam(apvts, "midiCh");
    p.group = intParam(apvts, "group");
    p.reactivity = floatParam(apvts, "reactivity");
    p.variation = floatParam(apvts, "variation");
    p.dynamics = floatParam(apvts, "dynamics");
    p.feel = floatParam(apvts, "feel");
    p.density = floatParam(apvts, "density");
    p.drums.kick = intParam(apvts, "kickNote");
    p.drums.snare = intParam(apvts, "snareNote");
    p.drums.tom1 = intParam(apvts, "tom1Note");
    p.drums.tom2 = intParam(apvts, "tom2Note");
    p.drums.hihat = intParam(apvts, "hhNote");
    p.drums.preset = static_cast<sk::DrumPreset>(intParam(apvts, "drumMap"));
    if (! isBass)
    {
        p.harmony.key = intParam(apvts, "key");
        p.harmony.progA = intParam(apvts, "progA");
        p.harmony.progB = intParam(apvts, "progB");
        p.harmony.fillSwitchesSection = intParam(apvts, "fillSwitch") != 0;
        p.harmony.valid = true;
        p.giveWayToBass = intParam(apvts, "giveWay") != 0;
        p.registerCenter = intParam(apvts, "register");
    }
    else
    {
        p.octave = intParam(apvts, "octave");
        p.kickLock = floatParam(apvts, "kickLock");
    }
    return p;
}

void SekcjaProcessor::prepareToPlay(double sampleRate, int)
{
    sampleRateHz = sampleRate;
    coreEngine->prepare();
    setLatencySamples(0); // bez bufora wyprzedzenia: decyzje zapadają z pozycji transportu
}

bool SekcjaProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void SekcjaProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    // Audio przechodzi bez zmian (instrument może stać za wtyczką na tej samej ścieżce).
    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear(ch, 0, buffer.getNumSamples());

    const auto params = currentParams();
    sk::BlockInfo info;
    info.numSamples = buffer.getNumSamples();

    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            const double bpm = pos->getBpm().orFallback(120.0);
            const auto sig = pos->getTimeSignature().orFallback(juce::AudioPlayHead::TimeSignature {});
            info.playing = pos->getIsPlaying() && pos->getPpqPosition().hasValue();
            info.ppqStart = pos->getPpqPosition().orFallback(0.0);
            info.ppqPerSample = bpm / 60.0 / sampleRateHz;
            info.barLenPpq = juce::jmax(0.25, sig.numerator * 4.0 / juce::jmax(1, sig.denominator));
            info.stepsPerBar = juce::jlimit(1, 32, juce::roundToInt(info.barLenPpq * 4.0));
            info.barOrigin = pos->getPpqPositionOfLastBarStart().orFallback(0.0);
        }

    // Zbierz uderzenia bębniarza z wejścia MIDI (nuty wejściowe nie idą dalej).
    int numHits = 0;
    uint32_t flash = 0;
    const sk::DrumMap map = params.drums;
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOn() && numHits < (int) std::size(hitBuffer))
        {
            hitBuffer[numHits++] = { info.ppqStart + meta.samplePosition * info.ppqPerSample, m.getNoteNumber(), m.getVelocity() };
            flash |= 1u << int(map.classify(m.getNoteNumber()));
        }
    }
    if (flash) drumFlash.fetch_or(flash, std::memory_order_relaxed);
    midi.clear();

    outBuffer.count = 0;
    coreEngine->process(info, params, hitBuffer, numHits, outBuffer);
    for (int i = 0; i < outBuffer.count; ++i)
    {
        const auto& e = outBuffer.events[i];
        midi.addEvent(juce::MidiMessage(e.status, e.data1, e.data2), e.sampleOffset);
    }
}

void SekcjaProcessor::timerCallback()
{
    coreEngine->heartbeat(currentParams());
}

juce::AudioProcessorEditor* SekcjaProcessor::createEditor()
{
    return new SekcjaEditor(*this);
}

void SekcjaProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void SekcjaProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SekcjaProcessor();
}
