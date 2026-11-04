#pragma once
#include "Harmony.h"
#include <cstdint>

namespace sk {

// Jedna nuta pasażu melodii: takt 0..7, krok 0..15 w takcie, długość w 16-tkach
// (nie przechodzi przez kreskę taktową), wysokość względem toniki *durowej*
// tonacji pasażu (półtony, tonikę liczymy w oktawie C4..B4) i dynamika.
struct PassageNote
{
    uint8_t bar, step, len;
    int8_t rel;
    uint8_t vel;
};

// Pasaż = 8 kolejnych taktów melodii z jednej piosenki POP909.
// chords[k]: akord taktu k jako stopień (półtony od toniki durowej) i jakość.
// source: 0 melodia wokalu (MELODY), 1 kontrmelodia instrumentalna (BRIDGE).
// repeats: ile razy pasaż wystąpił w piosence (refreny > 1).
struct Passage
{
    uint16_t first, count;
    Degree chords[8];
    uint8_t source, repeats;
    int8_t centre;          // średnia wysokość względem toniki
    float energy, density;
    uint16_t song;          // numer piosenki w POP909
};

struct PassageSet
{
    const Passage* passages;
    const PassageNote* notes;
    int count;
    float minEnergy, maxEnergy;
};

// Biblioteka wygenerowana z POP909 (tools/extract_guitar.py).
const PassageSet& guitarPassages();

} // namespace sk
