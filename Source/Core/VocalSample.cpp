#include "VocalSample.h"
#include <algorithm>
#include <cmath>

namespace sk {

namespace {
    constexpr double analysisRate = 11025.0;
    constexpr float minHz = 70.0f, maxHz = 1000.0f;
    constexpr float yinThreshold = 0.15f;

    float median(std::vector<float> v)
    {
        if (v.empty()) return 0.0f;
        const size_t m = v.size() / 2;
        std::nth_element(v.begin(), v.begin() + long(m), v.end());
        return v[m];
    }
}

void analysePitch(VocalSample& s)
{
    const int n = s.length();
    const int dec = std::max(1, int(std::lround(s.sampleRate / analysisRate)));
    const double fs = s.sampleRate / dec;
    const int hopDec = std::max(1, 256 / dec);
    s.hop = hopDec * dec;

    // Decymacja z prostym filtrem (dwa przebiegi średniej ruchomej szerokości `dec`).
    std::vector<float> x(size_t(n / dec + 1), 0.0f);
    {
        std::vector<float> a(s.audio);
        for (int pass = 0; pass < 2 && dec > 1; ++pass)
        {
            double acc = 0.0;
            std::vector<float> b(a.size());
            for (int i = 0; i < n; ++i)
            {
                acc += a[size_t(i)];
                if (i >= dec) acc -= a[size_t(i - dec)];
                b[size_t(i)] = float(acc / dec);
            }
            a.swap(b);
        }
        for (size_t i = 0; i < x.size(); ++i)
        {
            const size_t src = std::min(size_t(n > 0 ? n - 1 : 0), i * size_t(dec));
            x[i] = n > 0 ? a[src] : 0.0f;
        }
    }

    const int tauMin = std::max(2, int(fs / maxHz));
    const int tauMax = int(fs / minHz) + 1;
    const int W = std::max(tauMax, int(fs * 0.02));
    const int frameLen = W + tauMax + 1;
    const int nx = int(x.size());
    const int frames = n / s.hop + 1;
    s.pitch.assign(size_t(frames), -1.0f);

    // Próg ciszy względem głośnych fragmentów sampla.
    std::vector<float> rms(size_t(frames), 0.0f);
    for (int i = 0; i < frames; ++i)
    {
        const int start = i * hopDec - frameLen / 2;
        double e = 0.0;
        int cnt = 0;
        for (int j = std::max(0, start); j < std::min(nx, start + W); ++j, ++cnt)
            e += double(x[size_t(j)]) * x[size_t(j)];
        rms[size_t(i)] = cnt ? float(std::sqrt(e / cnt)) : 0.0f;
    }
    std::vector<float> sorted(rms);
    std::sort(sorted.begin(), sorted.end());
    const float loud = sorted.empty() ? 0.0f : sorted[size_t(float(sorted.size() - 1) * 0.9f)];
    const float gate = std::max(1e-4f, 0.12f * loud);

    std::vector<float> d(size_t(tauMax + 1));
    for (int i = 0; i < frames; ++i)
    {
        const int start = i * hopDec - frameLen / 2;
        if (start < 0 || start + frameLen > nx || rms[size_t(i)] < gate)
            continue;
        const float* f = x.data() + start;

        d[0] = 0.0f;
        double running = 0.0;
        int found = -1;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            double sum = 0.0;
            for (int j = 0; j < W; ++j)
            {
                const float diff = f[j] - f[j + tau];
                sum += double(diff) * diff;
            }
            running += sum;
            d[size_t(tau)] = running > 0.0 ? float(sum * tau / running) : 1.0f;
        }
        for (int tau = tauMin; tau < tauMax; ++tau)
            if (d[size_t(tau)] < yinThreshold)
            {
                while (tau + 1 < tauMax && d[size_t(tau + 1)] < d[size_t(tau)])
                    ++tau;
                found = tau;
                break;
            }
        if (found < 0)
            continue;

        // interpolacja paraboliczna minimum
        double t = found;
        if (found > 1 && found < tauMax)
        {
            const double a = d[size_t(found - 1)], b = d[size_t(found)], c = d[size_t(found + 1)];
            const double den = a - 2.0 * b + c;
            if (std::abs(den) > 1e-12)
                t += std::clamp(0.5 * (a - c) / den, -1.0, 1.0);
        }
        const float hz = float(fs / t);
        if (hz >= minHz && hz <= maxHz)
            s.pitch[size_t(i)] = hzToMidi(hz);
    }

    // Wygładzenie medianą z 5 ramek (pojedyncze skoki oktawowe znikają).
    std::vector<float> smooth(s.pitch);
    for (int i = 2; i + 2 < frames; ++i)
    {
        if (s.pitch[size_t(i)] < 0.0f)
            continue;
        float w[5];
        int k = 0;
        for (int j = -2; j <= 2; ++j)
            if (s.pitch[size_t(i + j)] >= 0.0f)
                w[k++] = s.pitch[size_t(i + j)];
        for (int a = 1; a < k; ++a) // sortowanie przez wstawianie (najwyżej 5 elementów)
            for (int b = a; b > 0 && w[b - 1] > w[b]; --b)
                std::swap(w[b - 1], w[b]);
        smooth[size_t(i)] = w[k / 2];
    }
    s.pitch.swap(smooth);

    std::vector<float> voiced;
    for (float p : s.pitch)
        if (p >= 0.0f) voiced.push_back(p);
    s.voicedFraction = frames ? float(voiced.size()) / float(frames) : 0.0f;
    s.medianMidi = voiced.empty() ? 60.0f : median(voiced);
}

std::unique_ptr<VocalSample> makeVocalSample(std::vector<float> mono, double sampleRate, std::string name, std::string path)
{
    auto s = std::make_unique<VocalSample>();
    s->name = std::move(name);
    s->path = std::move(path);
    s->sampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    s->audio = std::move(mono);

    // usuń składową stałą
    if (! s->audio.empty())
    {
        double mean = 0.0;
        for (float v : s->audio) mean += v;
        mean /= double(s->audio.size());
        for (float& v : s->audio) v -= float(mean);
    }

    analysePitch(*s);

    // Normalizacja: RMS głośnych fragmentów do ok. -20 dBFS, żeby sample mieszały się równo.
    const int block = 1024;
    std::vector<float> levels;
    float peak = 0.0f;
    for (size_t i = 0; i < s->audio.size(); i += block)
    {
        double e = 0.0;
        const size_t end = std::min(s->audio.size(), i + block);
        for (size_t j = i; j < end; ++j)
        {
            e += double(s->audio[j]) * s->audio[j];
            peak = std::max(peak, std::abs(s->audio[j]));
        }
        levels.push_back(float(std::sqrt(e / double(end - i))));
    }
    std::sort(levels.begin(), levels.end());
    const float loud = levels.empty() ? 0.0f : levels[size_t(float(levels.size() - 1) * 0.8f)];
    if (loud > 1e-5f)
    {
        float gain = 0.1f / loud;
        if (peak * gain > 1.5f)
            gain = 1.5f / peak;
        for (float& v : s->audio) v *= gain;
    }
    return s;
}

} // namespace sk
