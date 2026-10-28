#pragma once
#include <atomic>
#include <cstdint>

namespace sk {

// Pamięć współdzielona między instancjami Piano i Bass (POSIX shm).
// Działa w obrębie procesu REAPER-a i także między procesami, gdy REAPER
// uruchamia wtyczki w osobnym procesie (bridge). Wszystko to atomiki bez blokad,
// więc czytanie i pisanie z wątku audio jest bezpieczne.
//
// Kierunek przepływu jest celowo jednostronny (lider -> akompaniament):
//   Piano publikuje: progresję, sekcję i akord każdego taktu, rytm swojego taktu, energię.
//   Bass publikuje tylko "jestem tu" (Piano zwalnia wtedy niski rejestr).
// Żadna instancja nie reaguje na nuty drugiej w tym samym takcie, więc nie ma
// pętli sprzężenia zwrotnego: decyzje zapadają raz na takt i opierają się na
// poprzednim, już zamkniętym takcie.
struct GroupState
{
    // Ustawienia harmonii z okna progresji (Piano): bity
    // [0..3] tonacja, [4..11] progresja zwrotki, [12..19] progresja refrenu,
    // [20] przejście zmienia sekcję, [31] ważne.
    std::atomic<uint32_t> harmony { 0 };

    // Decyzja Piano dla taktu: [63..32] numer taktu, [15..8] sekcja, [7..0] pozycja akordu.
    std::atomic<uint64_t> pianoBar { 0 };
    // Rytm Piano: [63..32] numer taktu, [31..0] maska kroków z nutą.
    std::atomic<uint64_t> pianoRhythm { 0 };
    std::atomic<uint32_t> pianoEnergy { 0 };   // energia * 1000
    std::atomic<int64_t> pianoHeartbeat { 0 }; // ms zegara monotonicznego
    std::atomic<int64_t> bassHeartbeat { 0 };
    std::atomic<uint32_t> bassRhythm { 0 };
};

struct LinkBlock
{
    static constexpr uint32_t magicValue = 0x534b5231; // "SKR1"
    std::atomic<uint32_t> magic { 0 };
    GroupState groups[4];
};

class SharedLink
{
public:
    SharedLink();
    ~SharedLink();
    SharedLink(const SharedLink&) = delete;
    SharedLink& operator=(const SharedLink&) = delete;

    // Grupa 0..3 pozwala uruchomić kilka niezależnych "zespołów" w jednym projekcie.
    GroupState& group(int g) { return block->groups[g < 0 ? 0 : (g > 3 ? 3 : g)]; }
    bool isShared() const { return mapped; }

    static int64_t nowMs();
    static constexpr int64_t aliveMs = 1500;

private:
    LinkBlock* block = nullptr;
    bool mapped = false;
};

// Pomocnicze pakowanie/rozpakowanie.
struct HarmonySettings
{
    int key = 0, progA = 0, progB = 1;
    bool fillSwitchesSection = true;
    bool valid = false;

    uint32_t pack() const
    {
        return uint32_t(key & 15) | uint32_t(progA & 255) << 4 | uint32_t(progB & 255) << 12
             | uint32_t(fillSwitchesSection ? 1 : 0) << 20 | 1u << 31;
    }
    static HarmonySettings unpack(uint32_t w)
    {
        HarmonySettings h;
        h.valid = (w >> 31) & 1;
        h.key = int(w & 15);
        h.progA = int((w >> 4) & 255);
        h.progB = int((w >> 12) & 255);
        h.fillSwitchesSection = (w >> 20) & 1;
        return h;
    }
};

inline uint64_t packBar(int bar, int section, int chordPos)
{
    return uint64_t(uint32_t(bar)) << 32 | uint64_t(section & 255) << 8 | uint64_t(chordPos & 255);
}

} // namespace sk
