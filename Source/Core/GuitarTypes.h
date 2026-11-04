#pragma once
#include <cstdint>

namespace sk {

// Jedna nuta frazy gitary: krok w siatce 32 szesnastek (dwa takty), długość
// w krokach (nie przechodzi przez kreskę taktową), interwał względem pryma
// akordu *swojego* taktu (półtony, prymę liczymy w oktawie C4..B4) i dynamika.
struct PhraseNote
{
    uint8_t step, len;
    int8_t rel;
    uint8_t vel;
};

// Fraza = dwa takty 4/4. quality[k] to jakość akordu taktu k w źródle.
// source: 0 melodia wokalu POP909, 1 kontrmelodia BRIDGE POP909, 2 idiom gitarowy, 3 Lakh.
// repeats: ile razy fraza wystąpiła w piosence (hooki refrenu > 1).
struct Phrase
{
    uint16_t first, count;
    uint8_t quality[2];
    uint8_t source, repeats;
    int8_t centre;          // średni interwał (rejestr frazy)
    float energy, density;
    uint32_t onsetMask;     // kroki 0..31 z początkiem nuty
};

struct PhraseSet
{
    const Phrase* phrases;
    const PhraseNote* notes;
    int count;
    float minEnergy, maxEnergy;
};

// Biblioteka wygenerowana z POP909 (tools/extract_guitar.py).
const PhraseSet& guitarPhrases();

} // namespace sk
