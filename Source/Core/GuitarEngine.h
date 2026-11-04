#pragma once
#include "DrumAnalyzer.h"
#include "Engine.h"
#include "GuitarTypes.h"
#include "Harmony.h"
#include "SharedLink.h"
#include <atomic>
#include <cstdint>

namespace sk {

// Keyswitche i sterowanie instrumentu Kontakt.
// Domyślnie: Session Guitarist Electric Neon (Essentials / pełna) w trybie Melody.
// NI nazywa oktawy tak, że C3 = 60, więc C1 = 36.
struct GuitarArticulations
{
    bool keyswitches = true;   // false = ogólny instrument (tylko nuty, pitch bend i CC1)
    int open = 36;             // C1   artykulacja „open”
    int muted = 37;            // C#1  palm mute
    int slideUpEnd = 44;       // G#1  zakończenie nuty slajdem w górę
    int slideDownEnd = 45;     // A1   zakończenie nuty slajdem w dół (prędkość = dynamika)
    int slideIn = 47;          // B1   modyfikator: następna nuta wchodzi slajdem
    int bendRange = 2;         // zakres pitch bend instrumentu w półtonach
};

struct GuitarParams
{
    int midiChannel = 1;
    int group = 0;
    float reactivity = 0.6f, variation = 0.4f, dynamics = 0.7f, feel = 0.5f, density = 0.0f;
    DrumMap drums;
    HarmonySettings harmony;   // zapas, gdy w zespole nie ma Piano

    int role = 0;              // 0 melodia (MELODY), 1 wypełnienia (kontrmelodie BRIDGE), 2 solo
    int registerCenter = 72;   // środek rejestru frazy (nuta MIDI)
    int lowest = 52, highest = 88;
    float ornaments = 0.35f;   // ile slajdów, bendów, hammer-onów, wibrata i zjazdów
    float space = 0.6f;        // jak mocno omijać rytm Piano
    GuitarArticulations art;
};

struct GuitarStatus
{
    std::atomic<int> chordRoot { 0 }, chordQuality { 0 }, section { 0 }, chordPos { 0 };
    std::atomic<int> bar { -1 }, phrase { -1 }, phraseSource { 0 }, phraseBar { 0 }, phraseSong { 0 };
    std::atomic<int> mode { 0 };   // 0 czeka, 1 melodia zwrotki, 2 melodia refrenu, 3 czeka na frazę, 4 przejście
    std::atomic<float> phraseFit { 0.0f }; // jak dobrze akordy pasażu pasują do progresji (0..1)
    std::atomic<int> lastPitch { -1 };
    std::atomic<int> slides { 0 }, bends { 0 }, legatos { 0 }, falls { 0 };
    std::atomic<float> energy { 0.0f };
    std::atomic<bool> pianoAlive { false }, bassAlive { false }, linkShared { false };
    std::atomic<uint32_t> harmonyWord { 0 };
};

// Gitara prowadząca. Słucha bębnów i (przez SharedLink) Piano i Bass, sama
// niczego nie publikuje, więc nie może wywołać sprzężenia zwrotnego.
class GuitarEngine
{
public:
    explicit GuitarEngine(SharedLink& link);

    void prepare();
    void process(const BlockInfo& info, const GuitarParams& p, const DrumHit* hits, int numHits, MidiOutBuffer& out);
    void heartbeat(const GuitarParams& p);

    GuitarStatus status;

    enum Flags : uint8_t
    {
        LegatoIn = 1, LegatoOut = 2, SlideIn = 4, Bend = 8, Vibrato = 16, FallOff = 32, Muted = 64
    };

private:
    struct Planned { int8_t step, len; uint8_t pitch, vel, flags; };
    struct Event { double ppq; uint32_t seq; uint8_t status, d1, d2; };

    void decideBar(int bar, const BlockInfo& info, const GuitarParams& p);
    void resolveHarmony(int bar, const BarFeatures& f, const GuitarParams& p, bool continuous);
    int choosePassage(float target, int pos0, const GuitarParams& p);
    float harmonicFit(const Passage& ps, int pos0) const;
    void chooseShift(const GuitarParams& p);
    void buildBar(const GuitarParams& p, int spb, bool accent);
    void scheduleNote(const Planned& n, double t, double stepLen, const GuitarParams& p);
    void push(double ppq, uint8_t status, uint8_t d1, uint8_t d2);
    void flush(const BlockInfo& info, double until, MidiOutBuffer& out);
    void allOff(const GuitarParams& p, MidiOutBuffer& out);
    bool alive(int64_t hb) const;
    float rand01();

    SharedLink& link;
    const PassageSet* set = nullptr;
    DrumAnalyzer drums;

    bool wasPlaying = false;
    double expectedPpq = 0.0;
    int decidedBar = INT32_MIN;

    HarmonySettings harmony;
    int section = 0, sectionStart = 0, chordPos = 0;
    Chord chord;
    uint64_t usedPianoBar = 0;
    bool sectionChanged = false;
    bool pianoAlive = false, bassAlive = false;

    // pasaż (8 taktów melodii z jednej piosenki)
    int passage = -1, passageStart = 0, passageBar = 0, passageShift = 0;
    int sectionPassage[2] = { -1, -1 }, sectionShift[2] = { 0, 0 };
    int recent[6] = { -1, -1, -1, -1, -1, -1 };
    int lastPitch = -1;
    int currentArt = -1;
    float barTarget = 0.0f;
    bool playingBar = false;

    Planned planned[48];
    int numPlanned = 0;

    // kolejka zdarzeń MIDI (nuty, keyswitche, pitch bend, CC) z czasem w ppq
    static constexpr int queueSize = 512;
    Event queue[queueSize];
    int queued = 0;
    uint32_t seqCounter = 0;
    uint8_t channel = 0;

    // co aktualnie brzmi (do bezpiecznego zatrzymania)
    uint8_t sounding[16];
    int numSounding = 0;
    bool bent = false, vibrato = false;

    uint32_t rng = 0x2545f491u;
};

} // namespace sk
