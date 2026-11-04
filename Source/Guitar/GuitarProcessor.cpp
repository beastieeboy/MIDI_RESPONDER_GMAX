#include "GuitarProcessor.h"
#include "GuitarEditor.h"

namespace {
    sk::SharedLink& sharedLink()
    {
        static sk::SharedLink l;
        return l;
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

GuitarProcessor::GuitarProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput(U("Wejście"), juce::AudioChannelSet::stereo(), true)
                         .withOutput(U("Wyjście"), juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "SekcjaKrzemowaGitara", createLayout()),
      link(sharedLink())
{
    coreEngine = std::make_unique<sk::GuitarEngine>(link);
    startTimerHz(5);
}

GuitarProcessor::~GuitarProcessor()
{
    stopTimer();
}

juce::AudioProcessorValueTreeState::ParameterLayout GuitarProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto pct = AudioParameterFloatAttributes().withStringFromValueFunction(
        [](float v, int) { return String(roundToInt(v * 100.0f)) + " %"; });
    auto note = [&](const char* id, const String& name, int def)
    {
        l.add(std::make_unique<AudioParameterInt>(ParameterID { id, 1 }, name, 0, 127, def));
    };

    l.add(std::make_unique<AudioParameterInt>(ParameterID { "midiCh", 1 }, U("Kanał MIDI wyjścia"), 1, 16, 1));
    l.add(std::make_unique<AudioParameterChoice>(ParameterID { "group", 1 }, U("Zespół"),
                                                 StringArray { U("Zespół 1"), U("Zespół 2"), U("Zespół 3"), U("Zespół 4") }, 0));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID { "reactivity", 1 }, U("Reaktywność"), NormalisableRange<float>(0.0f, 1.0f), 0.6f, pct));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID { "variation", 1 }, "Wariacje", NormalisableRange<float>(0.0f, 1.0f), 0.3f, pct));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID { "dynamics", 1 }, "Dynamika", NormalisableRange<float>(0.0f, 1.0f), 0.7f, pct));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID { "feel", 1 }, U("Feel bębniarza"), NormalisableRange<float>(0.0f, 1.0f), 0.5f, pct));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID { "density", 1 }, U("Gęstość"), NormalisableRange<float>(-1.0f, 1.0f), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction(
            [](float v, int) { return (v > 0 ? "+" : "") + String(roundToInt(v * 100.0f)) + " %"; })));

    l.add(std::make_unique<AudioParameterChoice>(ParameterID { "role", 1 }, "Rola gitary",
        StringArray { U("Hook: motyw i odpowiedź"), U("Wypełnienia (miejsce dla wokalu)"), "Solo" }, 0));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID { "ornaments", 1 }, U("Ozdobniki"), NormalisableRange<float>(0.0f, 1.0f), 0.55f, pct));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID { "space", 1 }, U("Miejsce dla Piano"), NormalisableRange<float>(0.0f, 1.0f), 0.6f, pct));
    l.add(std::make_unique<AudioParameterInt>(ParameterID { "register", 1 }, "Rejestr", 60, 84, 72));
    l.add(std::make_unique<AudioParameterInt>(ParameterID { "lowest", 1 }, U("Najniższa nuta"), 40, 72, 52));

    l.add(std::make_unique<AudioParameterChoice>(ParameterID { "profile", 1 }, "Instrument",
        StringArray { U("Session Guitarist Electric Neon (Melody)"), U("Ogólny: tylko nuty, bend i CC1") }, 0));
    note("ksOpen", "Keyswitch open", 36);
    note("ksMuted", "Keyswitch palm mute", 37);
    note("ksSlideIn", U("Keyswitch slajd do nuty"), 47);
    note("ksSlideDown", U("Keyswitch zjazd w dół"), 45);
    note("ksSlideUp", U("Keyswitch zjazd w górę"), 44);
    l.add(std::make_unique<AudioParameterInt>(ParameterID { "bendRange", 1 }, "Zakres pitch bend", 1, 12, 2));

    l.add(std::make_unique<AudioParameterChoice>(ParameterID { "drumMap", 1 }, "Mapa perkusji",
                                                 StringArray { U("Własne nuty / GM"), "GGD Modern & Massive 2" }, 0));
    note("kickNote", "Nuta stopy", 36);
    note("snareNote", "Nuta werbla", 38);
    note("tom1Note", "Nuta tomu 1", 48);
    note("tom2Note", "Nuta tomu 2", 45);
    note("hhNote", "Nuta hi-hatu", 42);
    return l;
}

sk::GuitarParams GuitarProcessor::currentParams() const
{
    sk::GuitarParams p;
    p.midiChannel = intParam(apvts, "midiCh");
    p.group = intParam(apvts, "group");
    p.reactivity = floatParam(apvts, "reactivity");
    p.variation = floatParam(apvts, "variation");
    p.dynamics = floatParam(apvts, "dynamics");
    p.feel = floatParam(apvts, "feel");
    p.density = floatParam(apvts, "density");
    p.role = intParam(apvts, "role");
    p.ornaments = floatParam(apvts, "ornaments");
    p.space = floatParam(apvts, "space");
    p.registerCenter = intParam(apvts, "register");
    p.lowest = intParam(apvts, "lowest");
    p.highest = p.registerCenter + 16;
    p.art.keyswitches = intParam(apvts, "profile") == 0;
    p.art.open = intParam(apvts, "ksOpen");
    p.art.muted = intParam(apvts, "ksMuted");
    p.art.slideIn = intParam(apvts, "ksSlideIn");
    p.art.slideDownEnd = intParam(apvts, "ksSlideDown");
    p.art.slideUpEnd = intParam(apvts, "ksSlideUp");
    p.art.bendRange = intParam(apvts, "bendRange");
    p.drums.kick = intParam(apvts, "kickNote");
    p.drums.snare = intParam(apvts, "snareNote");
    p.drums.tom1 = intParam(apvts, "tom1Note");
    p.drums.tom2 = intParam(apvts, "tom2Note");
    p.drums.hihat = intParam(apvts, "hhNote");
    p.drums.preset = static_cast<sk::DrumPreset>(intParam(apvts, "drumMap"));
    return p;
}

void GuitarProcessor::prepareToPlay(double sampleRate, int)
{
    sampleRateHz = sampleRate;
    coreEngine->prepare();
    setLatencySamples(0);
}

bool GuitarProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void GuitarProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
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

    int numHits = 0;
    uint32_t flash = 0;
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOn() && numHits < (int) std::size(hitBuffer))
        {
            hitBuffer[numHits++] = { info.ppqStart + meta.samplePosition * info.ppqPerSample, m.getNoteNumber(), m.getVelocity() };
            flash |= 1u << int(params.drums.classify(m.getNoteNumber()));
        }
    }
    if (flash) drumFlash.fetch_or(flash, std::memory_order_relaxed);
    midi.clear(); // nuty bębnów nie idą do Kontaktu

    outBuffer.count = 0;
    coreEngine->process(info, params, hitBuffer, numHits, outBuffer);
    for (int i = 0; i < outBuffer.count; ++i)
    {
        const auto& e = outBuffer.events[i];
        midi.addEvent(juce::MidiMessage(e.status, e.data1, e.data2), e.sampleOffset);
    }
}

void GuitarProcessor::timerCallback()
{
    coreEngine->heartbeat(currentParams());
}

juce::AudioProcessorEditor* GuitarProcessor::createEditor()
{
    return new GuitarEditor(*this);
}

void GuitarProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void GuitarProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GuitarProcessor();
}
