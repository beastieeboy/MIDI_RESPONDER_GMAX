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
    enum Mode { Choir = 0, Cloud = 1 };
    int mode = Choir;              // Chór: stałe głosy na składnikach akordu; Chmura: rój ziaren
    int sync = 1;                  // chmura: 0 swobodnie, 1 = ósemki, 2 = szesnastki
    int group = 0;
    HarmonySettings harmony;       // własna harmonia, gdy w zespole nie ma Piano
    DrumMap drums;

    // ziarna
    float grainMs = 300.0f;        // długość ziarna
    float density = 8.0f;         // ziaren na sekundę (przed reakcją na bębny)
    float position = 0.3f;         // gdzie w samplu (0..1)
    float scatter = 0.15f;          // rozrzut pozycji
    float scan = 0.15f;            // przesuwanie pozycji w czasie (-1..1); w Chórze tempo czytania sampla
    float reverse = 0.0f;          // szansa ziarna od tyłu
    float shape = 0.0f;            // 0 = gładkie (Hann), 1 = perkusyjne
    float width = 0.6f;            // szerokość stereo

    // strój i mieszanie
    float retune = 1.0f;          // 0 = naturalna melodia sampla, 1 = każde ziarno dokładnie na składniku akordu
    int octave = 0;                // -2..2
    float spread = 0.0f;           // rozrzut po składnikach akordu i oktawach
    float voiced = 0.9f;           // preferuj samogłoski (fragmenty z wysokością)
    float freshness = 0.3f;        // nowsze sample w piecu grają częściej
    float gainDb = 0.0f;

    // reakcja na bębny
    float hatToDensity = 0.3f;     // gęstość hi-hatu -> gęstość ziaren
    float swell = 0.6f;            // przejście na tomach -> puchnięcie chmury
    float pulse = 0.1f;            // uderzenia bębnów wyzwalają ziarna
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
    void emit(const VocalSample* s, double pos, float src, float target, int lenSamples, float gain, float pan,
              bool reverse, int delay);
    void assignVoices(const VocalParams& p);
    void retuneChoir(const VocalParams& p);
    void stepChoir(int n, const VocalParams& p, float swellAmt);

    // Chór: każdy głos = jeden "śpiewak" (stały sampel, własna głowica, jeden składnik akordu)
    static constexpr int maxVoices = 5;
    struct ChoirVoice { const VocalSample* s = nullptr; uint64_t gen = 0; double pos = 0.0; float target = 60.0f; int timer = 0; };
    ChoirVoice voices[maxVoices];
    uint64_t choirGeneration = 0;
    int choirRoot = -1, choirQuality = -1;
    bool gridSynced = false;
    float gridCarry = 0.0f;
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
