#include "VocalEngine.h"
#include <algorithm>
#include <cmath>

namespace sk {

namespace {
    constexpr float pi = 3.14159265358979f;

    long long floorDiv(long long a, long long b)
    {
        const long long q = a / b;
        return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
    }

    // Interpolacja Hermite'a (4 punkty) z przycięciem indeksów do sampla.
    inline float readHermite(const float* a, int len, double pos)
    {
        const int i = int(pos);
        const float t = float(pos - double(i));
        auto at = [&](int k) { return a[k < 0 ? 0 : (k >= len ? len - 1 : k)]; };
        const float y0 = at(i - 1), y1 = at(i), y2 = at(i + 1), y3 = at(i + 2);
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * t + c2) * t + c1) * t + y1;
    }

    inline float softClip(float x)
    {
        const float a = std::abs(x);
        if (a < 0.7f)
            return x;
        const float y = 0.7f + 0.3f * std::tanh((a - 0.7f) / 0.3f);
        return x < 0.0f ? -y : y;
    }
}

VocalEngine::VocalEngine(SharedLink& l) : link(l)
{
    for (int i = 0; i <= 1024; ++i)
    {
        const float x = float(i) / 1024.0f;
        hann[i] = 0.5f - 0.5f * std::cos(2.0f * pi * x);
        // perkusyjne: szybki atak, wykładnicze wygaszanie, krótki fade na końcu
        const float att = std::min(1.0f, x / 0.04f);
        const float tail = std::min(1.0f, (1.0f - x) / 0.06f);
        perc[i] = att * std::exp(-4.0f * x) * tail;
    }
}

void VocalEngine::prepare(double sampleRate)
{
    sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    drums.reset();
    wasPlaying = false;
    decidedBar = INT32_MIN;
    numGrains = 0;
    hatAcc = swellTarget = swellNow = activity = 0.0f;
    spawnPhase = 0.0f;
    sinceHit = 1LL << 40;
}

float VocalEngine::rand01()
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return float(rng & 0xffffff) / float(0x1000000);
}

void VocalEngine::process(const BlockInfo& info, const VocalParams& p, const VocalHit* hits, int numHits,
                          const PoolSnapshot* snapshot, float* L, float* R)
{
    const int N = info.numSamples;
    if (N <= 0)
        return;
    std::fill(L, L + N, 0.0f);
    std::fill(R, R + N, 0.0f);
    pool = snapshot;
    drums.map = p.drums;
    auto& group = link.group(p.group);

    const bool playing = info.playing && info.ppqPerSample > 0.0;
    gridSynced = playing && p.sync != 0;
    beatSec = info.ppqPerSample > 0.0 ? std::clamp(1.0 / (info.ppqPerSample * sr), 0.15, 3.0) : 0.5;

    int cursor = 0, hi = 0;
    auto hitAt = [&](int i) { return std::clamp(hits[i].offset, 0, N - 1); };

    if (! playing)
    {
        wasPlaying = false;
        decidedBar = INT32_MIN;
        follower.idle(p.harmony, group);
        status.bar = -1;
        for (; hi < numHits; ++hi)
        {
            const int at = std::max(cursor, hitAt(hi));
            render(cursor, at, p, L, R);
            cursor = at;
            onHit(hits[hi], p, info, cursor);
        }
    }
    else
    {
        const double p0 = info.ppqStart;
        const double p1 = p0 + N * info.ppqPerSample;
        const int spb = std::clamp(info.stepsPerBar, 1, 32);
        const double stepLen = info.barLenPpq / spb;

        if (! wasPlaying)
        {
            drums.reset();
            decidedBar = INT32_MIN;
        }
        else if (std::abs(p0 - expectedPpq) > std::max(0.05, 2.0 * (p1 - p0)))
        {
            decidedBar = INT32_MIN; // skok kursora albo pętla
        }
        wasPlaying = true;
        expectedPpq = p1;

        const double origin = info.barOrigin - std::floor(info.barOrigin / info.barLenPpq) * info.barLenPpq;
        long long gs = (long long) std::ceil((p0 - origin) / stepLen - 1e-9);

        for (;;)
        {
            const double t = origin + double(gs) * stepLen;
            const bool stepInBlock = t < p1;
            // uderzenia przed krokiem siatki trafiają do analizy wcześniej (jak w Piano/Bass)
            if (hi < numHits && (! stepInBlock || hits[hi].ppq < t))
            {
                const int at = std::max(cursor, hitAt(hi));
                render(cursor, at, p, L, R);
                cursor = at;
                drums.hit(hits[hi].ppq - origin, hits[hi].note, hits[hi].velocity, info.barLenPpq, spb);
                onHit(hits[hi], p, info, cursor);
                ++hi;
                continue;
            }
            if (! stepInBlock)
                break;

            const int at = std::clamp(int((t - p0) / info.ppqPerSample), cursor, N);
            render(cursor, at, p, L, R);
            cursor = at;

            const int bar = int(floorDiv(gs, spb));
            const int step = int(gs - (long long) bar * spb);

            if (bar != decidedBar)
            {
                const bool continuous = decidedBar == bar - 1;
                const BarFeatures f = drums.finishBar(bar - 1, p.reactivity, spb);
                follower.decide(bar, continuous, f.fill, p.harmony, group);
                decidedBar = bar;
                status.bar = bar;
            }
            else if (step < spb / 2)
            {
                follower.refresh(bar, group);
            }
            // chmura na siatce (po decyzji o akordzie taktu): na każdej 8-ce / 16-tce tyle ziaren, ile wypada z gęstości
            if (gridSynced && p.mode == VocalParams::Cloud && step % (p.sync == 1 ? 2 : 1) == 0 && rateNow > 0.0f)
            {
                const float perStep = rateNow * float(stepLen * (p.sync == 1 ? 2.0 : 1.0) / info.ppqPerSample / sr);
                gridCarry += perStep;
                while (gridCarry >= 1.0f)
                {
                    gridCarry -= 1.0f;
                    spawn(Voice::Cloud, 0, 1.0f, p);
                }
            }
            ++gs;
        }
    }
    render(cursor, N, p, L, R);

    for (int i = 0; i < N; ++i)
    {
        L[i] = softClip(L[i]);
        R[i] = softClip(R[i]);
    }

    uint64_t oldest = pool ? pool->generation : 0;
    for (int i = 0; i < numGrains; ++i)
        oldest = std::min(oldest, grains[i].gen);
    oldestGen = oldest;

    status.playing = playing;
    status.chordRoot = follower.chord.root;
    status.chordQuality = follower.chord.quality;
    status.section = follower.section;
    status.chordPos = follower.positionInProgression();
    status.followingPiano = follower.followingPiano;
    status.harmonyWord = follower.harmony.pack();
    status.activeGrains = numGrains;
    status.hatDensity = hatDensityNow;
    status.swell = swellNow;
    status.activity = activity;
    status.grainsPerSecond = rateNow;
    status.energy = playing ? std::max(drums.energy(), 0.0f) : liveVel * activity;
}

void VocalEngine::onHit(const VocalHit& h, const VocalParams& p, const BlockInfo&, int)
{
    const auto part = p.drums.classify(h.note);
    if (part == DrumPart::None || h.velocity <= 0)
        return;
    const float vel = float(std::clamp(h.velocity, 1, 127)) / 127.0f;
    sinceHit = 0;
    liveVel += 0.3f * (vel - liveVel);
    activity = std::max(activity, 0.5f); // bębniarz wchodzi: chmura od razu słyszalna

    switch (part)
    {
        case DrumPart::HiHat:
            hatAcc += 1.0f;
            if (rand01() < 0.5f * p.pulse) spawn(Voice::Hat, 0, vel, p);
            break;
        case DrumPart::Kick:
            if (rand01() < p.pulse) spawn(Voice::Kick, 0, vel, p);
            break;
        case DrumPart::Snare:
            if (rand01() < p.pulse) spawn(Voice::Snare, 0, vel, p);
            break;
        case DrumPart::Tom:
            swellTarget = std::min(1.0f, swellTarget + 0.22f);
            if (rand01() < p.pulse) spawn(Voice::Cloud, 0, vel, p);
            break;
        case DrumPart::Cymbal:
        {
            // talerz (zwykle "raz" po przejściu): wybuch ziaren i koniec puchnięcia
            const int n = int(std::lround(3.0f * p.pulse + 8.0f * p.swell * std::max(swellNow, swellTarget)));
            for (int i = 0; i < n; ++i)
                spawn(Voice::Burst, int(rand01() * 0.03f * float(sr)), vel, p);
            swellTarget *= 0.4f;
            break;
        }
        case DrumPart::None:
            break;
    }
}

void VocalEngine::render(int from, int to, const VocalParams& p, float* L, float* R)
{
    const float level0 = std::pow(10.0f, p.gainDb / 20.0f);
    for (int c = from; c < to;)
    {
        const int n = std::min(32, to - c);
        const float dt = float(n) / float(sr);
        const float beat = float(beatSec);

        // gęstość hi-hatu: zanikający licznik uderzeń (stała czasowa zależna od reaktywności)
        const float hatTau = beat * (2.0f - 1.5f * p.reactivity);
        hatAcc *= std::exp(-dt / hatTau);
        const float hatsPerBeat = hatAcc * beat / hatTau;
        const float hatDensity = std::clamp(hatsPerBeat / 4.0f, 0.0f, 1.0f); // 16-tki = 1
        hatDensityNow = hatDensity;

        swellTarget *= std::exp(-dt / (2.0f * beat));
        const float swTau = swellTarget > swellNow ? 0.08f : 0.5f;
        swellNow += (swellTarget - swellNow) * (1.0f - std::exp(-dt / swTau));

        sinceHit += n;
        const double hold = std::max(1.5, 4.0 * beatSec) * sr; // takt bez bębnów -> wygaszanie
        const float actTarget = (p.playWithoutDrums || double(sinceHit) < hold) ? 1.0f : 0.0f;
        const float actTau = actTarget > activity ? 0.01f : 0.8f;
        const float actPrev = activity;
        activity += (actTarget - activity) * (1.0f - std::exp(-dt / actTau));
        if (actTarget == 0.0f && activity < 1e-3f)
            activity = 0.0f; // pełna cisza zamiast bardzo cichego ogona

        const float swellAmt = swellNow * p.swell;
        const float hatFactor = 1.0f + p.hatToDensity * (0.3f + 2.7f * hatDensity - 1.0f);
        float rate = p.density * hatFactor * (1.0f + 5.0f * swellAmt);
        if (activity < 0.02f || pool == nullptr || pool->count == 0)
            rate = 0.0f;
        rateNow = rate;

        scanPhase += p.scan * 0.2f * dt;
        scanPhase -= std::floor(scanPhase);

        if (p.mode == VocalParams::Choir)
            stepChoir(n, p, swellAmt);

        // nowe ziarna chmury: akumulator fazy z losowym progiem (mniej zbite niż Poisson);
        // z synchronizacją ziarna startują na siatce tempa (w process), nie tutaj
        const float perSample = (p.mode == VocalParams::Cloud && ! gridSynced) ? rate / float(sr) : 0.0f;
        if (perSample > 0.0f)
            for (int i = 0; i < n; ++i)
            {
                spawnPhase += perSample;
                if (spawnPhase >= spawnThreshold)
                {
                    spawnPhase -= spawnThreshold;
                    spawnThreshold = 0.5f + rand01();
                    spawn(Voice::Cloud, i, 1.0f, p);
                }
            }

        float* l = L + c;
        float* r = R + c;
        for (int gi = 0; gi < numGrains;)
        {
            auto& g = grains[gi];
            const float* a = g.s->audio.data();
            const int len = g.s->length();
            const float winScale = 1024.0f / float(g.len);
            bool dead = false;
            for (int k = g.delay; k < n; ++k)
            {
                if (g.age >= g.len)
                {
                    dead = true;
                    break;
                }
                const float x = float(g.age) * winScale;
                const int wi = std::min(1023, int(x));
                const float wf = x - float(wi);
                const float wh = hann[wi] + wf * (hann[wi + 1] - hann[wi]);
                const float wp = perc[wi] + wf * (perc[wi + 1] - perc[wi]);
                const float w = wh + p.shape * (wp - wh);
                const float v = readHermite(a, len, g.pos) * w;
                l[k] += v * g.gL;
                r[k] += v * g.gR;
                g.pos += g.rate;
                ++g.age;
            }
            g.delay = std::max(0, g.delay - n);
            if (dead || g.age >= g.len)
                grains[gi] = grains[--numGrains];
            else
                ++gi;
        }

        // głośność: dynamika bębniarza, puchnięcie, bramka aktywności (płynnie w obrębie kawałka)
        const float dyn = 1.0f + p.dynamics * (0.35f + 0.9f * liveVel - 1.0f);
        const float lv = level0 * dyn * (1.0f + 0.6f * swellAmt);
        for (int k = 0; k < n; ++k)
        {
            const float a = actPrev + (activity - actPrev) * float(k + 1) / float(n);
            l[k] *= lv * a;
            r[k] *= lv * a;
        }
        c += n;
    }
}

int VocalEngine::pickSample(const VocalParams& p)
{
    if (pool == nullptr || pool->count == 0)
        return -1;
    float w[PoolSnapshot::maxSamples];
    float total = 0.0f, fresh = 1.0f;
    const float decay = 1.0f - 0.85f * std::clamp(p.freshness, 0.0f, 1.0f);
    for (int i = pool->count - 1; i >= 0; --i)
    {
        const auto* s = pool->items[i];
        w[i] = s->length() >= 64 ? std::max(0.0f, s->weight.load(std::memory_order_relaxed)) * fresh : 0.0f;
        total += w[i];
        fresh *= decay;
    }
    if (total <= 0.0f)
        return -1;
    float u = rand01() * total;
    for (int i = 0; i < pool->count; ++i)
    {
        u -= w[i];
        if (u <= 0.0f && w[i] > 0.0f)
            return i;
    }
    for (int i = pool->count - 1; i >= 0; --i)
        if (w[i] > 0.0f)
            return i;
    return -1;
}

float VocalEngine::pickTarget(float src, Voice v, const VocalParams& p)
{
    const auto ch = follower.chord;
    const auto sh = shapeOf(ch.quality);
    int tones[4] = { 0, sh.third, sh.fifth, sh.seventh };
    int numTones = sh.seventh >= 0 ? 4 : 3;
    float centre = src + 12.0f * float(std::clamp(p.octave, -2, 2));

    switch (v)
    {
        case Voice::Kick:  numTones = 1; break;                                   // pryma
        case Voice::Snare: tones[1] = sh.fifth; numTones = 2; break;              // pryma / kwinta
        case Voice::Hat:   tones[0] = sh.third; tones[1] = sh.seventh >= 0 ? sh.seventh : sh.fifth;
                           numTones = 2; centre += 12.0f; break;                 // jasne składniki wyżej
        default: break;
    }

    const float range = 7.0f + 12.0f * p.spread;
    const float soft = 1.5f + 8.0f * p.spread;
    float cand[64], wts[64], total = 0.0f;
    int nc = 0;
    for (int m = int(std::floor(centre - range)); m <= int(std::ceil(centre + range)) && nc < 64; ++m)
    {
        const int rel = mod12(m - ch.root);
        for (int t = 0; t < numTones; ++t)
            if (rel == tones[t])
            {
                float w = std::exp(-std::abs(float(m) - centre) / soft);
                if (rel == 0) w *= 1.3f;
                cand[nc] = float(m);
                wts[nc++] = w;
                total += w;
                break;
            }
    }
    float target = std::round(centre);
    if (nc > 0)
    {
        float u = rand01() * total;
        target = cand[nc - 1];
        for (int i = 0; i < nc; ++i)
            if ((u -= wts[i]) <= 0.0f) { target = cand[i]; break; }
    }
    while (target - src > 24.0f) target -= 12.0f;
    while (src - target > 24.0f) target += 12.0f;
    return target;
}

void VocalEngine::spawn(Voice v, int delay, float velocity, const VocalParams& p)
{
    if (numGrains >= maxGrains)
        return;
    const int si = pickSample(p);
    if (si < 0)
        return;
    const VocalSample* s = pool->items[si];
    const int len = s->length();

    const float swellAmt = swellNow * p.swell;
    float lenSec = std::clamp(p.grainMs, 10.0f, 2000.0f) / 1000.0f * (1.0f + 0.8f * swellAmt);
    if (v == Voice::Hat) lenSec *= 0.5f;
    if (v == Voice::Burst) lenSec *= 1.5f;
    int lenSamples = std::clamp(int(lenSec * float(sr)), 64, int(4.0 * sr));

    // pozycja: środek + rozrzut + skan; z preferencją fragmentów z wysokością (samogłosek)
    double pos = 0.0;
    for (int attempt = 0; attempt < 6; ++attempt)
    {
        float c = p.position + scanPhase + p.scatter * (rand01() - 0.5f);
        c -= std::floor(c);
        pos = double(c) * double(len - 1);
        if (s->voicedAt(pos) || rand01() >= p.voiced)
            break;
    }

    const float local = s->midiAt(pos);
    const float src = local >= 0.0f ? s->medianMidi + p.retune * (local - s->medianMidi) : s->medianMidi;
    const float target = pickTarget(src, v, p);
    const float overlap = std::max(1.0f, std::max(rateNow, p.density) * float(lenSamples) / float(sr));
    const float pan = (rand01() * 2.0f - 1.0f) * std::clamp(p.width, 0.0f, 1.0f);
    emit(s, pos, src, target, lenSamples, 0.9f * velocity / std::sqrt(overlap), pan, rand01() < p.reverse, delay);
}

void VocalEngine::emit(const VocalSample* s, double pos, float src, float target, int lenSamples, float gain,
                       float pan, bool reverse, int delay)
{
    if (numGrains >= maxGrains)
        return;
    const int len = s->length();
    const double ratio = std::pow(2.0, double(target - src) / 12.0) * s->sampleRate / sr;
    double span = double(lenSamples) * ratio;
    if (span > double(len - 4))
    {
        lenSamples = int(double(len - 4) / ratio);
        span = double(lenSamples) * ratio;
    }
    if (lenSamples < 64)
        return;

    auto& g = grains[numGrains];
    if (reverse)
    {
        g.pos = std::clamp(pos + span, span + 1.0, double(len - 3));
        g.rate = -ratio;
    }
    else
    {
        g.pos = std::clamp(pos, 1.0, std::max(1.0, double(len - 3) - span));
        g.rate = ratio;
    }
    const float angle = (std::clamp(pan, -1.0f, 1.0f) + 1.0f) * pi * 0.25f;
    g.s = s;
    g.gen = pool->generation;
    g.len = lenSamples;
    g.age = 0;
    g.delay = std::max(0, delay);
    g.gL = gain * std::cos(angle) * 1.41421356f;
    g.gR = gain * std::sin(angle) * 1.41421356f;
    ++numGrains;
    s->grainsSpawned.fetch_add(1, std::memory_order_relaxed);
    ++diagnostics.pitchClass[mod12(int(std::lround(target)))];
    ++diagnostics.spawned;
}

// ---------------------------------------------------------------- chór

void VocalEngine::retuneChoir(const VocalParams& p)
{
    // Ciasny układ akordu: każdy głos dostaje jeden składnik, najbliżej naturalnej
    // wysokości swojego sampla (najmniej przestrajania = najmniej "chipmunka").
    const auto ch = follower.chord;
    const auto sh = shapeOf(ch.quality);
    const int tones[4] = { 0, sh.third, sh.fifth, sh.seventh >= 0 ? sh.seventh : 12 };
    for (int v = 0; v < maxVoices; ++v)
    {
        auto& vc = voices[v];
        if (vc.s == nullptr)
            continue;
        const float centre = vc.s->medianMidi + 12.0f * float(std::clamp(p.octave, -2, 2)) + (v >= 4 ? 12.0f : 0.0f);
        const int tone = tones[v % 4];
        float best = centre;
        float bestD = 1e9f;
        for (int m = int(centre) - 12; m <= int(centre) + 12; ++m)
            if (mod12(m - ch.root) == mod12(tone) && std::abs(float(m) - centre) < bestD)
            {
                bestD = std::abs(float(m) - centre);
                best = float(m);
            }
        vc.target = best;
    }
    choirRoot = ch.root;
    choirQuality = ch.quality;
}

void VocalEngine::assignVoices(const VocalParams& p)
{
    // Każdy głos to jeden "śpiewak": stały sampel i własna głowica odczytu.
    for (int v = 0; v < maxVoices; ++v)
    {
        auto& vc = voices[v];
        bool valid = false;
        if (vc.s != nullptr && pool != nullptr)
            for (int i = 0; i < pool->count; ++i)
                valid = valid || (pool->items[i] == vc.s && vc.s->weight.load(std::memory_order_relaxed) > 0.0f);
        if (valid && vc.gen != 0)
            continue;
        const int si = pickSample(p);
        vc.s = si >= 0 ? pool->items[si] : nullptr;
        vc.gen = pool ? pool->generation : 0;
        if (vc.s != nullptr)
        {
            float c = p.position + 0.17f * float(v);
            vc.pos = double(c - std::floor(c)) * double(vc.s->length() - 1);
        }
        vc.timer = int(rand01() * 0.5f * p.grainMs * 0.001f * float(sr)); // głosy nie startują równo
    }
    choirGeneration = pool ? pool->generation : 0;
    retuneChoir(p);
}

void VocalEngine::stepChoir(int n, const VocalParams& p, float swellAmt)
{
    if (pool == nullptr || pool->count == 0 || activity < 0.02f)
        return;
    if (pool->generation != choirGeneration)
        assignVoices(p);
    if (follower.chord.root != choirRoot || follower.chord.quality != choirQuality)
        retuneChoir(p);

    const auto sh = shapeOf(follower.chord.quality);
    // 3 albo 4 głosy (septyma), gęsty hi-hat albo przejście dokłada górny głos (oktawa wyżej)
    int count = sh.seventh >= 0 ? 4 : 3;
    if (hatDensityNow * p.hatToDensity > 0.35f || swellAmt > 0.3f)
        count += 1;
    const float lenSec = std::clamp(p.grainMs, 40.0f, 1000.0f) / 1000.0f * (1.0f + 0.5f * swellAmt);
    const int lenSamples = int(lenSec * float(sr));
    const int hop = std::max(32, lenSamples / 2); // dwa nakładające się ziarna Hanna = równy, ciągły dźwięk
    // tempo przesuwu głowicy: Skan 0 = 30 % szybkości mowy (rozciągnięty śpiew), 100 % = tempo oryginału
    const double speed = 0.3 + 0.7 * double(p.scan);

    for (int v = 0; v < count && v < maxVoices; ++v)
    {
        auto& vc = voices[v];
        if (vc.s == nullptr)
            continue;
        vc.timer -= n;
        while (vc.timer <= 0)
        {
            const int delay = n + vc.timer;
            vc.timer += hop;
            const VocalSample* s = vc.s;
            const int len = s->length();
            vc.pos += double(hop) * speed * s->sampleRate / sr;
            if (vc.pos >= len - 2 || vc.pos < 1.0)
                vc.pos = vc.pos < 1.0 ? double(len - 2) : 1.0;
            // spółgłoska / cisza: przeskocz do najbliższego fragmentu z wysokością
            if (! s->voicedAt(vc.pos) && rand01() < p.voiced)
                for (int k = 1; k < 400; ++k)
                {
                    const double q = std::fmod(vc.pos + double(k * s->hop), double(len - 2));
                    if (s->voicedAt(q)) { vc.pos = q; break; }
                }
            const double jitter = double(p.scatter) * (rand01() - 0.5f) * 0.5 * double(lenSamples);
            const double at = std::clamp(vc.pos + jitter, 1.0, double(len - 2));
            const float local = s->midiAt(at);
            const float src = local >= 0.0f ? s->medianMidi + p.retune * (local - s->medianMidi) : s->medianMidi;
            const float spreadPan = count > 1 ? (float(v) / float(count - 1)) * 2.0f - 1.0f : 0.0f;
            emit(s, at, src, voices[v].target, lenSamples, 0.75f / std::sqrt(float(count)),
                 spreadPan * std::clamp(p.width, 0.0f, 1.0f), rand01() < p.reverse, std::clamp(delay, 0, n - 1));
        }
    }
    rateNow = float(count) * float(sr) / float(hop);
}

} // namespace sk
