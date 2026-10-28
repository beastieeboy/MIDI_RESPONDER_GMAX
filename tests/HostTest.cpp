// Test w prawdziwym hoście JUCE: ładuje zbudowane VST3, gra bębny z "transportem"
// 120 BPM, sprawdza wyjście MIDI i zapisuje zrzut ekranu edytora.
// Użycie: sk_host_test <Piano.vst3> <Bass.vst3> <katalog_na_zrzuty>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <cstdio>

using namespace juce;

struct Transport : AudioPlayHead
{
    double ppq = 0.0;
    Optional<PositionInfo> getPosition() const override
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

static std::unique_ptr<AudioPluginInstance> load(AudioPluginFormatManager& fm, const String& path)
{
    OwnedArray<PluginDescription> found;
    for (auto* f : fm.getFormats())
        if (f->fileMightContainThisPluginType(path))
            f->findAllTypesForFile(found, path);
    if (found.isEmpty()) { std::printf("Nie znaleziono wtyczki: %s\n", path.toRawUTF8()); return {}; }
    String err;
    auto inst = fm.createPluginInstance(*found[0], 48000.0, 128, err);
    if (! inst) std::printf("Błąd ładowania: %s\n", err.toRawUTF8());
    return inst;
}

int main(int argc, char** argv)
{
    if (argc < 4) return 2;
    ScopedJuceInitialiser_GUI gui;
    AudioPluginFormatManager fm;
    fm.addFormat(new VST3PluginFormat());

    auto piano = load(fm, argv[1]);
    auto bass = load(fm, argv[2]);
    if (! piano || ! bass) return 1;

    Transport tp;
    int notes[2] = {};
    for (auto* p : { piano.get(), bass.get() })
    {
        p->setPlayHead(&tp);
        p->enableAllBuses();
        p->prepareToPlay(48000.0, 128);
    }
    // heartbeaty (timer 5 Hz) muszą zdążyć się wymienić
    for (int i = 0; i < 6; ++i) MessageManager::getInstance()->runDispatchLoopUntil(100);

    AudioBuffer<float> buf(2, 128);
    const double pps = 2.0 / 48000.0;
    const int bars = 8;
    std::vector<std::pair<double, int>> hits;
    for (int b = 0; b < bars; ++b)
    {
        for (int s = 0; s < 16; s += 2) hits.push_back({ b * 4 + s * 0.25, 42 });
        hits.push_back({ b * 4.0, 36 });
        hits.push_back({ b * 4 + 2.0, 36 });
        hits.push_back({ b * 4 + 1.0, 38 });
        hits.push_back({ b * 4 + 3.0, 38 });
    }
    std::sort(hits.begin(), hits.end());
    size_t hi = 0;
    for (int blk = 0; tp.ppq < bars * 4.0; ++blk)
    {
        MidiBuffer drums;
        const double end = tp.ppq + 128 * pps;
        while (hi < hits.size() && hits[hi].first < end)
        {
            drums.addEvent(MidiMessage::noteOn(10, hits[hi].second, (uint8) 100), jlimit(0, 127, int((hits[hi].first - tp.ppq) / pps)));
            ++hi;
        }
        for (int k = 0; k < 2; ++k)
        {
            MidiBuffer m(drums);
            buf.clear();
            (k == 0 ? piano : bass)->processBlock(buf, m);
            for (const auto meta : m)
                if (meta.getMessage().isNoteOn()) ++notes[k];
        }
        tp.ppq = end;
        if (blk % 50 == 0) MessageManager::getInstance()->runDispatchLoopUntil(1);
    }
    std::printf("Piano: %d nut, Bass: %d nut\n", notes[0], notes[1]);

    // zrzuty ekranu edytorów
    int idx = 0;
    for (auto* p : { piano.get(), bass.get() })
    {
        std::unique_ptr<AudioProcessorEditor> ed(p->createEditorIfNeeded());
        if (! ed) { std::printf("Brak edytora\n"); return 1; }
        ed->setVisible(true);
        auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 2.0f);
        File out = File(argv[3]).getChildFile(idx++ == 0 ? "gui-piano.png" : "gui-bass.png");
        out.deleteFile();
        FileOutputStream os(out);
        PNGImageFormat().writeImageToStream(img, os);
        p->editorBeingDeleted(ed.get());
    }
    for (auto* p : { piano.get(), bass.get() }) p->releaseResources();
    piano.reset();
    bass.reset();
    return (notes[0] > 20 && notes[1] > 10) ? 0 : 1;
}
