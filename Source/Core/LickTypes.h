#pragma once
#include <cstdint>

namespace sk {

// Jeden dźwięk licka: krok w siatce 16-tek, długość w krokach,
// interwał względem pryma akordu (półtony) i oryginalna dynamika.
struct LickNote
{
    uint8_t step, len;
    int8_t rel;
    uint8_t vel;
};

// Lick = jeden takt 4/4. quality to jakość akordu, na którym został zagrany.
struct Lick
{
    uint16_t first, count;
    uint8_t quality;
    float energy, density, sync;
    uint16_t onsetMask;
};

} // namespace sk
