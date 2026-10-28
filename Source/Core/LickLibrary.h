#pragma once
#include "LickTypes.h"

namespace sk {

struct LickSet
{
    const Lick* licks;
    const LickNote* notes;
    int count;
    float minEnergy, maxEnergy;
};

enum class Role { Piano, Bass };

// Biblioteka wygenerowana z POP909 (tools/extract_licks.py).
const LickSet& lickSet(Role role);

} // namespace sk
