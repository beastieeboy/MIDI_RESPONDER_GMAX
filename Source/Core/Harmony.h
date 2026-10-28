#pragma once
#include <array>
#include <cstdint>

namespace sk {

enum Quality : uint8_t { Maj, Min, Dom7, Maj7, Min7, Dim, Sus };

struct Chord
{
    int8_t root = 0;      // klasa wysokości 0..11
    uint8_t quality = Maj;
};

struct Degree
{
    int8_t semis;         // odległość pryma akordu od toniki
    uint8_t quality;
};

struct Progression
{
    const char* name;     // stopnie rzymskie, wyświetlane w oknie wyboru
    bool minorKey;
    uint8_t length;
    Degree chords[8];
};

// Najpopularniejsze progresje popowe. Każdy akord trwa jeden takt.
inline constexpr Progression progressions[] = {
    { "I - V - vi - IV",            false, 4, { {0,Maj},{7,Maj},{9,Min},{5,Maj} } },
    { "vi - IV - I - V",            false, 4, { {9,Min},{5,Maj},{0,Maj},{7,Maj} } },
    { "I - vi - IV - V",            false, 4, { {0,Maj},{9,Min},{5,Maj},{7,Maj} } },
    { "I - IV - vi - V",            false, 4, { {0,Maj},{5,Maj},{9,Min},{7,Maj} } },
    { "IV - I - V - vi",            false, 4, { {5,Maj},{0,Maj},{7,Maj},{9,Min} } },
    { "I - IV - I - V",             false, 4, { {0,Maj},{5,Maj},{0,Maj},{7,Maj} } },
    { "ii7 - V7 - Imaj7 - Imaj7",   false, 4, { {2,Min7},{7,Dom7},{0,Maj7},{0,Maj7} } },
    { "I - iii - IV - V",           false, 4, { {0,Maj},{4,Min},{5,Maj},{7,Maj} } },
    { "Kanon: I-V-vi-iii-IV-I-IV-V",false, 8, { {0,Maj},{7,Maj},{9,Min},{4,Min},{5,Maj},{0,Maj},{5,Maj},{7,Maj} } },
    { "Imaj7 - IVmaj7",             false, 2, { {0,Maj7},{5,Maj7} } },
    { "I - bVII - IV - I",          false, 4, { {0,Maj},{10,Maj},{5,Maj},{0,Maj} } },
    { "IV - V - iii - vi",          false, 4, { {5,Maj},{7,Maj},{4,Min},{9,Min} } },
    { "i - VI - III - VII",         true,  4, { {0,Min},{8,Maj},{3,Maj},{10,Maj} } },
    { "i - VII - VI - V7",          true,  4, { {0,Min},{10,Maj},{8,Maj},{7,Dom7} } },
    { "i - iv - VI - V",            true,  4, { {0,Min},{5,Min},{8,Maj},{7,Maj} } },
    { "i - VI - VII - i",           true,  4, { {0,Min},{8,Maj},{10,Maj},{0,Min} } },
    { "i - iv - VII - III",         true,  4, { {0,Min},{5,Min},{10,Maj},{3,Maj} } },
};
inline constexpr int numProgressions = int(sizeof(progressions) / sizeof(progressions[0]));

inline constexpr const char* noteNames[12] = { "C","C#","D","Eb","E","F","F#","G","Ab","A","Bb","B" };

inline int mod12(int x) { return ((x % 12) + 12) % 12; }

inline Chord chordFor(int key, int progIndex, int position)
{
    const auto& p = progressions[progIndex < 0 || progIndex >= numProgressions ? 0 : progIndex];
    const auto& d = p.chords[((position % p.length) + p.length) % p.length];
    return { int8_t(mod12(key + d.semis)), d.quality };
}

// Składniki akordu w półtonach od pryma: tercja, kwinta, septyma (-1 = brak).
struct ChordShape { int third, fifth, seventh; };

inline ChordShape shapeOf(uint8_t q)
{
    switch (q)
    {
        case Min:  return { 3, 7, -1 };
        case Dom7: return { 4, 7, 10 };
        case Maj7: return { 4, 7, 11 };
        case Min7: return { 3, 7, 10 };
        case Dim:  return { 3, 6, -1 };
        case Sus:  return { 5, 7, -1 };
        default:   return { 4, 7, -1 };
    }
}

inline bool isChordTone(int pc, uint8_t q)
{
    const auto s = shapeOf(q);
    return pc == 0 || pc == s.third || pc == s.fifth || pc == s.seventh;
}

// Skala tonacji (dur / moll naturalny) jako maska 12 bitów.
inline uint16_t scaleMask(int key, bool minor)
{
    static constexpr int majorSteps[7] = { 0, 2, 4, 5, 7, 9, 11 };
    static constexpr int minorSteps[7] = { 0, 2, 3, 5, 7, 8, 10 };
    uint16_t m = 0;
    for (int s : (minor ? minorSteps : majorSteps))
        m |= uint16_t(1u << mod12(key + s));
    return m;
}

// Przeniesienie interwału licka z akordu źródłowego na docelowy.
// Gwarancja braku zgrzytów: wynik jest składnikiem akordu albo dźwiękiem skali
// tonacji, który nie leży półton od składnika akordu.
inline int remapInterval(int rel, uint8_t src, uint8_t dst, int chordRoot, uint16_t scale)
{
    const int oct = (rel >= 0 ? rel / 12 : (rel - 11) / 12) * 12;
    int pc = rel - oct;
    const auto s = shapeOf(src), d = shapeOf(dst);

    if (pc == s.third || ((src == Sus) && pc == 5))
        pc = d.third;
    else if (pc == s.fifth || pc == 6 || pc == 8)
        pc = d.fifth;
    else if (pc == 10 || pc == 11)
        pc = d.seventh >= 0 ? d.seventh : 12;

    auto allowed = [&](int p)
    {
        const int q = mod12(p);
        if (isChordTone(q, dst))
            return true;
        if (! (scale & (1u << mod12(chordRoot + q))))
            return false;
        for (int t : { 0, d.third, d.fifth, d.seventh })
            if (t >= 0 && (mod12(q - t) == 1 || mod12(t - q) == 1))
                return false; // nona mała / dźwięk półtonowy względem składnika
        return true;
    };

    if (! allowed(pc))
    {
        // najbliższy składnik akordu (w dół przy remisie)
        int best = 0, bestDist = 99;
        for (int t : { 0, d.third, d.fifth, d.seventh, 12 })
        {
            if (t < 0) continue;
            const int dist = t > pc ? t - pc : pc - t;
            if (dist < bestDist) { best = t; bestDist = dist; }
        }
        pc = best;
    }
    return oct + pc;
}

} // namespace sk
