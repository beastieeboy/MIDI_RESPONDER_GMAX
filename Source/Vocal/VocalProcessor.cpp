#include "VocalProcessor.h"
#include "VocalEditor.h"

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

    constexpr double maxSampleSeconds = 90.0;
}

VocalProcessor::VocalProcessor()
    : AudioProcessor(BusesProperties().withOutput(U("Wyjście"), juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "SekcjaKrzemowaWokal", createLayout()),
      link(sharedLink()),
      vocal(link)
{
    startTimerHz(10);
}

VocalProcessor::~VocalProcessor()
{
    stopTimer();
    loader.removeAllJobs(true, 10000);
}

juce::AudioProcessorValueTreeState::ParameterLayout VocalProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto pct = AudioParameterFloatAttributes().withStringFromValueFunction(
        [](float v, int) { return String(roundToInt(v * 100.0f)) + " %"; });
    auto f01 = [&](const char* id, const String& name, float def)
    {
        l.add(std::make_unique<AudioParameterFloat>(ParameterID { id, 1 }, name, NormalisableRange<float>(0.0f, 1.0f), def, pct));
    };

    l.add(std::make_unique<AudioParameterChoice>(ParameterID { "group", 1 }, U("Zespół"),
                                                 StringArray { U("Zespół 1"), U("Zespół 2"), U("Zespół 3"), U("Zespół 4") }, 0));

    l.add(std::make_unique<AudioParameterChoice>(ParameterID { "mode", 1 }, "Tryb",
                                                 StringArray { U("Chór (stałe głosy akordu)"), U("Chmura (rój ziaren)") }, 0));
    l.add(std::make_unique<AudioParameterChoice>(ParameterID { "sync", 1 }, "Siatka chmury",
                                                 StringArray { "Swobodnie", U("Ósemki"), "Szesnastki" }, 1));

    // ziarna
    NormalisableRange<float> ms(10.0f, 1000.0f);
    ms.setSkewForCentre(150.0f);
    l.add(std::make_unique<AudioParameterFloat>(ParameterID { "grainMs", 1 }, "Ziarno", ms, 300.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction([](float v, int) { return String(roundToInt(v)) + " ms"; })));
    NormalisableRange<float> dens(1.0f, 80.0f);
    dens.setSkewForCentre(14.0f);
    l.add(std::make_unique<AudioParameterFloat>(ParameterID { "density", 1 }, U("Gęstość"), dens, 8.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction([](float v, int) { return String(v, 1) + " /s"; })));
    f01("position", "Pozycja", 0.3f);
    f01("scatter", "Rozrzut", 0.15f);
    l.add(std::make_unique<AudioParameterFloat>(ParameterID { "scan", 1 }, "Skan", NormalisableRange<float>(-1.0f, 1.0f), 0.15f,
        AudioParameterFloatAttributes().withStringFromValueFunction(
            [](float v, int) { return (v > 0 ? "+" : "") + String(roundToInt(v * 100.0f)) + " %"; })));
    f01("reverse", "Wstecz", 0.0f);
    f01("shape", U("Kształt"), 0.0f);
    f01("width", U("Szerokość"), 0.6f);

    // strój i mieszanie
    f01("retune", "Strojenie", 1.0f);
    l.add(std::make_unique<AudioParameterInt>(ParameterID { "octave", 1 }, "Oktawa", -2, 2, 0));
    f01("spread", U("Rozpiętość"), 0.0f);
    f01("voiced", U("Samogłoski"), 0.9f);
    f01("freshness", U("Świeżość"), 0.3f);
    l.add(std::make_unique<AudioParameterFloat>(ParameterID { "gain", 1 }, U("Głośność"), NormalisableRange<float>(-36.0f, 12.0f), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction([](float v, int) { return String(v, 1) + " dB"; })));

    // reakcja na bębny
    f01("hatDensity", U("Hi-hat → gęstość"), 0.3f);
    f01("swell", U("Tomy → puchnięcie"), 0.6f);
    f01("pulse", U("Puls bębnów"), 0.1f);
    f01("dynamics", "Dynamika", 0.6f);
    f01("reactivity", U("Reaktywność"), 0.6f);
    l.add(std::make_unique<AudioParameterBool>(ParameterID { "playAlways", 1 }, U("Graj bez bębniarza"), false));

    // własna harmonia (gdy w zespole nie ma Piano)
    l.add(std::make_unique<AudioParameterChoice>(ParameterID { "key", 1 }, "Tonacja", keyNames(), 0));
    l.add(std::make_unique<AudioParameterChoice>(ParameterID { "progA", 1 }, "Progresja zwrotki", progressionNames(), 1));
    l.add(std::make_unique<AudioParameterChoice>(ParameterID { "progB", 1 }, "Progresja refrenu", progressionNames(), 0));
    l.add(std::make_unique<AudioParameterBool>(ParameterID { "fillSwitch", 1 }, U("Przejście zmienia sekcję"), true));

    // bębny
    l.add(std::make_unique<AudioParameterChoice>(ParameterID { "drumMap", 1 }, "Mapa perkusji",
                                                 StringArray { U("Własne nuty / GM"), "GGD Modern & Massive 2" }, 0));
    l.add(std::make_unique<AudioParameterInt>(ParameterID { "kickNote", 1 }, "Nuta stopy", 0, 127, 36));
    l.add(std::make_unique<AudioParameterInt>(ParameterID { "snareNote", 1 }, "Nuta werbla", 0, 127, 38));
    l.add(std::make_unique<AudioParameterInt>(ParameterID { "tom1Note", 1 }, "Nuta tomu 1", 0, 127, 48));
    l.add(std::make_unique<AudioParameterInt>(ParameterID { "tom2Note", 1 }, "Nuta tomu 2", 0, 127, 45));
    l.add(std::make_unique<AudioParameterInt>(ParameterID { "hhNote", 1 }, "Nuta hi-hatu", 0, 127, 42));
    return l;
}

sk::VocalParams VocalProcessor::currentParams() const
{
    sk::VocalParams p;
    p.group = intParam(apvts, "group");
    p.mode = intParam(apvts, "mode");
    p.sync = intParam(apvts, "sync");
    p.grainMs = floatParam(apvts, "grainMs");
    p.density = floatParam(apvts, "density");
    p.position = floatParam(apvts, "position");
    p.scatter = floatParam(apvts, "scatter");
    p.scan = floatParam(apvts, "scan");
    p.reverse = floatParam(apvts, "reverse");
    p.shape = floatParam(apvts, "shape");
    p.width = floatParam(apvts, "width");
    p.retune = floatParam(apvts, "retune");
    p.octave = intParam(apvts, "octave");
    p.spread = floatParam(apvts, "spread");
    p.voiced = floatParam(apvts, "voiced");
    p.freshness = floatParam(apvts, "freshness");
    p.gainDb = floatParam(apvts, "gain");
    p.hatToDensity = floatParam(apvts, "hatDensity");
    p.swell = floatParam(apvts, "swell");
    p.pulse = floatParam(apvts, "pulse");
    p.dynamics = floatParam(apvts, "dynamics");
    p.reactivity = floatParam(apvts, "reactivity");
    p.playWithoutDrums = intParam(apvts, "playAlways") != 0;
    p.harmony.key = intParam(apvts, "key");
    p.harmony.progA = intParam(apvts, "progA");
    p.harmony.progB = intParam(apvts, "progB");
    p.harmony.fillSwitchesSection = intParam(apvts, "fillSwitch") != 0;
    p.harmony.valid = true;
    p.drums.kick = intParam(apvts, "kickNote");
    p.drums.snare = intParam(apvts, "snareNote");
    p.drums.tom1 = intParam(apvts, "tom1Note");
    p.drums.tom2 = intParam(apvts, "tom2Note");
    p.drums.hihat = intParam(apvts, "hhNote");
    p.drums.preset = static_cast<sk::DrumPreset>(intParam(apvts, "drumMap"));
    return p;
}

void VocalProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    sampleRateHz = sampleRate;
    scratchR.assign(size_t(juce::jmax(512, samplesPerBlock)), 0.0f);
    vocal.prepare(sampleRate);
    setLatencySamples(0);
}

bool VocalProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void VocalProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    const auto params = currentParams();

    sk::BlockInfo info;
    info.numSamples = numSamples;
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
    if (info.ppqPerSample <= 0.0)
        info.ppqPerSample = 2.0 / sampleRateHz;

    int numHits = 0;
    uint32_t flash = 0;
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOn() && numHits < (int) std::size(hitBuffer))
        {
            hitBuffer[numHits++] = { meta.samplePosition, info.ppqStart + meta.samplePosition * info.ppqPerSample,
                                     m.getNoteNumber(), m.getVelocity() };
            flash |= 1u << int(params.drums.classify(m.getNoteNumber()));
        }
    }
    if (flash) drumFlash.fetch_or(flash, std::memory_order_relaxed);
    midi.clear();

    const auto* snapshot = pool.acquire();
    float* L = buffer.getWritePointer(0);
    float* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;
    const int chunk = (int) scratchR.size();

    // Host może podać większy blok niż w prepareToPlay – dzielimy go na kawałki.
    int hi = 0;
    for (int start = 0; start < numSamples; start += chunk)
    {
        const int n = juce::jmin(chunk, numSamples - start);
        sk::BlockInfo part = info;
        part.numSamples = n;
        part.ppqStart = info.ppqStart + start * info.ppqPerSample;
        int first = hi;
        while (hi < numHits && hitBuffer[hi].offset < start + n)
            hitBuffer[hi++].offset -= start;
        vocal.process(part, params, hitBuffer + first, hi - first, snapshot, L + start, R ? R + start : scratchR.data());
        if (R == nullptr)
            for (int i = 0; i < n; ++i)
                L[start + i] = 0.5f * (L[start + i] + scratchR[size_t(i)]);
    }
    pool.reportInUse(vocal.oldestGenerationInUse());
    for (int ch = 2; ch < buffer.getNumChannels(); ++ch)
        buffer.clear(ch, 0, numSamples);
}

// ---------------------------------------------------------------- piec

bool VocalProcessor::isAudioFile(const juce::File& f)
{
    return f.existsAsFile() && f.hasFileExtension("wav;wave;aif;aiff;flac;mp3;m4a;ogg;caf") && ! f.getFileName().startsWith(".");
}

void VocalProcessor::addFiles(const juce::StringArray& paths)
{
    for (const auto& path : paths)
    {
        const juce::File f(path);
        if (f.isDirectory())
        {
            auto files = f.findChildFiles(juce::File::findFiles, true, audioWildcard());
            files.sort();
            for (const auto& c : files)
                if (isAudioFile(c))
                    queueLoad(c, 1.0f);
        }
        else if (isAudioFile(f))
        {
            queueLoad(f, 1.0f);
        }
    }
}

void VocalProcessor::queueLoad(const juce::File& file, float weight)
{
    const auto path = file.getFullPathName();
    {
        const juce::ScopedLock sl(poolLock);
        if (known.contains(path))
            return;
        known.add(path);
    }
    ++loading;
    loader.addJob([this, file, weight]
    {
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::shared_ptr<sk::VocalSample> sample;
        if (std::unique_ptr<juce::AudioFormatReader> reader { fm.createReaderFor(file) })
        {
            const auto len = (int) juce::jmin<juce::int64>(reader->lengthInSamples, juce::int64(reader->sampleRate * maxSampleSeconds));
            const int chans = (int) juce::jlimit(1u, 2u, reader->numChannels);
            if (len > 0 && reader->sampleRate > 0.0)
            {
                juce::AudioBuffer<float> buf(chans, len);
                reader->read(&buf, 0, len, 0, true, chans > 1);
                std::vector<float> mono((size_t) len);
                for (int i = 0; i < len; ++i)
                {
                    float v = 0.0f;
                    for (int c = 0; c < chans; ++c) v += buf.getSample(c, i);
                    mono[size_t(i)] = v / float(chans);
                }
                sample = sk::makeVocalSample(std::move(mono), reader->sampleRate,
                                             file.getFileNameWithoutExtension().toStdString(),
                                             file.getFullPathName().toStdString());
                sample->weight = weight;
            }
        }
        // Prosto do pieca z wątku wczytywania: nie czekamy na pętlę wiadomości hosta
        // (część hostów nie obsługuje timerów wtyczki, dopóki okno jest zamknięte).
        const juce::ScopedLock sl(poolLock);
        if (sample)
        {
            if (pool.size() >= sk::PoolSnapshot::maxSamples)   // najstarszy sampel się spala
                known.removeString(juce::String::fromUTF8(pool.at(0)->path.c_str()));
            pool.add(std::move(sample));
            loadError = {};
        }
        else
        {
            known.removeString(file.getFullPathName());
            loadError = U("Nie udało się wczytać: ") + file.getFileName();
        }
        ++version;
        --loading;
    });
}

void VocalProcessor::removeSample(int index)
{
    const juce::ScopedLock sl(poolLock);
    if (index < 0 || index >= pool.size())
        return;
    known.removeString(juce::String::fromUTF8(pool.at(index)->path.c_str()));
    pool.remove(index);
    ++version;
}

void VocalProcessor::clearPool()
{
    const juce::ScopedLock sl(poolLock);
    for (int i = 0; i < pool.size(); ++i)
        known.removeString(juce::String::fromUTF8(pool.at(i)->path.c_str()));
    pool.clear();
    ++version;
}

void VocalProcessor::setFurnaceFolder(const juce::File& folder)
{
    furnaceFolder = folder;
    folderTick = 0;
    folderBaseline = 0;   // nowy folder: do pieca trafia wszystko, co już w nim leży
    seenInFolder.clear();
    ++version;
    if (folder.isDirectory())
        scanFurnaceFolder();
}

void VocalProcessor::scanFurnaceFolder()
{
    if (! furnaceFolder.isDirectory())
        return;
    auto files = furnaceFolder.findChildFiles(juce::File::findFiles, true, audioWildcard());
    // najstarsze najpierw, żeby świeże sample były na końcu pieca
    std::sort(files.begin(), files.end(), [](const juce::File& a, const juce::File& b)
              { return a.getLastModificationTime() < b.getLastModificationTime(); });
    const auto now = juce::Time::getCurrentTime();
    for (const auto& f : files)
    {
        // plik jeszcze zapisywany (np. eksport z REAPER-a) – poczekaj, aż przestanie się zmieniać
        if (now - f.getLastModificationTime() < juce::RelativeTime::seconds(1.5))
            continue;
        const auto key = f.getFullPathName() + "|" + juce::String(f.getLastModificationTime().toMilliseconds());
        if (seenInFolder.contains(key))
            continue;
        seenInFolder.add(key);
        if (isAudioFile(f) && f.getLastModificationTime().toMilliseconds() > folderBaseline)
            queueLoad(f, 1.0f);
    }
}

void VocalProcessor::timerCallback()
{
    {
        const juce::ScopedLock sl(poolLock);
        pool.collectGarbage();
    }

    if (++folderTick >= 20) // co 2 s
    {
        folderTick = 0;
        scanFurnaceFolder();
    }
}

std::vector<std::shared_ptr<sk::VocalSample>> VocalProcessor::poolContents() const
{
    const juce::ScopedLock sl(poolLock);
    std::vector<std::shared_ptr<sk::VocalSample>> v;
    for (int i = 0; i < pool.size(); ++i)
        v.push_back(pool.at(i));
    return v;
}

juce::String VocalProcessor::lastError() const
{
    const juce::ScopedLock sl(poolLock);
    return loadError;
}

juce::AudioProcessorEditor* VocalProcessor::createEditor()
{
    return new VocalEditor(*this);
}

void VocalProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    if (! xml)
        return;
    auto* furnace = xml->createNewChildElement("Furnace");
    furnace->setAttribute("folder", furnaceFolder.getFullPathName());
    furnace->setAttribute("scannedAt", juce::String(juce::Time::currentTimeMillis()));
    for (const auto& s : poolContents())
    {
        auto* e = furnace->createNewChildElement("Sample");
        e->setAttribute("path", juce::String::fromUTF8(s->path.c_str()));
        e->setAttribute("weight", (double) s->weight.load());
    }
    copyXmlToBinary(*xml, destData);
}

void VocalProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (! xml || ! xml->hasTagName(apvts.state.getType()))
        return;

    std::unique_ptr<juce::XmlElement> furnace;
    if (auto* f = xml->getChildByName("Furnace"))
    {
        furnace = std::make_unique<juce::XmlElement>(*f);
        xml->removeChildElement(f, true);
    }
    apvts.replaceState(juce::ValueTree::fromXml(*xml));

    if (furnace)
    {
        clearPool();
        for (auto* e : furnace->getChildWithTagNameIterator("Sample"))
        {
            const juce::File f(e->getStringAttribute("path"));
            if (f.existsAsFile())
                queueLoad(f, (float) e->getDoubleAttribute("weight", 1.0));
        }
        // pliki z folderu pieca sprzed zapisu projektu już były w piecu (albo zostały usunięte)
        folderBaseline = furnace->getStringAttribute("scannedAt").getLargeIntValue();
        seenInFolder.clear();
        const auto folder = furnace->getStringAttribute("folder");
        furnaceFolder = juce::File::isAbsolutePath(folder) ? juce::File(folder) : juce::File();
        ++version;
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VocalProcessor();
}
