#include "LickLibrary.h"
#include "LickLibraryData.h"

namespace sk {

namespace {
    template <int N>
    LickSet make(const Lick (&l)[N], const LickNote* n)
    {
        float lo = 1.0f, hi = 0.0f;
        for (const auto& x : l)
        {
            lo = x.energy < lo ? x.energy : lo;
            hi = x.energy > hi ? x.energy : hi;
        }
        return { l, n, N, lo, hi };
    }
}

const LickSet& lickSet(Role role)
{
    static const LickSet piano = make(data::pianoLicks, data::pianoNotes);
    static const LickSet bass = make(data::bassLicks, data::bassNotes);
    return role == Role::Piano ? piano : bass;
}

} // namespace sk
