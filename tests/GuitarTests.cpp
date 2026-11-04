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

// fills = false: zapętlony jeden pattern (bez przejść, ta sama głośność).
static std::vector<DrumHit> drummer(int bars, bool fills = true)
{
    std::vector<DrumHit> h;
    for (int b = 0; b < bars; ++b)
    {
        const double o = b * 4.0;
        const bool loud = fills && b >= 8;
        const bool fill = fills && b == 7;
        for (int s = 0; s < 16; s += loud ? 1 : 2)
            if (! (fill && s >= 8)) h.push_back({ o + s * 0.25 + 0.004, 42, loud ? 100 : 70 });
        h.push_back({ o - 0.003, 36, 110 });
        h.push_back({ o + 1.5, 36, 90 });
        h.push_back({ o + 2.0, 36, 105 });
        h.push_back({ o + 1.006, 38, loud ? 120 : 85 });
        if (! fill) h.push_back({ o + 3.006, 38, loud ? 120 : 85 });
        if (fill)
            for (int s = 8; s < 16; ++s) h.push_back({ o + s * 0.25, s < 12 ? 48 : 45, 100 });
        if (fills && b == 8) h.push_back({ o, 49, 120 });
    }
    std::sort(h.begin(), h.end(), [](auto& a, auto& b) { return a.ppq < b.ppq; });
    return h;
}

static void testLibrary()
{
    const auto& s = guitarPassages();
    CHECK(s.count > 500, "za mało pasaży: %d", s.count);
    CHECK(s.maxEnergy > s.minEnergy, "brak rozrzutu energii");
    int bridge = 0, repeated = 0;
    for (int i = 0; i < s.count; ++i)
    {
        const auto& ps = s.passages[i];
        bridge += ps.source == 1;
        repeated += ps.repeats > 1;
        CHECK(ps.count >= 12, "za krótki pasaż %d", i);
        for (int k = 0; k < 8; ++k)
            CHECK(ps.chords[k].semis >= 0 && ps.chords[k].semis < 12 && ps.chords[k].quality <= Sus, "zły akord pasażu %d", i);
        for (int k = 0; k < ps.count; ++k)
        {
            const auto& n = s.notes[ps.first + k];
            CHECK(n.bar < 8 && n.step < 16 && n.len >= 1 && n.step + n.len <= 16, "zła nuta pasażu %d", i);
            if (k > 0)
            {
                const auto& m = s.notes[ps.first + k - 1];
                CHECK(m.bar * 16 + m.step < n.bar * 16 + n.step, "pasaż %d nie jest monofoniczny", i);
            }
        }
    }
    CHECK(bridge > 100 && bridge < s.count, "brak podziału MELODY / BRIDGE: %d", bridge);
    CHECK(repeated > 20, "za mało powtarzanych pasaży (refrenów): %d", repeated);
}

struct JamResult
{
    std::vector<Span> guitar, piano, bass, keyswitch;
    std::vector<std::pair<double, int>> bends;       // (ppq, wartość 14-bit)
    std::vector<std::pair<double, MidiOut>> raw;      // wszystkie zdarzenia gitary (do zrzutu)
    std::map<int, int> pianoChord, guitarChord, guitarPhrase, guitarSource, pianoSection, guitarSection;
    int unbalanced = 0;
    int ccVibrato = 0;
    GuitarParams gp;
};

static JamResult jam(GuitarParams gp, bool withPiano, int bars = 20, bool fills = true, int formBars = 0)
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
    pp.harmony.formBars = formBars;
    bp.harmony = {};
    if (withPiano) piano.heartbeat(pp);
    bass.heartbeat(bp);
    guitar.heartbeat(gp);

    const double sr = 48000.0, pps = 2.0 / sr;
    const int block = 128;
    const auto hits = drummer(bars, fills);
    std::map<int, double> openG, openP, openB;
    size_t hi = 0;
    double ppq = 0.0;
    int blocks = 0;

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
                r.guitarSource[guitar.status.bar] = guitar.status.phrase >= 0 ? guitar.status.phraseSource.load() : -1;
                r.guitarSection[guitar.status.bar] = guitar.status.section;
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

static void dump(const JamResult& r, const char* env)
{
    if (const char* path = std::getenv(env))
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

    // Melodia refrenu to jeden 8-taktowy pasaż z jednej piosenki, który wraca po 8 taktach.
    const int refrain = 8;
    CHECK(r.guitarPhrase.at(refrain) >= 0, "brak melodii refrenu");
    for (int b = refrain; b < refrain + 8; ++b)
        CHECK(r.guitarPhrase.at(b) == r.guitarPhrase.at(refrain), "refren miesza pasaże w takcie %d", b);
    CHECK(r.guitarPhrase.at(refrain + 8) == r.guitarPhrase.at(refrain), "melodia refrenu nie wróciła: %d vs %d",
          r.guitarPhrase.at(refrain), r.guitarPhrase.at(refrain + 8));
    CHECK(r.guitarPhrase.at(4) >= 0 && r.guitarPhrase.at(4) != r.guitarPhrase.at(refrain), "zwrotka = refren");

    // Płynna linia: w obrębie frazy średni skok to krok, nie skakanie po oktawach.
    double jump = 0.0;
    int pairs = 0;
    for (size_t i = 1; i < r.guitar.size(); ++i)
        if (r.guitar[i].on - r.guitar[i - 1].on < 1.01)
        {
            jump += std::abs(r.guitar[i].pitch - r.guitar[i - 1].pitch);
            ++pairs;
        }
    CHECK(pairs > 20 && jump / pairs < 4.0, "linia skacze: średnio %.2f półtonu", pairs ? jump / pairs : 0.0);
    std::printf("średni skok melodii: %.2f półtonu\n", pairs ? jump / pairs : 0.0);

    std::printf("gitara: %d nut, keyswitchy: %d, bendów: %d, legato: %d, porównań harmonii: %d\n",
                int(r.guitar.size()), int(r.keyswitch.size()), int(r.bends.size()), legato, checks);

    dump(r, "SK_DUMP");
}

static void testModes()
{
    // Wypełnienia: tylko kontrmelodie instrumentalne (BRIDGE)
    GuitarParams gp;
    gp.group = 2;
    gp.role = 1;
    const auto r = jam(gp, true, 16);
    int played = 0;
    for (auto& [bar, src] : r.guitarSource)
        if (src >= 0)
        {
            ++played;
            CHECK(src == 1, "wypełnienia: pasaż spoza BRIDGE w takcie %d", bar);
        }
    CHECK(played >= 8, "wypełnienia grają za mało taktów: %d", played);
    CHECK(r.guitar.size() > 10, "wypełnienia: za mało nut %d", int(r.guitar.size()));

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

// Zapętlony jeden pattern perkusji: forma automatyczna co 8 taktów (zwrotka, refren, zwrotka...).
static void testAutoForm()
{
    GuitarParams gp;
    gp.group = 3;
    gp.variation = 0.0f;
    const auto r = jam(gp, true, 26, false, 8);
    for (int b : { 1, 7, 8, 15, 16, 24 })
    {
        const int want = (b / 8) & 1;
        CHECK(r.pianoSection.count(b) && r.pianoSection.at(b) == want, "piano: takt %d sekcja %d", b, r.pianoSection.count(b) ? r.pianoSection.at(b) : -1);
        CHECK(r.guitarSection.count(b) && r.guitarSection.at(b) == want, "gitara: takt %d sekcja %d", b, r.guitarSection.count(b) ? r.guitarSection.at(b) : -1);
    }
    CHECK(r.guitarPhrase.at(4) >= 0 && r.guitarPhrase.at(8) >= 0, "gitara nie gra przy zapętlonym bębnie");
    CHECK(r.guitarPhrase.at(8) != r.guitarPhrase.at(4), "refren gra melodię zwrotki");
    CHECK(r.guitarPhrase.at(16) == r.guitarPhrase.at(4), "druga zwrotka bez melodii pierwszej: %d vs %d",
          r.guitarPhrase.at(16), r.guitarPhrase.at(4));
    CHECK(r.guitarPhrase.at(24) == r.guitarPhrase.at(8), "drugi refren bez melodii pierwszego");
    int agree = 0;
    for (auto& [bar, c] : r.pianoChord)
        if (bar >= 1 && r.guitarChord.count(bar))
        {
            CHECK(r.guitarChord.at(bar) == c, "forma: takt %d piano %d gitara %d", bar, c, r.guitarChord.at(bar));
            ++agree;
        }
    CHECK(agree >= 24, "forma: za mało wspólnych taktów %d", agree);
    CHECK(r.unbalanced == 0, "forma: podwójne note-on");
    dump(r, "SK_DUMP_FORM");
}

int main()
{
    testLibrary();
    testJamWithSection();
    testModes();
    testAutoForm();
    if (failures) { std::printf("%d błędów\n", failures); return 1; }
    std::printf("OK\n");
    return 0;
}
