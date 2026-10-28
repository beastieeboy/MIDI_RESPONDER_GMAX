#pragma once
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace sk {

// Jeden sampel wokalu wrzucony do "pieca". Dane audio są niezmienne po wczytaniu,
// zmienia się tylko waga (z GUI) i licznik ziaren (z wątku audio) – oba to atomiki.
struct VocalSample
{
    std::string name, path;
    std::vector<float> audio;     // mono, znormalizowane do wspólnej głośności
    double sampleRate = 48000.0;

    // Analiza wysokości (YIN) co `hop` próbek: nuta MIDI (ułamkowa), -1 = bezdźwięczne / cisza.
    int hop = 256;
    std::vector<float> pitch;
    float medianMidi = 60.0f;     // mediana wysokości dźwięcznych fragmentów (nuta MIDI)
    float voicedFraction = 0.0f;  // jaka część sampla ma wyraźną wysokość

    std::atomic<float> weight { 1.0f };          // udział w mieszance (0..1)
    mutable std::atomic<uint32_t> grainsSpawned { 0 };   // do "żaru" w GUI

    int length() const { return int(audio.size()); }

    // Wysokość w miejscu `pos` (próbki) jako nuta MIDI; <0 gdy bezdźwięczne.
    float midiAt(double pos) const
    {
        const long long i = (long long) (pos / hop + 0.5);
        return i >= 0 && i < (long long) pitch.size() ? pitch[size_t(i)] : -1.0f;
    }
    bool voicedAt(double pos) const { return midiAt(pos) >= 0.0f; }
};

// Analiza wysokości i normalizacja. Wywoływane poza wątkiem audio (wątek wczytywania).
// `mono` może mieć dowolną długość; dane zostają przeniesione do sampla.
std::unique_ptr<VocalSample> makeVocalSample(std::vector<float> mono, double sampleRate, std::string name,
                                             std::string path);

// Wykrywanie f0 metodą YIN na sygnale zdecymowanym do ~11 kHz (zakres 70–1000 Hz).
void analysePitch(VocalSample& s);

inline float hzToMidi(float hz) { return 69.0f + 12.0f * std::log2(hz / 440.0f); }

} // namespace sk
