// Testy gitary prowadzącej bez JUCE: jam Piano + Bass + Gitara (120 BPM, 48 kHz,
// bufor 128), bębniarz z przejściem na tomach. Sprawdzamy harmonię względem
// Piano i Bass, keyswitche Session Guitarist, brak wiszących nut i hooki.
#include "../Source/Core/Engine.h"
#include "../Source/Core/GuitarEngine.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <vector>

using namespace sk;

static int failures = 0;
#define CHECK(c, ...) do { if (! (c)) { ++failures; std::printf("BŁĄD %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

struct Span { double on, off; int pitch; };

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
        h.push_back({ o - 0.003, 36, 110 });
        h.push_back({ o + 1.5, 36, 90 });
        h.push_back({ o + 2.0, 36, 105 });
        h.push_back({ o + 1.006, 38, loud ? 120 : 85 });
        if (! fill) h.push_back({ o + 3.006, 38, loud ? 120 : 85 });
        if (fill)
            for (int s = 8; s < 16; ++s) h.push_back({ o + s * 0.25, s < 12 ? 48 : 45, 100 });
        if (b == 8) h.push_back({ o, 49, 120 });
    }
    std::sort(h.begin(), h.end(), [](auto& a, auto& b) { return a.ppq < b.ppq; });
    return h;
}

static void testLibrary()
{
    const auto& s = guitarPhrases();
    CHECK(s.count > 300, "za mało fraz: %d", s.count);
    CHECK(s.maxEnergy > s.minEnergy, "brak rozrzutu energii");
    int idioms = 0, hooks = 0;
    for (int i = 0; i < s.count; ++i)
    {
        const auto& ph = s.phrases[i];
        idioms += ph.source == 2;
        hooks += ph.repeats > 1;
        CHECK(ph.count >= 2, "pusta fraza %d", i);
        for (int k = 0; k < ph.count; ++k)
        {
            const auto& n = s.notes[ph.first + k];
            const int barEnd = n.step < 16 ? 16 : 32;
            CHECK(n.step < 32 && n.len >= 1 && n.step + n.len <= barEnd, "zła nuta frazy %d", i);
            CHECK(k == 0 || s.notes[ph.first + k - 1].step < n.step, "fraza %d nie jest monofoniczna", i);
        }
    }
    CHECK(idioms >= 10, "brak idiomów gitarowych");
    CHECK(hooks > 50, "za mało hooków (fraz powtarzanych w piosence): %d", hooks);
}

struct JamResult
{
    std::vector<Span> guitar, piano, bass, keyswitch;
    std::vector<std::pair<double, int>> bends;       // (ppq, wartość 14-bit)
    std::vector<std::pair<double, MidiOut>> raw;      // wszystkie zdarzenia gitary (do zrzutu)
    std::map<int, int> pianoChord, guitarChord, guitarPhrase, pianoSection;
    int unbalanced = 0;
    int ccVibrato = 0;
    GuitarParams gp;
};

static JamResult jam(GuitarParams gp, bool withPiano, int bars = 20, uint32_t seedBlocks = 0)
{
    JamResult r;
    r.gp = gp;
    SharedLink link;
    Engine piano(Role::Piano, link), bass(Role::Bass, link);
    GuitarEngine guitar(link);
    piano.prepare();
    bass.prepare();
    guitar.prepare();
    EngineParams pp, bp;
    pp.group = bp.group = gp.group;
    pp.harmony.key = 9;      // A
    pp.harmony.progA = 1;    // vi-IV-I-V
    pp.harmony.progB = 0;    // I-V-vi-IV
    pp.harmony.valid = true;
    bp.harmony = {};
    if (withPiano) piano.heartbeat(pp);
    bass.heartbeat(bp);
    guitar.heartbeat(gp);

    const double sr = 48000.0, pps = 2.0 / sr;
    const int block = 128;
    const auto hits = drummer(bars);
    std::map<int, double> openG, openP, openB;
    size_t hi = 0;
    double ppq = 0.0;
    int blocks = int(seedBlocks);

    auto collect = [&](const MidiOutBuffer& out, std::map<int, double>& open, std::vector<Span>& dst, bool isGuitar)
    {
        for (int i = 0; i < out.count; ++i)
        {
            const auto& m = out.events[i];
            CHECK(m.sampleOffset >= 0 && m.sampleOffset < block, "offset poza blokiem");
            const double t = ppq + m.sampleOffset * pps;
            const int type = m.status & 0xf0;
            if (isGuitar) r.raw.push_back({ t, m });
            if (isGuitar && type == 0xE0) r.bends.push_back({ t, m.data1 | m.data2 << 7 });
            if (isGuitar && type == 0xB0 && m.data1 == 1) ++r.ccVibrato;
            if (type != 0x90 && type != 0x80) continue;
            const bool on = type == 0x90 && m.data2 > 0;
            if (on)
            {
                if (open.count(m.data1)) ++r.unbalanced; // ta sama nuta dwa razy bez note-off
                open[m.data1] = t;
            }
            else if (open.count(m.data1))
            {
                const bool isKs = isGuitar && gp.art.keyswitches && m.data1 < 48;
                (isKs ? r.keyswitch : dst).push_back({ open[m.data1], t, m.data1 });
                open.erase(m.data1);
            }
        }
    };

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
        info.barOrigin = std::floor(ppq / 4.0) * 4.0;

        // kolejność instancji rotuje (REAPER przetwarza ścieżki równolegle)
        for (int k = 0; k < 3; ++k)
        {
            const int which = (blocks + k) % 3;
            MidiOutBuffer out;
            if (which == 0 && withPiano)
            {
                piano.process(info, pp, bh.data(), int(bh.size()), out);
                collect(out, openP, r.piano, false);
                r.pianoChord[piano.status.bar] = piano.status.chordRoot * 16 + piano.status.chordQuality;
                r.pianoSection[piano.status.bar] = piano.status.section;
            }
            else if (which == 1)
            {
                bass.process(info, bp, bh.data(), int(bh.size()), out);
                collect(out, openB, r.bass, false);
            }
            else if (which == 2)
            {
                guitar.process(info, gp, bh.data(), int(bh.size()), out);
                collect(out, openG, r.guitar, true);
                r.guitarChord[guitar.status.bar] = guitar.status.chordRoot * 16 + guitar.status.chordQuality;
                r.guitarPhrase[guitar.status.bar] = guitar.status.phrase;
            }
        }
        if (blocks % 200 == 0)
        {
            if (withPiano) piano.heartbeat(pp);
            bass.heartbeat(bp);
            guitar.heartbeat(gp);
        }
        ppq = end;
        ++blocks;
    }
    // stop transportu
    {
        BlockInfo stop;
        MidiOutBuffer out;
        guitar.process(stop, gp, nullptr, 0, out);
        for (int i = 0; i < out.count; ++i)
            if ((out.events[i].status & 0xf0) == 0x80 && openG.count(out.events[i].data1))
            {
                r.guitar.push_back({ openG[out.events[i].data1], ppq, out.events[i].data1 });
                openG.erase(out.events[i].data1);
            }
        CHECK(openG.empty(), "po stopie wiszą nuty gitary: %d", int(openG.size()));
    }
    return r;
}

// Wysokość brzmiąca: uwzględnia bend z dołu (nuta + podciągnięcie).
static int soundingAt(const JamResult& r, const Span& s, double t)
{
    int bend = 0;
    for (auto& [bt, v] : r.bends)
        if (bt >= s.on && bt <= t && bt <= s.off)
            bend = v;
    if (bend == 0) return s.pitch;
    return s.pitch + int(std::lround(double(bend - 8192) / 8191.0 * r.gp.art.bendRange));
}

static void testJamWithSection()
{
    GuitarParams gp;
    gp.group = 1;
    gp.ornaments = 0.85f;
    gp.variation = 0.0f;   // hook powinien wracać
    const auto r = jam(gp, true);

    CHECK(r.unbalanced == 0, "podwójne note-on bez note-off: %d", r.unbalanced);
    CHECK(r.guitar.size() > 40, "gitara gra za mało: %d", int(r.guitar.size()));

    // keyswitche tylko w strefie C1..B1, melodia nad nią
    for (auto& s : r.keyswitch) CHECK(s.pitch >= 36 && s.pitch <= 47, "dziwny keyswitch %d", s.pitch);
    for (auto& s : r.guitar) CHECK(s.pitch >= 48 && s.pitch <= 90, "gitara poza rejestrem %d", s.pitch);

    // użyte artykulacje Session Guitarist
    std::set<int> ks;
    for (auto& s : r.keyswitch) ks.insert(s.pitch);
    CHECK(ks.count(gp.art.open), "brak keyswitcha open (C1)");
    CHECK(ks.count(gp.art.slideIn), "brak slajdów (B1)");
    CHECK(ks.count(gp.art.slideDownEnd) || ks.count(gp.art.slideUpEnd), "brak zjazdów na końcu frazy (A1/G#1)");
    CHECK(! r.bends.empty(), "brak bendów (pitch bend)");
    CHECK(r.ccVibrato > 0, "brak wibrata (CC1)");
    int legato = 0;
    for (size_t i = 1; i < r.guitar.size(); ++i)
        for (size_t j = 0; j < r.guitar.size(); ++j)
            if (j != i && r.guitar[j].on < r.guitar[i].on && r.guitar[j].off > r.guitar[i].on + 0.001
                && std::abs(r.guitar[j].pitch - r.guitar[i].pitch) <= 3)
                ++legato;
    CHECK(legato > 0, "brak hammer-onów / pull-offów (zachodzące nuty)");

    // ta sama harmonia co Piano
    int agree = 0;
    for (auto& [bar, c] : r.pianoChord)
        if (bar >= 1 && r.guitarChord.count(bar))
        {
            CHECK(r.guitarChord.at(bar) == c, "takt %d: piano %d, gitara %d", bar, c, r.guitarChord.at(bar));
            ++agree;
        }
    CHECK(agree >= 18, "za mało wspólnych taktów: %d", agree);
    CHECK(r.pianoSection.at(8) == 1, "brak refrenu po przejściu");

    // Żadnych zgrzytów półtonowych z Piano i Bass (sprawdzamy co 1/32 nuty,
    // pomijając pierwsze 0.15 ćw. po bendzie z dołu – to celowy ozdobnik).
    int clashes = 0, checks = 0;
    for (auto& g : r.guitar)
        for (double t = g.on + 0.01; t < g.off - 0.01; t += 0.125)
        {
            const int gpitch = soundingAt(r, g, t);
            bool bendingNow = false;
            for (auto& [bt, v] : r.bends)
                if (bt >= g.on && bt <= g.off && t < bt + 0.15 && t > bt - 0.4) bendingNow = true;
            if (bendingNow) continue;
            for (auto* acc : { &r.piano, &r.bass })
                for (auto& a : *acc)
                    if (a.on <= t && a.off > t)
                    {
                        ++checks;
                        const int d = mod12(gpitch - a.pitch);
                        if (d == 1 || d == 11)
                        {
                            if (clashes < 5)
                                std::printf("  zgrzyt t=%.3f gitara %d, %s %d\n", t, gpitch, acc == &r.piano ? "piano" : "bass", a.pitch);
                            ++clashes;
                        }
                    }
        }
    CHECK(clashes == 0, "zgrzyty półtonowe: %d na %d porównań", clashes, checks);

    // w przejściu bębniarza (2. połowa taktu 7) gitara nie zaczyna nowych nut
    for (auto& g : r.guitar) CHECK(! (g.on > 7 * 4 + 2.1 && g.on < 8 * 4 - 0.1), "gitara wchodzi w przejście %.2f", g.on);

    // hook: przy wariacjach 0 motyw sekcji wraca co 4 takty
    const int refrain = 8;
    CHECK(r.guitarPhrase.at(refrain) >= 0 && r.guitarPhrase.at(refrain) == r.guitarPhrase.at(refrain + 4),
          "hook refrenu nie wrócił: %d vs %d", r.guitarPhrase.at(refrain), r.guitarPhrase.at(refrain + 4));
    CHECK(r.guitarPhrase.at(refrain) != r.guitarPhrase.at(refrain + 2), "odpowiedź = motyw (%d)", r.guitarPhrase.at(refrain + 2));

    std::printf("gitara: %d nut, keyswitchy: %d, bendów: %d, legato: %d, porównań harmonii: %d\n",
                int(r.guitar.size()), int(r.keyswitch.size()), int(r.bends.size()), legato, checks);

    if (const char* path = std::getenv("SK_DUMP"))
        if (FILE* f = std::fopen(path, "w"))
        {
            for (auto& s : r.piano) std::fprintf(f, "P %.5f %.5f %d\n", s.on, s.off, s.pitch);
            for (auto& s : r.bass) std::fprintf(f, "B %.5f %.5f %d\n", s.on, s.off, s.pitch);
            for (auto& s : r.guitar) std::fprintf(f, "G %.5f %.5f %d\n", s.on, s.off, s.pitch);
            for (auto& s : r.keyswitch) std::fprintf(f, "K %.5f %.5f %d\n", s.on, s.off, s.pitch);
            for (auto& [t, v] : r.bends) std::fprintf(f, "W %.5f %d\n", t, v);
            for (auto& [t, m] : r.raw) std::fprintf(f, "R %.5f %d %d %d\n", t, m.status, m.data1, m.data2);
            std::fclose(f);
        }
}

static void testModes()
{
    // Wypełnienia: gitara gra tylko w co drugiej frazie (2 takty ciszy, 2 takty gry)
    GuitarParams gp;
    gp.group = 2;
    gp.role = 1;
    const auto r = jam(gp, true, 16);
    std::set<int> barsWithNotes;
    for (auto& g : r.guitar) barsWithNotes.insert(int(std::floor(g.on / 4.0)));
    for (int b : barsWithNotes)
    {
        const int pos = b >= 8 ? b - 8 : b; // refren od taktu 8
        CHECK(((pos >> 1) & 1) == 1, "wypełnienia: nuta w takcie motywu %d", b);
    }
    CHECK(barsWithNotes.size() >= 4, "wypełnienia grają za mało taktów: %d", int(barsWithNotes.size()));

    // Ogólny instrument: bez keyswitchy, ale z nutami
    GuitarParams gen;
    gen.group = 3;
    gen.art.keyswitches = false;
    const auto g = jam(gen, false, 12);
    CHECK(g.keyswitch.empty(), "tryb ogólny wysłał keyswitche");
    CHECK(g.guitar.size() > 15, "bez Piano gitara gra za mało: %d", int(g.guitar.size()));
    CHECK(g.unbalanced == 0, "podwójne note-on (tryb ogólny)");

    // Solo: gęściej niż hook
    GuitarParams solo;
    solo.group = 0;
    solo.role = 2;
    const auto s = jam(solo, true, 16);
    GuitarParams hook;
    hook.group = 0;
    const auto h = jam(hook, true, 16);
    CHECK(s.guitar.size() > h.guitar.size(), "solo nie jest gęstsze od hooka: %d vs %d", int(s.guitar.size()), int(h.guitar.size()));
}

int main()
{
    testLibrary();
    testJamWithSection();
    testModes();
    if (failures) { std::printf("%d błędów\n", failures); return 1; }
    std::printf("OK\n");
    return 0;
}
