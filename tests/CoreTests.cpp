// Testy rdzenia bez JUCE: symulacja REAPER-a (120 BPM, 48 kHz, bufor 128)
// z bębniarzem grającym groove, przejście na tomach i dwie instancje.
#include "../Source/Core/Engine.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <vector>

using namespace sk;

static int failures = 0;
#define CHECK(c, ...) do { if (! (c)) { ++failures; std::printf("BŁĄD %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

struct Note { double ppq; int pitch, vel; bool on; };

static void testRemap()
{
    for (int key = 0; key < 12; ++key)
        for (int pi = 0; pi < numProgressions; ++pi)
        {
            const auto& pr = progressions[pi];
            const uint16_t sc = scaleMask(key, pr.minorKey);
            for (int c = 0; c < pr.length; ++c)
            {
                const Chord ch = chordFor(key, pi, c);
                for (int src = 0; src <= Sus; ++src)
                    for (int rel = -24; rel <= 36; ++rel)
                    {
                        const int r = remapInterval(rel, uint8_t(src), ch.quality, ch.root, sc);
                        const int pc = mod12(r);
                        const bool ok = isChordTone(pc, ch.quality) || (sc & (1u << mod12(ch.root + pc)));
                        CHECK(ok, "zgrzyt: key %d prog %d chord %d src %d rel %d -> %d", key, pi, c, src, rel, r);
                        CHECK(std::abs(r - rel) <= 12, "za duży skok %d -> %d", rel, r);
                    }
            }
        }
}

static void testLibrary()
{
    for (Role r : { Role::Piano, Role::Bass })
    {
        const auto& s = lickSet(r);
        CHECK(s.count > 100, "za mało licków: %d", s.count);
        CHECK(s.maxEnergy > s.minEnergy, "brak rozrzutu energii");
        for (int i = 0; i < s.count; ++i)
            for (int k = 0; k < s.licks[i].count; ++k)
            {
                const auto& n = s.notes[s.licks[i].first + k];
                CHECK(n.step < 16 && n.len >= 1 && n.step + n.len <= 16, "zły lick %d", i);
            }
    }
}

// Groove: HH ósemki, stopa 1 i 3 (+ "i" przed 3), werbel 2 i 4.
// Takty 0..7 groove, takt 7 kończy się przejściem na tomach, 8..15 mocniej (16-tki HH).
static std::vector<DrumHit> drummer(int bars)
{
    std::vector<DrumHit> h;
    for (int b = 0; b < bars; ++b)
    {
        const double o = b * 4.0;
        const bool loud = b >= 8;
        const bool fill = b == 7;
        for (int s = 0; s < 16; s += loud ? 1 : 2)
            if (! (fill && s >= 8)) h.push_back({ o + s * 0.25 + 0.004, 42, loud ? 100 : 70 });
        h.push_back({ o + 0.0 - 0.003, 36, 110 });
        h.push_back({ o + 1.5, 36, 90 });
        h.push_back({ o + 2.0, 36, 105 });
        h.push_back({ o + 1.0 + 0.006, 38, loud ? 120 : 85 });
        if (! fill) h.push_back({ o + 3.0 + 0.006, 38, loud ? 120 : 85 });
        if (fill)
            for (int s = 8; s < 16; ++s) h.push_back({ o + s * 0.25, s < 12 ? 48 : 45, 100 });
        if (b == 8) h.push_back({ o, 49, 120 });
    }
    std::sort(h.begin(), h.end(), [](auto& a, auto& b) { return a.ppq < b.ppq; });
    return h;
}

static void testJam()
{
    SharedLink link;
    Engine piano(Role::Piano, link), bass(Role::Bass, link);
    piano.prepare();
    bass.prepare();
    EngineParams pp, bp;
    pp.group = bp.group = 2;
    pp.harmony.key = 9;      // A
    pp.harmony.progA = 1;    // vi-IV-I-V
    pp.harmony.progB = 0;    // I-V-vi-IV
    bp.harmony = {};         // Bass nie zna progresji – ma ją wziąć z linku
    piano.heartbeat(pp);
    bass.heartbeat(bp);

    const double sr = 48000.0, bpm = 120.0;
    const double pps = bpm / 60.0 / sr;
    const int block = 128, bars = 16;
    const auto hits = drummer(bars);
    std::vector<Note> pn, bn;
    std::map<int, int> pianoChordByBar, bassChordByBar, pianoSectionByBar;

    size_t hi = 0;
    double ppq = 0.0;
    int blocks = 0;
    while (ppq < bars * 4.0)
    {
        const double end = ppq + block * pps;
        std::vector<DrumHit> bh;
        while (hi < hits.size() && hits[hi].ppq < end)
            bh.push_back(hits[hi++]);
        BlockInfo info;
        info.playing = true;
        info.ppqStart = ppq;
        info.ppqPerSample = pps;
        info.numSamples = block;

        for (int which = 0; which < 2; ++which)
        {
            // kolejność instancji zmienia się (REAPER przetwarza ścieżki równolegle)
            Engine& e = ((blocks + which) % 2) ? bass : piano;
            MidiOutBuffer out;
            e.process(info, &e == &piano ? pp : bp, bh.data(), int(bh.size()), out);
            auto& dst = &e == &piano ? pn : bn;
            for (int i = 0; i < out.count; ++i)
            {
                const auto& m = out.events[i];
                CHECK(m.sampleOffset >= 0 && m.sampleOffset < block, "offset poza blokiem");
                if ((m.status & 0xf0) == 0x90 || (m.status & 0xf0) == 0x80)
                    dst.push_back({ ppq + m.sampleOffset * pps, m.data1, m.data2, (m.status & 0xf0) == 0x90 && m.data2 > 0 });
            }
            const int bar = e.status.bar;
            (&e == &piano ? pianoChordByBar : bassChordByBar)[bar] = e.status.chordRoot * 16 + e.status.chordQuality;
            if (&e == &piano) pianoSectionByBar[bar] = e.status.section;
        }
        if (blocks % 200 == 0) { piano.heartbeat(pp); bass.heartbeat(bp); }
        ppq = end;
        ++blocks;
    }
    // stop transportu
    for (Engine* e : { &piano, &bass })
    {
        BlockInfo stop;
        MidiOutBuffer out;
        e->process(stop, e == &piano ? pp : bp, nullptr, 0, out);
        for (int i = 0; i < out.count; ++i)
            if ((out.events[i].status & 0xf0) == 0x80)
                (e == &piano ? pn : bn).push_back({ ppq, out.events[i].data1, 0, false });
    }

    for (auto* v : { &pn, &bn })
    {
        std::map<int, int> open;
        for (auto& n : *v) open[n.pitch] += n.on ? 1 : -1;
        for (auto& [p, c] : open) CHECK(c == 0, "wisząca nuta %d (%d)", p, c);
    }

    int pOn = 0, bOn = 0;
    for (auto& n : pn) if (n.on) { ++pOn; CHECK(n.pitch >= 36 && n.pitch <= 96, "piano poza rejestrem %d", n.pitch); }
    for (auto& n : bn) if (n.on) { ++bOn; CHECK(n.pitch >= 24 && n.pitch <= 67, "bass poza rejestrem %d", n.pitch); }
    CHECK(pOn > 30, "piano gra za mało: %d", pOn);
    CHECK(bOn > 15, "bass gra za mało: %d", bOn);

    // Piano ustępuje basowi
    for (auto& n : pn) if (n.on) CHECK(n.pitch >= 52, "piano weszło basowi w drogę: %d", n.pitch);

    // Wspólna harmonia w każdym takcie, w którym oba grają
    int agree = 0;
    for (auto& [bar, c] : pianoChordByBar)
        if (bar >= 1 && bassChordByBar.count(bar))
        {
            CHECK(bassChordByBar[bar] == c, "takt %d: piano %d, bass %d", bar, c, bassChordByBar[bar]);
            ++agree;
        }
    CHECK(agree >= 14, "za mało wspólnych taktów: %d", agree);

    // Przejście w takcie 7 => od taktu 8 refren
    CHECK(pianoSectionByBar[7] == 0 && pianoSectionByBar[8] == 1, "brak zmiany sekcji po przejściu (%d,%d)",
          pianoSectionByBar[7], pianoSectionByBar[8]);
    // Pierwszy akord refrenu (I-V-vi-IV w A) = A dur
    CHECK(pianoChordByBar[8] == 9 * 16 + Maj, "refren nie zaczyna się od I: %d", pianoChordByBar[8]);
    // Zwrotka vi-IV-I-V w A: takt 1 => D dur
    CHECK(pianoChordByBar[1] == 2 * 16 + Maj, "takt 1 powinien być D: %d", pianoChordByBar[1]);

    // Piano w przejściu milknie (druga połowa taktu 7)
    for (auto& n : pn) if (n.on) CHECK(! (n.ppq > 7 * 4 + 2.1 && n.ppq < 8 * 4 - 0.1), "piano gra w przejściu %.2f", n.ppq);

    // Głośniejsza druga połowa => wyższa średnia gęstość piano
    int first = 0, second = 0;
    for (auto& n : pn) if (n.on) (n.ppq < 32 ? first : second)++;
    CHECK(second > first, "brak reakcji na energię: %d vs %d", first, second);

    if (const char* path = std::getenv("SK_DUMP"))
        if (FILE* f = std::fopen(path, "w"))
        {
            for (auto& h : hits) std::fprintf(f, "D %.5f %d %d 1\n", h.ppq, h.note, h.velocity);
            for (auto& n : pn) std::fprintf(f, "P %.5f %d %d %d\n", n.ppq, n.pitch, n.vel, n.on ? 1 : 0);
            for (auto& n : bn) std::fprintf(f, "B %.5f %d %d %d\n", n.ppq, n.pitch, n.vel, n.on ? 1 : 0);
            std::fclose(f);
        }

    std::printf("piano: %d nut, bass: %d nut, wspólne takty: %d, link współdzielony: %s\n", pOn, bOn, agree,
                link.isShared() ? "tak (shm)" : "nie");
}

int main()
{
    testRemap();
    testLibrary();
    testJam();
    if (failures) { std::printf("%d błędów\n", failures); return 1; }
    std::printf("OK\n");
    return 0;
}
