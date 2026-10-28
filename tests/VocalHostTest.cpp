// Test Wokalu w prawdziwym hoście JUCE: ładuje zbudowane VST3, wrzuca do pieca sample
// (przez stan wtyczki, jak przy otwieraniu projektu), gra bębny z "transportem" 120 BPM
// i sprawdza, że z wyjścia leci dźwięk tylko wtedy, gdy bębniarz gra.
// Użycie: sk_vocal_host_test <Wokal.vst3> <katalog_roboczy>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
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

// Sztuczny "wokal": dźwięk harmoniczny z wibratem (do testów nie trzeba prawdziwego głosu).
static File writeVocal(const File& dir, const String& name, double hz)
{
    const double sr = 44100.0;
    AudioBuffer<float> b(1, int(sr * 2.0));
    double ph = 0.0;
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const double f = hz * (1.0 + 0.01 * std::sin(2.0 * MathConstants<double>::pi * 5.0 * i / sr));
        ph += 2.0 * MathConstants<double>::pi * f / sr;
        double x = 0.0;
        for (int k = 1; k <= 10; ++k) x += std::sin(k * ph) / k;
        b.setSample(0, i, float(0.2 * x));
    }
    auto file = dir.getChildFile(name + ".wav");
    file.deleteFile();
    WavAudioFormat wav;
    std::unique_ptr<OutputStream> os(file.createOutputStream().release());
    if (auto* w = wav.createWriterFor(os.get(), sr, 1, 24, {}, 0))
    {
        os.release();
        std::unique_ptr<AudioFormatWriter> writer(w);
        writer->writeFromAudioSampleBuffer(b, 0, b.getNumSamples());
    }
    return file;
}

int main(int argc, char** argv)
{
    if (argc < 3) return 2;
    ScopedJuceInitialiser_GUI gui;
    AudioPluginFormatManager fm;
    fm.addFormat(new VST3PluginFormat());

    OwnedArray<PluginDescription> found;
    for (auto* f : fm.getFormats())
        if (f->fileMightContainThisPluginType(argv[1]))
            f->findAllTypesForFile(found, argv[1]);
    if (found.isEmpty()) { std::printf("Nie znaleziono wtyczki\n"); return 1; }
    String err;
    auto plug = fm.createPluginInstance(*found[0], 48000.0, 128, err);
    if (! plug) { std::printf("Błąd ładowania: %s\n", err.toRawUTF8()); return 1; }
    std::printf("Załadowano: %s (instrument: %s)\n", plug->getName().toRawUTF8(), found[0]->isInstrument ? "tak" : "nie");

    const File dir(argv[2]);
    dir.createDirectory();
    const auto a = writeVocal(dir, "wokal-a", 196.0), b = writeVocal(dir, "wokal-b", 262.0);

    // Stan z piecem: dwa sample (tak wtyczka odtwarza projekt REAPER-a).
    MemoryBlock state;
    plug->getStateInformation(state);
    auto xml = AudioProcessor::getXmlFromBinary(state.getData(), (int) state.getSize());
    if (! xml) { std::printf("Brak stanu\n"); return 1; }
    auto* furnace = xml->createNewChildElement("Furnace");
    for (auto& f : { a, b })
        furnace->createNewChildElement("Sample")->setAttribute("path", f.getFullPathName());
    MemoryBlock withFurnace;
    AudioProcessor::copyXmlToBinary(*xml, withFurnace);
    plug->setStateInformation(withFurnace.getData(), (int) withFurnace.getSize());
    for (int i = 0; i < 30; ++i) MessageManager::getInstance()->runDispatchLoopUntil(100); // wczytywanie w tle

    Transport tp;
    plug->setPlayHead(&tp);
    plug->enableAllBuses();
    plug->prepareToPlay(48000.0, 128);

    AudioBuffer<float> buf(jmax(2, plug->getTotalNumOutputChannels()), 128);
    const double pps = 2.0 / 48000.0;
    double energyPlaying = 0.0, energySilent = 0.0;
    bool finite = true;
    const int bars = 11; // 6 taktów gry, potem cisza
    for (int blk = 0; tp.ppq < bars * 4.0; ++blk)
    {
        MidiBuffer midi;
        const double end = tp.ppq + 128 * pps;
        if (tp.ppq < 24.0)
            for (double q = std::ceil(tp.ppq / 0.5) * 0.5; q < end; q += 0.5)
            {
                const int off = jlimit(0, 127, int((q - tp.ppq) / pps));
                midi.addEvent(MidiMessage::noteOn(10, 42, (uint8) 90), off);
                if (std::fmod(q, 2.0) == 0.0) midi.addEvent(MidiMessage::noteOn(10, 36, (uint8) 110), off);
                if (std::fmod(q, 2.0) == 1.0) midi.addEvent(MidiMessage::noteOn(10, 38, (uint8) 100), off);
            }
        buf.clear();
        plug->processBlock(buf, midi);
        double e = 0.0;
        for (int c = 0; c < buf.getNumChannels(); ++c)
            for (int i = 0; i < buf.getNumSamples(); ++i)
            {
                const float v = buf.getSample(c, i);
                finite = finite && std::isfinite(v) && std::abs(v) <= 1.0f;
                e += double(v) * v;
            }
        (tp.ppq < 24.0 ? energyPlaying : (tp.ppq > 36.0 ? energySilent : e)) += e;
        tp.ppq = end;
        if (blk % 50 == 0) MessageManager::getInstance()->runDispatchLoopUntil(1);
    }
    std::printf("energia gdy bębniarz gra: %.3f, w ciszy: %.6f\n", energyPlaying, energySilent);

    // zapisany stan pamięta piec
    MemoryBlock after;
    plug->getStateInformation(after);
    auto xml2 = AudioProcessor::getXmlFromBinary(after.getData(), (int) after.getSize());
    int saved = 0;
    if (xml2)
        if (auto* f = xml2->getChildByName("Furnace"))
            saved = f->getNumChildElements();
    std::printf("sample w zapisanym stanie: %d\n", saved);

    if (auto* ed = plug->createEditorIfNeeded())
    {
        for (int i = 0; i < 5; ++i) MessageManager::getInstance()->runDispatchLoopUntil(100);
        auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f);
        auto out = dir.getChildFile("wokal-host.png");
        out.deleteFile();
        FileOutputStream os(out);
        PNGImageFormat().writeImageToStream(img, os);
        delete ed;
    }
    plug->releaseResources();
    plug.reset();

    const bool ok = finite && energyPlaying > 1.0 && energySilent < 1e-4 * energyPlaying && saved == 2;
    std::printf(ok ? "OK\n" : "BŁĄD\n");
    return ok ? 0 : 1;
}
