// Testy Wokalu bez JUCE: analiza wysokości sampli, piec (pula sampli) i silnik ziaren
// grający z bębniarzem i instancją Piano na wspólnym linku (120 BPM, 48 kHz, bufor 128).
#include "../Source/Core/VocalEngine.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <vector>

using namespace sk;

static int failures = 0;
#define CHECK(c, ...) do { if (! (c)) { ++failures; std::printf("BŁĄD %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

// "Wokal" do testów: dźwięk harmoniczny z formantem, wibratem i przerwami (spółgłoski = szum).
static std::vector<float> fakeVocal(double sr, double seconds, double hz, bool gaps, uint32_t seed = 1)
{
    std::vector<float> v(size_t(sr * seconds));
    double ph = 0.0;
    for (size_t i = 0; i < v.size(); ++i)
    {
        const double t = double(i) / sr;
        const bool consonant = gaps && std::fmod(t, 0.5) > 0.4;
        seed = seed * 1664525u + 1013904223u;
        const float noise = float(seed >> 8) / float(1 << 24) - 0.5f;
        if (consonant)
        {
            v[i] = 0.05f * noise;
            continue;
        }
        const double f = hz * (1.0 + 0.01 * std::sin(2.0 * M_PI * 5.5 * t));
        ph += 2.0 * M_PI * f / sr;
        double x = 0.0;
        for (int k = 1; k <= 12; ++k)
        {
            const double fk = f * k;
            const double formant = std::exp(-std::pow((fk - 700.0) / 400.0, 2.0)) + 0.4 * std::exp(-std::pow((fk - 1200.0) / 300.0, 2.0));
            x += (0.2 + formant) / k * std::sin(k * ph);
        }
        v[i] = float(0.3 * x) + 0.003f * noise;
    }
    return v;
}

static void testPitch()
{
    for (double sr : { 44100.0, 48000.0 })
        for (double hz : { 110.0, 196.0, 330.0, 523.25 })
        {
            auto s = makeVocalSample(fakeVocal(sr, 2.0, hz, false), sr, "t", "");
            const float expect = hzToMidi(float(hz));
            CHECK(std::abs(s->medianMidi - expect) < 0.35f, "sr %.0f %.1f Hz: mediana %.2f zamiast %.2f", sr, hz, s->medianMidi, expect);
            CHECK(s->voicedFraction > 0.85f, "%.1f Hz: za mało dźwięcznych ramek %.2f", hz, s->voicedFraction);
        }

    // przerwy ze spółgłoskami: ok. 20 % bezdźwięcznych
    auto g = makeVocalSample(fakeVocal(48000.0, 3.0, 220.0, true), 48000.0, "g", "");
    CHECK(g->voicedFraction > 0.6f && g->voicedFraction < 0.88f, "spółgłoski: dźwięczne %.2f", g->voicedFraction);
    CHECK(g->voicedAt(0.2 * 48000.0) && ! g->voicedAt(0.45 * 48000.0), "mapa dźwięczności");

    // sam szum nie ma wysokości
    std::vector<float> noise(48000);
    uint32_t r = 7;
    for (auto& x : noise) { r = r * 1664525u + 1013904223u; x = float(r >> 8) / float(1 << 24) - 0.5f; }
    auto n = makeVocalSample(noise, 48000.0, "n", "");
    CHECK(n->voicedFraction < 0.1f, "szum dźwięczny: %.2f", n->voicedFraction);

    // normalizacja głośności
    auto quiet = fakeVocal(48000.0, 1.0, 220.0, false);
    for (auto& x : quiet) x *= 0.01f;
    auto q = makeVocalSample(quiet, 48000.0, "q", "");
    double e = 0.0;
    for (float x : q->audio) e += double(x) * x;
    const double rms = std::sqrt(e / double(q->audio.size()));
    CHECK(rms > 0.05 && rms < 0.2, "normalizacja: rms %.3f", rms);
}

static void testPool()
{
    SamplePool pool;
    const auto* empty = pool.acquire();
    CHECK(empty && empty->count == 0, "pusty piec");

    std::weak_ptr<VocalSample> first;
    for (int i = 0; i < PoolSnapshot::maxSamples + 3; ++i)
    {
        std::shared_ptr<VocalSample> s = makeVocalSample(fakeVocal(48000.0, 0.2, 200.0 + i, false), 48000.0, "s" + std::to_string(i), "");
        if (i == 0) first = s;
        pool.add(s);
    }
    CHECK(pool.size() == PoolSnapshot::maxSamples, "piec ma limit: %d", pool.size());
    CHECK(pool.at(0)->name == "s3", "najstarsze spalone, a jest %s", pool.at(0)->name.c_str());
    // wątek audio nic nie zgłosił -> stare migawki (i sampel s0) muszą żyć
    CHECK(! first.expired(), "sampel zwolniony za wcześnie");
    const auto* snap = pool.acquire();
    pool.reportInUse(snap->generation);
    pool.collectGarbage();
    CHECK(first.expired(), "spalony sampel nie został zwolniony");
    CHECK(pool.pendingGarbage() == 0, "zostały migawki: %d", pool.pendingGarbage());

    // ziarno ze starej generacji trzyma sampel po wyczyszczeniu pieca
    std::weak_ptr<VocalSample> last = pool.at(pool.size() - 1);
    const uint64_t oldGen = pool.acquire()->generation;
    pool.clear();
    pool.reportInUse(oldGen);
    pool.collectGarbage();
    CHECK(! last.expired(), "sampel zwolniony mimo grającego ziarna");
    pool.reportInUse(pool.acquire()->generation);
    pool.collectGarbage();
    CHECK(last.expired(), "sampel nie zwolniony po zakończeniu ziaren");
}

// Groove jak w testach Piano/Bass: HH ósemki (takty 0..7), przejście na tomach w takcie 7,
// od taktu 8 HH szesnastki i crash na "raz"; takty 12..15 cisza.
static std::vector<DrumHit> drummer()
{
    std::vector<DrumHit> h;
    for (int b = 0; b < 12; ++b)
    {
        const double o = b * 4.0;
        const bool loud = b >= 8, fill = b == 7;
        for (int s = 0; s < 16; s += loud ? 1 : 2)
            if (! (fill && s >= 8)) h.push_back({ o + s * 0.25 + 0.004, 42, loud ? 100 : 70 });
        h.push_back({ o, 36, 110 });
        h.push_back({ o + 2.0, 36, 105 });
        h.push_back({ o + 1.0, 38, 90 });
        if (! fill) h.push_back({ o + 3.0, 38, 90 });
        if (fill)
            for (int s = 8; s < 16; ++s) h.push_back({ o + s * 0.25, s < 12 ? 48 : 45, 100 });
        if (b == 8) h.push_back({ o, 49, 120 });
    }
    std::sort(h.begin(), h.end(), [](auto& a, auto& b) { return a.ppq < b.ppq; });
    return h;
}

static void testJamWithPiano(bool withPiano)
{
    SharedLink link;
    Engine piano(Role::Piano, link);
    piano.prepare();
    EngineParams pp;
    pp.group = 3;
    pp.harmony.key = 9;      // A
    pp.harmony.progA = 1;    // vi-IV-I-V
    pp.harmony.progB = 0;    // I-V-vi-IV
    pp.harmony.valid = true;
    // wyczyść stan grupy po poprzednich testach
    link.group(3).pianoHeartbeat.store(0);
    link.group(3).pianoBar.store(0);
    if (withPiano) piano.heartbeat(pp);

    const double sr = 48000.0, pps = 120.0 / 60.0 / sr;
    const int block = 128, bars = 16;

    SamplePool pool;
    pool.add(makeVocalSample(fakeVocal(sr, 3.0, 207.0, true, 3), sr, "a", ""));
    pool.add(makeVocalSample(fakeVocal(44100.0, 2.0, 311.0, false, 5), 44100.0, "b", ""));

    VocalEngine voc(link);
    voc.prepare(sr);
    VocalParams vp;
    vp.group = 3;
    vp.retune = 1.0f;
    vp.harmony.key = 0;      // własna harmonia (C) – używana tylko bez Piano
    vp.harmony.progA = 0;
    vp.harmony.progB = 3;
    vp.harmony.valid = true;

    const auto hits = drummer();
    std::vector<float> L(block), R(block);
    std::map<int, int> pianoChord, vocChord;
    std::map<int, uint32_t> spawnedInBar;
    std::map<int, double> energyInBar;
    std::map<int, float> swellMax;
    float peak = 0.0f;
    bool finite = true;
    size_t hi = 0;
    double ppq = 0.0;
    int blocks = 0, offChord = 0, onChord = 0;

    while (ppq < bars * 4.0)
    {
        const double end = ppq + block * pps;
        std::vector<DrumHit> bh;
        std::vector<VocalHit> vh;
        while (hi < hits.size() && hits[hi].ppq < end)
        {
            bh.push_back(hits[hi]);
            const int off = std::clamp(int((hits[hi].ppq - ppq) / pps), 0, block - 1);
            vh.push_back({ off, hits[hi].ppq, hits[hi].note, hits[hi].velocity });
            ++hi;
        }
        BlockInfo info;
        info.playing = true;
        info.ppqStart = ppq;
        info.ppqPerSample = pps;
        info.numSamples = block;
        info.barOrigin = std::floor(ppq / 4.0) * 4.0;

        const int bar = int(std::floor(ppq / 4.0 + 1e-9));
        auto runPiano = [&]
        {
            if (! withPiano) return;
            MidiOutBuffer out;
            piano.process(info, pp, bh.data(), int(bh.size()), out);
            pianoChord[piano.status.bar] = piano.status.chordRoot * 16 + piano.status.chordQuality;
        };
        // kolejność zmienia się jak w REAPER-ze (ścieżki równolegle)
        if (blocks % 2) runPiano();
        voc.diagnostics = {};
        voc.process(info, vp, vh.data(), int(vh.size()), pool.acquire(), L.data(), R.data());
        if (! (blocks % 2)) runPiano();
        pool.reportInUse(voc.oldestGenerationInUse());

        const int vb = voc.status.bar;
        vocChord[vb] = voc.status.chordRoot * 16 + voc.status.chordQuality;
        spawnedInBar[bar] += voc.diagnostics.spawned;
        swellMax[bar] = std::max(swellMax[bar], voc.status.swell.load());
        for (int pc = 0; pc < 12; ++pc)
        {
            const uint32_t n = voc.diagnostics.pitchClass[pc];
            if (! n) continue;
            const int rel = mod12(pc - voc.status.chordRoot);
            if (isChordTone(rel, uint8_t(voc.status.chordQuality))) onChord += int(n);
            else offChord += int(n);
        }
        for (int i = 0; i < block; ++i)
        {
            finite = finite && std::isfinite(L[i]) && std::isfinite(R[i]);
            peak = std::max({ peak, std::abs(L[i]), std::abs(R[i]) });
            energyInBar[bar] += double(L[i]) * L[i] + double(R[i]) * R[i];
        }
        if (withPiano && blocks % 200 == 0) piano.heartbeat(pp);
        ppq = end;
        ++blocks;
    }

    CHECK(finite, "NaN/inf na wyjściu");
    CHECK(peak <= 1.0f, "przester: %.3f", peak);
    CHECK(onChord > 200, "za mało ziaren: %d", onChord);
    CHECK(offChord == 0, "ziarna poza akordem: %d (na akordzie %d)", offChord, onChord);

    if (withPiano)
    {
        int agree = 0;
        for (auto& [b, c] : pianoChord)
            if (b >= 1 && b < 12 && vocChord.count(b))
            {
                CHECK(vocChord[b] == c, "takt %d: piano %d, wokal %d", b, c, vocChord[b]);
                ++agree;
            }
        CHECK(agree >= 10, "za mało wspólnych taktów: %d", agree);
        CHECK(vocChord[1] == 2 * 16 + Maj, "takt 1 powinien być D (z Piano): %d", vocChord[1]);
        CHECK(vocChord[8] == 9 * 16 + Maj, "refren po przejściu = A: %d", vocChord[8]);
    }
    else
    {
        // własna harmonia: I-V-vi-IV w C
        CHECK(vocChord[1] == 7 * 16 + Maj, "bez Piano takt 1 = G: %d", vocChord[1]);
        CHECK(vocChord[2] == 9 * 16 + Min, "bez Piano takt 2 = Am: %d", vocChord[2]);
    }

    // HH szesnastki (takty 9..11) => gęściej niż ósemki (takty 2..5)
    const double eighth = (spawnedInBar[2] + spawnedInBar[3] + spawnedInBar[4] + spawnedInBar[5]) / 4.0;
    const double sixteenth = (spawnedInBar[9] + spawnedInBar[10] + spawnedInBar[11]) / 3.0;
    CHECK(sixteenth > 1.4 * eighth, "brak reakcji gęstości na hi-hat: %.1f vs %.1f", eighth, sixteenth);
    // przejście na tomach (takt 7) => puchnięcie
    CHECK(swellMax[7] > 0.5f && swellMax[4] < 0.1f, "puchnięcie: takt 7 %.2f, takt 4 %.2f", swellMax[7], swellMax[4]);
    CHECK(spawnedInBar[7] > 1.3 * eighth, "przejście nie zagęszcza chmury: %u vs %.1f", spawnedInBar[7], eighth);
    // cisza po takcie 12 => wygaszenie
    CHECK(energyInBar[15] < 1e-4 * energyInBar[10], "brak wygaszenia w ciszy: %.3g vs %.3g", energyInBar[15], energyInBar[10]);
    CHECK(energyInBar[1] > 0.0, "brak dźwięku w takcie 1");

    std::printf("wokal (%s): %d ziaren na akordzie, gęstość ósemki %.0f / szesnastki %.0f na takt, puchnięcie %.2f, szczyt %.2f\n",
                withPiano ? "z Piano" : "bez Piano", onChord, eighth, sixteenth, swellMax[7], peak);
}

static void testStoppedTransport()
{
    SharedLink link;
    link.group(1).pianoHeartbeat.store(0);
    SamplePool pool;
    pool.add(makeVocalSample(fakeVocal(48000.0, 1.0, 180.0, false), 48000.0, "a", ""));
    VocalEngine voc(link);
    voc.prepare(48000.0);
    VocalParams vp;
    vp.group = 1;
    vp.harmony.key = 2;      // D
    vp.harmony.progA = 0;
    vp.harmony.valid = true;
    std::vector<float> L(256), R(256);
    BlockInfo stop;
    stop.numSamples = 256;
    VocalHit h { 10, 0.0, 42, 100 };
    double e = 0.0;
    for (int b = 0; b < 200; ++b)
    {
        voc.process(stop, vp, &h, b % 12 == 0 ? 1 : 0, pool.acquire(), L.data(), R.data());
        for (float x : L) e += double(x) * x;
    }
    CHECK(e > 0.0, "bębniarz bez transportu: cisza");
    CHECK(voc.status.chordRoot == 2, "bez transportu akord z własnej tonacji: %d", voc.status.chordRoot.load());

    // pusty piec = cisza (żadnych oscylatorów)
    SamplePool emptyPool;
    VocalEngine v2(link);
    v2.prepare(48000.0);
    vp.playWithoutDrums = true;
    double e2 = 0.0;
    for (int b = 0; b < 100; ++b)
    {
        v2.process(stop, vp, &h, 1, emptyPool.acquire(), L.data(), R.data());
        for (float x : L) e2 += double(x) * x;
    }
    CHECK(e2 == 0.0, "pusty piec powinien milczeć: %g", e2);
}

int main()
{
    testPitch();
    testPool();
    testJamWithPiano(true);
    testJamWithPiano(false);
    testStoppedTransport();
    if (failures) { std::printf("%d błędów\n", failures); return 1; }
    std::printf("OK\n");
    return 0;
}
