#pragma once
#include "DrumAnalyzer.h"
#include "Harmony.h"
#include "LickLibrary.h"
#include "SharedLink.h"
#include <atomic>
#include <cstdint>

namespace sk {

struct EngineParams
{
    int midiChannel = 1;          // 1..16
    float reactivity = 0.6f;      // jak szybko energia podąża za bębniarzem
    float variation = 0.4f;       // jak często zmieniać lick i ile losowości
    float dynamics = 0.7f;        // ile dynamiki bębniarza przenosić na nuty
    float feel = 0.5f;            // ile "przed/za bitem" bębniarza kopiować
    float density = 0.0f;         // -1..1 przesunięcie energii (gęstości)
    int group = 0;                // zespół 0..3 (link między instancjami)
    DrumMap drums;

    // Piano (okno progresji)
    HarmonySettings harmony;
    bool giveWayToBass = true;    // gdy Bass gra, Piano nie schodzi poniżej E3
    int registerCenter = 64;

    // Bass
    int octave = 0;               // -1..1
    float kickLock = 0.6f;        // jak mocno rytm basu trzyma się stopy
};

struct DrumHit
{
    double ppq;
    int note, velocity;
};

struct BlockInfo
{
    bool playing = false;
    double ppqStart = 0.0;
    double ppqPerSample = 0.0;
    int numSamples = 0;
    double barLenPpq = 4.0;       // długość taktu w ćwierćnutach
    int stepsPerBar = 16;         // liczba 16-tek w takcie
    double barOrigin = 0.0;       // ppq dowolnego początku taktu
};

struct MidiOut
{
    int sampleOffset;
    uint8_t status, data1, data2;
};

struct MidiOutBuffer
{
    static constexpr int capacity = 512;
    MidiOut events[capacity];
    int count = 0;
    void add(int offset, uint8_t s, uint8_t d1, uint8_t d2)
    {
        if (count < capacity)
            events[count++] = { offset, s, d1, d2 };
    }
};

// Stan dla GUI (czytany z wątku wiadomości).
struct EngineStatus
{
    std::atomic<int> chordRoot { 0 }, chordQuality { 0 }, section { 0 }, chordPos { 0 };
    std::atomic<int> bar { -1 }, lick { -1 }, mode { 0 }; // mode: 0 czeka, 1 gra, 2 przejście, 3 pad
    std::atomic<float> energy { 0.0f }, hatDensity { 0.0f };
    std::atomic<bool> partnerAlive { false }, linkShared { false };
    std::atomic<uint32_t> harmonyWord { 0 };
};

class Engine
{
public:
    Engine(Role role, SharedLink& link);

    void prepare();
    void process(const BlockInfo& info, const EngineParams& p, const DrumHit* hits, int numHits, MidiOutBuffer& out);
    void heartbeat(const EngineParams& p); // z timera wątku wiadomości, ~5 Hz

    Role role() const { return myRole; }
    EngineStatus status;

private:
    struct Planned { int8_t step, len; uint8_t pitch, vel; };
    struct Active { uint8_t pitch; double endPpq; };
    enum class PlanKind { Rest, Lick, Pad };

    void decideBar(int bar, const BlockInfo& info, const EngineParams& p);
    void resolveHarmony(int bar, const BarFeatures& f, const EngineParams& p, bool continuous);
    int chooseLick(float target, const BarFeatures& f, const EngineParams& p);
    void buildLickPlan(int lickIndex, const EngineParams& p, int stepsPerBar, bool accent);
    void buildPad(int fromStep, const EngineParams& p, int stepsPerBar);
    void noteOn(int offset, uint8_t pitch, uint8_t vel, double endPpq, const EngineParams& p, MidiOutBuffer& out);
    void releaseUntil(double ppq, const BlockInfo& info, const EngineParams& p, MidiOutBuffer& out);
    void allNotesOff(const EngineParams& p, MidiOutBuffer& out);
    bool partnerAlive(const EngineParams& p) const;
    float rand01();

    Role myRole;
    SharedLink& link;
    const LickSet* set = nullptr;
    DrumAnalyzer drums;

    bool wasPlaying = false;
    double expectedPpq = 0.0;
    int decidedBar = INT32_MIN;

    // harmonia bieżącego taktu
    HarmonySettings harmony;
    int section = 0, sectionStart = 0, chordPos = 0;
    Chord chord;
    uint64_t usedPianoBar = 0;
    bool sectionChanged = false;

    // wybór licka
    int currentLick = -1, lickSince = 0;
    float lickTarget = 0.0f;
    int recent[4] = { -1, -1, -1, -1 };
    float centroid = 64.0f;

    PlanKind plan = PlanKind::Rest;
    Planned planned[64];
    int numPlanned = 0;
    uint32_t planMask = 0;

    Active active[64];
    int numActive = 0;
    uint32_t rng = 0x9e3779b9u;
};

} // namespace sk
