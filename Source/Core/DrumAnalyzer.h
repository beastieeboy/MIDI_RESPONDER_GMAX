#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace sk {

enum class DrumPart : uint8_t { None, Kick, Snare, Tom, HiHat, Cymbal };

// Gotowe mapy perkusji.
enum class DrumPreset : int { GeneralMidi = 0, GgdModernMassive2 = 1 };

// GGD Modern & Massive 2 (mapa domyślna, C1 = 36). Ride (bow/bell) liczymy jako
// hi-hat, bo trzyma puls tak samo; choke'i talerzy to nie uderzenia.
inline DrumPart classifyGgdMm2(int note)
{
    switch (note)
    {
        case 36: return DrumPart::Kick;
        case 37: case 38: case 39: return DrumPart::Snare;
        case 42: case 43: case 44: case 45: case 46: case 47: case 48: return DrumPart::Tom;
        case 49: case 50: case 51: case 52: case 53: case 54: case 55: case 56: case 57:
        case 68: case 69: return DrumPart::HiHat;
        case 58: case 60: case 62: case 64: case 66: case 70: case 74: case 76: return DrumPart::Cymbal;
        default: return DrumPart::None;
    }
}

// Mapa perkusji: własne numery nut użytkownika + aliasy General MIDI
// albo gotowa mapa konkretnego instrumentu.
struct DrumMap
{
    int kick = 36, snare = 38, tom1 = 48, tom2 = 45, hihat = 42;
    DrumPreset preset = DrumPreset::GeneralMidi;

    DrumPart classify(int note) const
    {
        if (preset == DrumPreset::GgdModernMassive2)
            return classifyGgdMm2(note);
        if (note == kick) return DrumPart::Kick;
        if (note == snare) return DrumPart::Snare;
        if (note == tom1 || note == tom2) return DrumPart::Tom;
        if (note == hihat) return DrumPart::HiHat;
        switch (note)
        {
            case 35: case 36: return DrumPart::Kick;
            case 37: case 38: case 40: return DrumPart::Snare;
            case 41: case 43: case 45: case 47: case 48: case 50: return DrumPart::Tom;
            case 42: case 44: case 46: return DrumPart::HiHat;
            case 49: case 51: case 52: case 53: case 55: case 57: case 59: return DrumPart::Cymbal;
            default: return DrumPart::None;
        }
    }
};

// Cechy jednego zagranego taktu.
struct BarFeatures
{
    int bar = -1;
    int kicks = 0, snares = 0, toms = 0, tomsLateHalf = 0, hats = 0, cymbals = 0, hits = 0;
    float snareVel = 0.0f, meanVel = 0.0f;
    uint32_t kickMask = 0;      // kroki 16-tek, na których padła stopa
    uint32_t hitMask = 0;
    bool crashOnOne = false;
    float energy = 0.0f;        // 0..1
    bool fill = false;          // przejście na tomach = sygnał zmiany sekcji
};

// Analiza na żywo, bez alokacji, wywoływana z wątku audio.
// Uderzenia przypisujemy do najbliższej 16-tki, więc stopa zagrana
// minimalnie przed "raz" liczy się już do nowego taktu.
class DrumAnalyzer
{
public:
    DrumMap map;

    void reset()
    {
        acc[0] = acc[1] = {};
        last = {};
        smoothed = 0.0f;
        recentVel = 90.0f;
        feelPpq = 0.0f;
        silentBars = 0;
    }

    // ppq: pozycja uderzenia, barLen/stepsPerBar z metrum hosta.
    void hit(double ppq, int note, int velocity, double barLen, int stepsPerBar)
    {
        const auto part = map.classify(note);
        if (part == DrumPart::None)
            return;

        const double stepLen = barLen / stepsPerBar;
        const long long gstep = (long long) std::floor(ppq / stepLen + 0.5);
        const int bar = int(std::floor(double(gstep) / stepsPerBar));
        const int step = int(gstep - (long long) bar * stepsPerBar);

        // Dwa akumulatory na zmianę: uderzenie "na raz" zagrane odrobinę przed
        // granicą taktu nie kasuje jeszcze niezamkniętego poprzedniego taktu.
        auto& cur = acc[bar & 1];
        if (bar != cur.bar)
        {
            if (cur.bar >= 0 && bar < cur.bar)
                return; // bardzo spóźnione uderzenie – takt już dawno zamknięty
            cur = {};
            cur.bar = bar;
        }

        const float v = float(velocity);
        recentVel += 0.35f * (v - recentVel);
        cur.hits++;
        cur.meanVel += (v - cur.meanVel) / float(cur.hits);
        if (step < 32) cur.hitMask |= 1u << step;

        switch (part)
        {
            case DrumPart::Kick:
                cur.kicks++;
                if (step < 32) cur.kickMask |= 1u << step;
                break;
            case DrumPart::Snare:
                cur.snares++;
                cur.snareVel += (v - cur.snareVel) / float(cur.snares);
                break;
            case DrumPart::Tom:
                cur.toms++;
                if (step >= stepsPerBar / 2) cur.tomsLateHalf++;
                break;
            case DrumPart::HiHat: cur.hats++; break;
            case DrumPart::None: break;
            case DrumPart::Cymbal:
                cur.cymbals++;
                if (step == 0) cur.crashOnOne = true;
                break;
        }

        // "feel": średnie odchylenie hi-hatu i werbla od siatki (granie przed / za bitem)
        if (part == DrumPart::HiHat || part == DrumPart::Snare)
        {
            const float dev = float(ppq - double(gstep) * stepLen);
            feelPpq += 0.1f * (std::clamp(dev, -0.08f, 0.08f) - feelPpq);
        }
    }

    // Zamknięcie taktu `bar` (wywoływane, gdy zaczyna się bar+1).
    // reactivity 0..1: jak szybko energia podąża za bębniarzem.
    const BarFeatures& finishBar(int bar, float reactivity, int stepsPerBar)
    {
        auto& cur = acc[bar & 1];
        BarFeatures f = cur.bar == bar ? cur : BarFeatures{};
        f.bar = bar;
        const float sp = float(stepsPerBar);
        const float hatDensity = std::min(1.0f, float(f.hats) / sp);
        const float kickDensity = std::min(1.0f, float(f.kicks) / (sp * 0.375f));
        const float busy = std::min(1.0f, float(f.hits) / (sp * 2.0f));
        const float dyn = f.hits ? f.meanVel / 127.0f : 0.0f;
        const float snare = f.snares ? f.snareVel / 127.0f : dyn;
        float e = 0.30f * hatDensity + 0.15f * kickDensity + 0.20f * busy + 0.15f * dyn + 0.20f * snare;
        if (f.crashOnOne || f.cymbals > 2) e += 0.08f;
        f.energy = std::clamp(e, 0.0f, 1.0f);
        f.fill = f.toms >= 3 || f.tomsLateHalf >= 2;

        const float a = 0.25f + 0.7f * reactivity;
        smoothed += a * (f.energy - smoothed);
        silentBars = f.hits == 0 ? silentBars + 1 : 0;
        last = f;
        if (cur.bar == bar) cur = {};
        return last;
    }

    const BarFeatures& lastBar() const { return last; }
    const BarFeatures& barInProgress(int bar) const { static const BarFeatures none{}; return acc[bar & 1].bar == bar ? acc[bar & 1] : none; }
    float energy() const { return smoothed; }
    float dynamics() const { return recentVel / 127.0f; }
    float feel() const { return feelPpq; }
    int silent() const { return silentBars; }

    // Bębniarz gra przejście w tym takcie (tomy w drugiej połowie) – zostaw mu miejsce.
    bool fillInProgress(int bar) const { return acc[bar & 1].bar == bar && acc[bar & 1].tomsLateHalf >= 2; }

private:
    BarFeatures acc[2], last;
    float smoothed = 0.0f, recentVel = 90.0f, feelPpq = 0.0f;
    int silentBars = 0;
};

} // namespace sk
