#pragma once
#include "Harmony.h"
#include "SharedLink.h"

namespace sk {

// Akord bieżącego taktu dla instancji, która tylko słucha (Wokal).
// Ta sama reguła co w Bass: gdy Piano gra w tym zespole, bierzemy jego decyzję o takcie
// (albo przewidujemy ją z tych samych bębnów, jeśli Piano jeszcze nie zdecydowało);
// bez Piano liczymy progresję sami z własnych ustawień. Nic nie publikujemy,
// więc Wokal nie może wywołać sprzężenia zwrotnego z resztą sekcji.
class ChordFollower
{
public:
    HarmonySettings harmony;
    int section = 0, sectionStart = 0, chordPos = 0;
    Chord chord;
    bool followingPiano = false;

    static bool pianoAlive(const GroupState& g)
    {
        const int64_t hb = g.pianoHeartbeat.load(std::memory_order_relaxed);
        return hb != 0 && SharedLink::nowMs() - hb < SharedLink::aliveMs;
    }

    // Na początku taktu `bar`. continuous: poprzedni takt też był grany; fill: w poprzednim
    // takcie było przejście na tomach.
    void decide(int bar, bool continuous, bool fill, const HarmonySettings& own, const GroupState& g)
    {
        const bool alive = pianoAlive(g);
        const auto shared = HarmonySettings::unpack(g.harmony.load(std::memory_order_relaxed));
        harmony = alive && shared.valid ? shared : own;
        followingPiano = alive && shared.valid;

        const uint64_t pb = g.pianoBar.load(std::memory_order_acquire);
        int pbBar, pbSec, pbPos;
        unpack(pb, pbBar, pbSec, pbPos);
        usedPianoBar = 0;

        if (followingPiano && pbBar == bar)
        {
            usedPianoBar = pb;
            section = pbSec;
            sectionStart = bar - pbPos;
        }
        else if (followingPiano && pbBar == bar - 1)
        {
            section = pbSec;
            sectionStart = bar - 1 - pbPos;
            if (fill && harmony.fillSwitchesSection)
            {
                section ^= 1;
                sectionStart = bar;
            }
        }
        else if (! continuous)
        {
            section = 0;
            sectionStart = 0;
        }
        else if (fill && harmony.fillSwitchesSection)
        {
            section ^= 1;
            sectionStart = bar;
        }
        update(bar);
    }

    // W pierwszej połowie taktu: Piano mogło zdecydować po nas (inny wątek). true = zmiana akordu.
    bool refresh(int bar, const GroupState& g)
    {
        if (! followingPiano)
            return false;
        const uint64_t pb = g.pianoBar.load(std::memory_order_acquire);
        int pbBar, pbSec, pbPos;
        unpack(pb, pbBar, pbSec, pbPos);
        if (pbBar != bar || pb == usedPianoBar)
            return false;
        usedPianoBar = pb;
        const Chord before = chord;
        section = pbSec;
        sectionStart = bar - pbPos;
        update(bar);
        return before.root != chord.root || before.quality != chord.quality;
    }

    // Transport stoi: akord z ostatniej decyzji Piano albo pierwszy akord własnej zwrotki.
    void idle(const HarmonySettings& own, const GroupState& g)
    {
        int pbBar, pbSec, pbPos;
        unpack(g.pianoBar.load(std::memory_order_acquire), pbBar, pbSec, pbPos);
        decide(pianoAlive(g) ? pbBar : 0, false, false, own, g);
    }

    int progression() const { return section ? harmony.progB : harmony.progA; }
    bool minorKey() const
    {
        const int p = progression();
        return progressions[p < 0 || p >= numProgressions ? 0 : p].minorKey;
    }
    int positionInProgression() const
    {
        const int p = progression();
        const int len = progressions[p < 0 || p >= numProgressions ? 0 : p].length;
        return chordPos % (len > 0 ? len : 1);
    }

private:
    static void unpack(uint64_t w, int& bar, int& sec, int& pos)
    {
        bar = int(int32_t(uint32_t(w >> 32)));
        sec = int((w >> 8) & 255);
        pos = int(w & 255);
    }

    void update(int bar)
    {
        chordPos = bar - sectionStart < 0 ? 0 : bar - sectionStart;
        chord = chordFor(harmony.key, progression(), chordPos);
    }

    uint64_t usedPianoBar = 0;
};

} // namespace sk
