#pragma once
#include "ChordFollower.h"
#include "DrumAnalyzer.h"
#include "Engine.h"
#include "SamplePool.h"
#include <atomic>
#include <cstdint>

namespace sk {

// Ustawienia Wokalu (syntezator granularny pracujący wyłącznie na samplach z pieca).
struct VocalParams
{
    int group = 0;
    HarmonySettings harmony;       // własna harmonia, gdy w zespole nie ma Piano
    DrumMap drums;

    // ziarna
    float grainMs = 140.0f;        // długość ziarna
    float density = 14.0f;         // ziaren na sekundę (przed reakcją na bębny)
    float position = 0.3f;         // gdzie w samplu (0..1)
    float scatter = 0.6f;          // rozrzut pozycji
    float scan = 0.0f;             // przesuwanie pozycji w czasie (-1..1)
    float reverse = 0.0f;          // szansa ziarna od tyłu
    float shape = 0.0f;            // 0 = gładkie (Hann), 1 = perkusyjne
    float width = 0.6f;            // szerokość stereo

    // strój i mieszanie
    float retune = 0.85f;          // 0 = naturalna melodia sampla, 1 = każde ziarno dokładnie na składniku akordu
    int octave = 0;                // -2..2
    float spread = 0.3f;           // rozrzut po składnikach akordu i oktawach
    float voiced = 0.7f;           // preferuj samogłoski (fragmenty z wysokością)
    float freshness = 0.3f;        // nowsze sample w piecu grają częściej
    float gainDb = 0.0f;

    // reakcja na bębny
    float hatToDensity = 0.6f;     // gęstość hi-hatu -> gęstość ziaren
    float swell = 0.6f;            // przejście na tomach -> puchnięcie chmury
    float pulse = 0.3f;            // uderzenia bębnów wyzwalają ziarna
    float dynamics = 0.6f;         // dynamika bębniarza -> głośność
    float reactivity = 0.6f;
    bool playWithoutDrums = false; // chmura gra także, gdy bębniarz milczy
};

struct VocalHit
{
    int offset;                    // próbka w bloku
    double ppq;                    // pozycja (gdy transport gra)
    int note, velocity;
};

struct VocalStatus
{
    std::atomic<int> chordRoot { 0 }, chordQuality { 0 }, section { 0 }, chordPos { 0 }, bar { -1 };
    std::atomic<int> activeGrains { 0 };
    std::atomic<float> energy { 0.0f }, hatDensity { 0.0f }, swell { 0.0f }, grainsPerSecond { 0.0f }, activity { 0.0f };
    std::atomic<bool> followingPiano { false }, playing { false };
    std::atomic<uint32_t> harmonyWord { 0 };
};

class VocalEngine
{
public:
    static constexpr int maxGrains = 128;

    explicit VocalEngine(SharedLink& link);

    void prepare(double sampleRate);
    // L i R muszą mieć numSamples próbek; są nadpisywane.
    void process(const BlockInfo& info, const VocalParams& p, const VocalHit* hits, int numHits,
                 const PoolSnapshot* pool, float* L, float* R);

    // Najstarsza generacja pieca, z której grają jeszcze ziarna (do zwalniania pamięci).
    uint64_t oldestGenerationInUse() const { return oldestGen; }

    VocalStatus status;

    // Diagnostyka dla testów: klasy wysokości docelowych nut ziaren i liczba ziaren.
    struct Diagnostics { uint32_t pitchClass[12] {}; uint32_t spawned = 0; } diagnostics;

private:
    enum class Voice : uint8_t { Cloud, Kick, Snare, Hat, Burst };

    struct Grain
    {
        const VocalSample* s;
        uint64_t gen;
        double pos, rate;
        int len, age, delay;
        float gL, gR;
    };

    void decideBar(int bar, const BlockInfo& info, const VocalParams& p);
    void onHit(const VocalHit& h, const VocalParams& p, const BlockInfo& info, int sampleIndex);
    void render(int from, int to, const VocalParams& p, float* L, float* R);
    void spawn(Voice v, int delay, float velocity, const VocalParams& p);
    int pickSample(const VocalParams& p);
    float pickTarget(float src, Voice v, const VocalParams& p);
    float rand01();

    SharedLink& link;
    DrumAnalyzer drums;
    ChordFollower follower;
    const PoolSnapshot* pool = nullptr;

    double sr = 48000.0;
    bool wasPlaying = false;
    double expectedPpq = 0.0;
    int decidedBar = INT32_MIN;

    // stan sterowania (wygładzany co 32 próbki)
    double beatSec = 0.5;
    float hatAcc = 0.0f, hatDensityNow = 0.0f, swellTarget = 0.0f, swellNow = 0.0f, activity = 0.0f, liveVel = 0.7f;
    float spawnPhase = 0.0f, spawnThreshold = 1.0f, scanPhase = 0.0f, rateNow = 0.0f;
    long long sinceHit = 1LL << 40;

    Grain grains[maxGrains];
    int numGrains = 0;
    uint64_t oldestGen = 0;
    uint32_t rng = 0x2545f491u;

    float hann[1025], perc[1025];
};

} // namespace sk
