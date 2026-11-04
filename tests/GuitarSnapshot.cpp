// Renderuje edytor wtyczki Gitara do PNG bez hosta (podgląd GUI w CI / bez macOS).
// Przed zrzutem gra kilka taktów z bębniarzem, żeby status i gryf nie były puste.
#include "../Source/Guitar/GuitarProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>

struct FakeTransport : juce::AudioPlayHead
{
    double ppq = 0.0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setIsPlaying(true);
        p.setBpm(120.0);
        p.setTimeSignature(TimeSignature { 4, 4 });
        p.setPpqPosition(ppq);
        p.setPpqPositionOfLastBarStart(std::floor(ppq / 4.0) * 4.0);
        return p;
    }
};

int main(int argc, char** argv)
{
    if (argc < 2) return 2;
    juce::ScopedJuceInitialiser_GUI gui;
    std::unique_ptr<juce::AudioProcessor> proc(createPluginFilter());
    FakeTransport tp;
    proc->setPlayHead(&tp);
    proc->prepareToPlay(48000.0, 128);
    juce::AudioBuffer<float> buf(2, 128);
    const double pps = 2.0 / 48000.0;
    while (tp.ppq < 4 * 7.0)
    {
        juce::MidiBuffer m;
        const double end = tp.ppq + 128 * pps;
        for (double s = std::ceil(tp.ppq * 2.0) / 2.0; s < end; s += 0.5)
        {
            const int off = juce::jlimit(0, 127, int((s - tp.ppq) / pps));
            m.addEvent(juce::MidiMessage::noteOn(10, 42, (juce::uint8) 90), off);
            if (std::fmod(s, 2.0) == 0.0) m.addEvent(juce::MidiMessage::noteOn(10, 36, (juce::uint8) 110), off);
            if (std::fmod(s, 2.0) == 1.0) m.addEvent(juce::MidiMessage::noteOn(10, 38, (juce::uint8) 110), off);
        }
        proc->processBlock(buf, m);
        tp.ppq = end;
    }
    std::unique_ptr<juce::AudioProcessorEditor> ed(proc->createEditor());
    for (int i = 0; i < 5; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
    auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, argc > 2 ? juce::String(argv[2]).getFloatValue() : 1.5f);
    juce::File out(argv[1]);
    out.deleteFile();
    juce::FileOutputStream os(out);
    juce::PNGImageFormat().writeImageToStream(img, os);
    ed.reset();
    return 0;
}
