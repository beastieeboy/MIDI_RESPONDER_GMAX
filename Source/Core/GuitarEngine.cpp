#include "GuitarEngine.h"
#include "GuitarPhraseData.h"
#include <algorithm>
#include <cmath>

namespace sk {

namespace {
    int popcount(uint32_t x) { return __builtin_popcount(x); }

    long long floorDiv(long long a, long long b)
    {
        const long long q = a / b;
        return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
    }

    const Progression& prog(int i) { return progressions[std::clamp(i, 0, numProgressions - 1)]; }
    bool isMinorQuality(int q) { return q == Min || q == Min7 || q == Dim; }

    void unpackBar(uint64_t w, int& bar, int& section, int& pos)
    {
        bar = int(int32_t(uint32_t(w >> 32)));
        section = int((w >> 8) & 255);
        pos = int(w & 255);
    }

    // Krótkie odstępy w ćwierćnutach (przy 120 BPM 0.01 ppq = 5 ms).
    constexpr double ksLead = 0.0;     // keyswitch w tej samej chwili co nuta, ale przed nią w kolejce
    constexpr double ksHold = 0.06;    // jak długo trzymamy keyswitch slajdu po wejściu nuty
    constexpr double legatoOverlap = 0.03;
    constexpr double fallLength = 0.2; // ile trwa zjazd po keyswitchu A1
}

const PassageSet& guitarPassages()
{
    static const PassageSet s = []
    {
        float lo = 1.0f, hi = 0.0f;
        for (const auto& p : data::guitarPassages)
        {
            lo = std::min(lo, p.energy);
            hi = std::max(hi, p.energy);
        }
        return PassageSet { data::guitarPassages, data::guitarNotes, data::guitarPassageCount, lo, hi };
    }();
    return s;
}

GuitarEngine::GuitarEngine(SharedLink& l) : link(l)
{
    set = &guitarPassages(); // inicjalizacja poza wątkiem audio
}

void GuitarEngine::prepare()
{
    drums.reset();
    wasPlaying = false;
    decidedBar = INT32_MIN;
    numPlanned = 0;
    queued = 0;
    numSounding = 0;
    passage = -1;
    sectionPassage[0] = sectionPassage[1] = -1;
    currentArt = -1;
    lastPitch = -1;
    playingBar = false;
}

float GuitarEngine::rand01()
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return float(rng & 0xffffff) / float(0x1000000);
}

bool GuitarEngine::alive(int64_t hb) const
{
    return hb != 0 && SharedLink::nowMs() - hb < SharedLink::aliveMs;
}

void GuitarEngine::heartbeat(const GuitarParams& p)
{
    auto& g = link.group(p.group);
    status.pianoAlive = alive(g.pianoHeartbeat.load(std::memory_order_relaxed));
    status.bassAlive = alive(g.bassHeartbeat.load(std::memory_order_relaxed));
    status.linkShared = link.isShared();
    const uint32_t w = g.harmony.load(std::memory_order_relaxed);
    status.harmonyWord = HarmonySettings::unpack(w).valid ? w : p.harmony.pack();
}

void GuitarEngine::push(double ppq, uint8_t st, uint8_t d1, uint8_t d2)
{
    if (queued < queueSize)
        queue[queued++] = { ppq, seqCounter++, st, d1, d2 };
}

void GuitarEngine::flush(const BlockInfo& info, double until, MidiOutBuffer& out)
{
    // wybieramy zdarzenia z tego bloku w kolejności czasu (a przy remisie – dodania)
    int idx[queueSize];
    int n = 0;
    for (int i = 0; i < queued; ++i)
        if (queue[i].ppq < until)
            idx[n++] = i;
    std::sort(idx, idx + n, [this](int a, int b)
    {
        return queue[a].ppq < queue[b].ppq || (queue[a].ppq == queue[b].ppq && queue[a].seq < queue[b].seq);
    });

    for (int k = 0; k < n; ++k)
    {
        const auto& e = queue[idx[k]];
        const int off = std::clamp(int((e.ppq - info.ppqStart) / info.ppqPerSample), 0, info.numSamples - 1);
        out.add(off, e.status, e.d1, e.d2);
        const uint8_t type = e.status & 0xf0;
        if (type == 0x90 && e.d2 > 0)
        {
            if (numSounding < 16)
                sounding[numSounding++] = e.d1;
        }
        else if (type == 0x80 || type == 0x90)
        {
            for (int s = 0; s < numSounding; ++s)
                if (sounding[s] == e.d1)
                {
                    sounding[s] = sounding[--numSounding];
                    break;
                }
        }
        else if (type == 0xE0)
            bent = ! (e.d1 == 0 && e.d2 == 64);
        else if (type == 0xB0 && e.d1 == 1)
            vibrato = e.d2 != 0;
    }

    int w = 0;
    for (int i = 0; i < queued; ++i)
        if (! (queue[i].ppq < until))
            queue[w++] = queue[i];
    queued = w;
}

void GuitarEngine::allOff(const GuitarParams&, MidiOutBuffer& out)
{
    for (int s = 0; s < numSounding; ++s)
        out.add(0, uint8_t(0x80 | channel), sounding[s], 0);
    numSounding = 0;
    if (bent)
        out.add(0, uint8_t(0xE0 | channel), 0, 64);
    if (vibrato)
        out.add(0, uint8_t(0xB0 | channel), 1, 0);
    bent = vibrato = false;
    out.add(0, uint8_t(0xB0 | channel), 123, 0);
    queued = 0;
    currentArt = -1;
    playingBar = false;
    passage = -1;
}

void GuitarEngine::process(const BlockInfo& info, const GuitarParams& p, const DrumHit* hits, int numHits, MidiOutBuffer& out)
{
    if (! info.playing || info.ppqPerSample <= 0.0 || info.numSamples <= 0)
    {
        if (wasPlaying)
            allOff(p, out);
        wasPlaying = false;
        decidedBar = INT32_MIN;
        status.mode = 0;
        return;
    }

    const uint8_t newChannel = uint8_t(std::clamp(p.midiChannel, 1, 16) - 1);
    if (newChannel != channel && wasPlaying)
        allOff(p, out);
    channel = newChannel;

    const double p0 = info.ppqStart;
    const double p1 = p0 + info.numSamples * info.ppqPerSample;
    const int spb = std::clamp(info.stepsPerBar, 1, 32);
    const double stepLen = info.barLenPpq / spb;

    if (! wasPlaying)
    {
        drums.reset();
        decidedBar = INT32_MIN;
    }
    else if (std::abs(p0 - expectedPpq) > std::max(0.05, 2.0 * (p1 - p0)))
    {
        allOff(p, out); // skok kursora albo pętla
        decidedBar = INT32_MIN;
    }
    wasPlaying = true;
    expectedPpq = p1;
    drums.map = p.drums;

    auto& g = link.group(p.group);
    pianoAlive = alive(g.pianoHeartbeat.load(std::memory_order_relaxed));
    bassAlive = alive(g.bassHeartbeat.load(std::memory_order_relaxed));

    const double origin = info.barOrigin - std::floor(info.barOrigin / info.barLenPpq) * info.barLenPpq;
    const double feelOff = std::clamp(double(drums.feel()) * p.feel, -0.06, 0.06);
    long long gs = (long long) std::ceil((p0 - feelOff - origin) / stepLen - 1e-9);
    int hi = 0;
    auto feedUntil = [&](double t)
    {
        while (hi < numHits && hits[hi].ppq < t)
        {
            const auto& h = hits[hi++];
            drums.hit(h.ppq - origin, h.note, h.velocity, info.barLenPpq, spb);
        }
    };

    for (;;)
    {
        const double t = origin + double(gs) * stepLen + feelOff;
        if (t >= p1)
            break;
        feedUntil(t);

        const int bar = int(floorDiv(gs, spb));
        const int step = int(gs - (long long) bar * spb);

        if (bar != decidedBar)
            decideBar(bar, info, p);
        else if (pianoAlive && playingBar && step < spb / 2)
        {
            // Piano zdecydowało o tym takcie po nas – przenieś resztę taktu na jego akord.
            const uint64_t pb = g.pianoBar.load(std::memory_order_acquire);
            int pbBar, pbSec, pbPos;
            unpackBar(pb, pbBar, pbSec, pbPos);
            if (pbBar == bar && pb != usedPianoBar)
            {
                usedPianoBar = pb;
                if (pbSec != section || pbPos != chordPos % 240)
                {
                    section = pbSec;
                    chordPos = pbPos;
                    sectionStart = bar - pbPos;
                    chord = chordFor(harmony.key, section ? harmony.progB : harmony.progA, chordPos);
                    buildBar(p, spb, false);
                }
            }
        }

        const bool fill = drums.fillInProgress(bar) && step >= spb / 2;
        if (fill && playingBar)
            status.mode = 4;

        for (int i = 0; i < numPlanned; ++i)
            if (planned[i].step == step && ! fill)
                scheduleNote(planned[i], t, stepLen, p);
        ++gs;
    }

    feedUntil(p1 + 1e9);
    flush(info, p1, out);
    status.energy = drums.energy();
}

void GuitarEngine::resolveHarmony(int bar, const BarFeatures& f, const GuitarParams& p, bool continuous)
{
    const int previousSection = section;
    auto& g = link.group(p.group);
    const auto shared = HarmonySettings::unpack(g.harmony.load(std::memory_order_relaxed));
    harmony = shared.valid ? shared : p.harmony;

    const uint64_t pb = g.pianoBar.load(std::memory_order_acquire);
    int pbBar, pbSec, pbPos;
    unpackBar(pb, pbBar, pbSec, pbPos);

    if (pianoAlive && pbBar == bar)
    {
        usedPianoBar = pb;
        section = pbSec;
        sectionStart = bar - pbPos;
    }
    else if (pianoAlive && pbBar == bar - 1)
    {
        // Piano jeszcze nie zdecydowało: ta sama reguła na tych samych bębnach.
        section = pbSec;
        sectionStart = bar - 1 - pbPos;
        if (harmony.switchesAt(bar, sectionStart, f.fill))
        {
            section ^= 1;
            sectionStart = bar;
        }
    }
    else if (! continuous)
        harmony.sectionAtStart(bar, section, sectionStart);
    else if (harmony.switchesAt(bar, sectionStart, f.fill))
    {
        section ^= 1;
        sectionStart = bar;
    }

    sectionChanged = continuous && section != previousSection;
    chordPos = std::max(0, bar - sectionStart);
    chord = chordFor(harmony.key, section ? harmony.progB : harmony.progA, chordPos);
}

void GuitarEngine::decideBar(int bar, const BlockInfo& info, const GuitarParams& p)
{
    const int spb = std::clamp(info.stepsPerBar, 1, 32);
    const bool continuous = decidedBar == bar - 1;
    const BarFeatures f = drums.finishBar(bar - 1, p.reactivity, spb);

    resolveHarmony(bar, f, p, continuous);
    decidedBar = bar;
    numPlanned = 0;
    playingBar = false;

    const int pos = bar - sectionStart;
    int mode = 0;

    if (drums.silent() >= 1)
        passage = -1; // bębniarz milczy – gitara też
    else
    {
        // Energia: bębniarz + Piano, refren pełniej, a w sekcji lekko narasta.
        float target = drums.energy() + 0.25f * p.density + (p.role == 2 ? 0.2f : 0.0f)
                     + (section == 1 ? 0.12f : 0.0f) + 0.01f * float(std::min(pos, 8));
        if (pianoAlive)
            target = 0.8f * target + 0.2f * float(link.group(p.group).pianoEnergy.load(std::memory_order_relaxed)) / 1000.0f;
        barTarget = std::clamp(target, 0.0f, 1.0f);

        if (! continuous || sectionChanged || bar < passageStart || bar - passageStart >= 8)
            passage = -1;

        if (passage < 0 && pos % 4 == 0)
        {
            // Pasaż zaczyna się z frazą sekcji (co 4 takty), w takcie zgodnym z formą 8-taktową.
            const int pos0 = pos - pos % 8;
            passage = choosePassage(barTarget, pos0, p);
            passageStart = bar - pos % 8;
        }

        if (passage >= 0)
        {
            passageBar = bar - passageStart;
            buildBar(p, spb, sectionChanged);
            playingBar = numPlanned > 0;
            mode = section == 1 ? 2 : 1;
        }
        else
            mode = 3;
    }

    status.mode = mode;
    status.bar = bar;
    status.section = section;
    status.chordPos = chordPos % std::max<int>(1, prog(section ? harmony.progB : harmony.progA).length);
    status.chordRoot = chord.root;
    status.chordQuality = chord.quality;
    status.phrase = passage;
    status.phraseBar = passageBar;
    if (passage >= 0)
    {
        status.phraseSource = set->passages[passage].source;
        status.phraseSong = set->passages[passage].song;
    }
}

float GuitarEngine::harmonicFit(const Passage& ps, int pos0) const
{
    // Akordy pasażu (stopnie w tonacji) kontra 8 taktów bieżącej progresji Piano.
    const auto& pr = prog(section ? harmony.progB : harmony.progA);
    const int tonic = mod12(harmony.key + (pr.minorKey ? 3 : 0));
    float fit = 0.0f;
    for (int k = 0; k < 8; ++k)
    {
        const Chord c = chordFor(harmony.key, section ? harmony.progB : harmony.progA, pos0 + k);
        const int deg = mod12(c.root - tonic);
        const auto& d = ps.chords[k];
        if (d.semis == deg)
            fit += isMinorQuality(d.quality) == isMinorQuality(c.quality) ? 1.0f : 0.6f;
        else
        {
            // wspólne dźwięki (np. IV i ii, I i vi) – melodia i tak dobrze leży
            const auto a = shapeOf(d.quality), b = shapeOf(c.quality);
            int common = 0;
            for (int x : { 0, a.third, a.fifth })
                for (int y : { 0, b.third, b.fifth })
                    if (mod12(d.semis + x) == mod12(deg + y)) ++common;
            fit += common >= 2 ? 0.45f : 0.0f;
        }
    }
    return fit / 8.0f;
}

int GuitarEngine::choosePassage(float target, int pos0, const GuitarParams& p)
{
    const int sec = section & 1;
    // Melodia sekcji wraca: druga zwrotka i drugi refren grają tę samą melodię.
    if (p.role != 2 && sectionPassage[sec] >= 0 && rand01() > 0.4f * p.variation)
    {
        passageShift = sectionShift[sec];
        status.phraseFit = harmonicFit(set->passages[sectionPassage[sec]], pos0);
        return sectionPassage[sec];
    }

    const float lt = set->minEnergy + target * (set->maxEnergy - set->minEnergy);
    uint32_t accMask = 0;
    if (pianoAlive)
        accMask = uint32_t(link.group(p.group).pianoRhythm.load(std::memory_order_relaxed)) & 0xffffu;
    const float busy = std::min(1.0f, 2.0f * float(popcount(accMask)) / 16.0f);
    const int other = sectionPassage[sec ^ 1];

    float best = -1e9f, bestFit = 0.0f;
    int bestIndex = -1;
    for (int i = 0; i < set->count; ++i)
    {
        const auto& ps = set->passages[i];
        if (p.role == 0 && ps.source != 0) continue;   // melodia: linie wokalne
        if (p.role == 1 && ps.source != 1) continue;   // wypełnienia: kontrmelodie
        const float fit = harmonicFit(ps, pos0);
        float s = 3.0f * fit - 3.0f * std::abs(ps.energy - lt);
        for (int r : recent)
            if (r == i) s -= 1.0f;
        if (i == other) s -= 2.0f;                      // refren inny niż zwrotka
        if (sec == 1) s += 0.25f * std::min(2.0f, float(ps.repeats - 1)); // refreny się powtarzają
        s += rand01() * (0.1f + 0.5f * p.variation);

        if (accMask != 0)
        {
            int on = 0, clash = 0;
            for (int k = 0; k < ps.count; ++k)
            {
                const auto& n = set->notes[ps.first + k];
                ++on;
                if ((accMask >> n.step & 1u) && (n.step & 1)) ++clash;
            }
            s -= p.space * 0.8f * busy * float(clash) / float(std::max(1, on));
        }
        if (s > best)
        {
            best = s;
            bestIndex = i;
            bestFit = fit;
        }
    }
    if (bestIndex < 0)
        return -1;

    for (int k = 5; k > 0; --k)
        recent[k] = recent[k - 1];
    recent[0] = bestIndex;
    passage = bestIndex;
    chooseShift(p);
    if (p.role != 2)
    {
        sectionPassage[sec] = bestIndex;
        sectionShift[sec] = passageShift;
    }
    status.phraseFit = bestFit;
    return bestIndex;
}

void GuitarEngine::chooseShift(const GuitarParams& p)
{
    // Jedna oktawa dla całego pasażu: kształt melodii zostaje nietknięty.
    const auto& ps = set->passages[passage];
    const int tonic = mod12(harmony.key + (prog(section ? harmony.progB : harmony.progA).minorKey ? 3 : 0));
    const int ref = 60 + tonic;
    const int centre = p.registerCenter + (section == 1 ? 4 : 0); // refren wyżej
    float bestCost = 1e9f;
    for (int s : { -24, -12, 0, 12 })
    {
        float cost = std::abs(float(ref + ps.centre + s) - float(centre));
        if (lastPitch >= 0 && ps.count > 0)
            cost += 0.25f * float(std::abs(ref + set->notes[ps.first].rel + s - lastPitch));
        if (cost < bestCost)
        {
            bestCost = cost;
            passageShift = s;
        }
    }
}

void GuitarEngine::buildBar(const GuitarParams& p, int spb, bool accent)
{
    numPlanned = 0;
    if (passage < 0 || passage >= set->count)
        return;

    const auto& ps = set->passages[passage];
    const auto& pr = prog(section ? harmony.progB : harmony.progA);
    const int tonic = mod12(harmony.key + (pr.minorKey ? 3 : 0));
    const uint16_t scale = scaleMask(harmony.key, pr.minorKey);
    const bool ks = p.art.keyswitches;
    const int lo = std::max(p.lowest, ks ? 50 : 0);
    const int hiP = std::max(lo + 12, std::min(p.highest + (section == 1 ? 4 : 0), 120));
    const float dynScale = std::clamp(1.0f + p.dynamics * (drums.dynamics() * 1.25f - 0.75f), 0.45f, 1.35f);
    const auto sh = shapeOf(chord.quality);

    auto allowed = [&](int pitch)
    {
        const int q = mod12(pitch - chord.root);
        if (isChordTone(q, chord.quality))
            return true;
        if (! (scale & (1u << mod12(pitch))))
            return false;
        for (int t : { 0, sh.third, sh.fifth, sh.seventh })
            if (t >= 0 && (mod12(q - t) == 1 || mod12(t - q) == 1))
                return false;
        return true;
    };

    for (int k = 0; k < ps.count && numPlanned < 48; ++k)
    {
        const auto& n = set->notes[ps.first + k];
        if (n.bar != passageBar || n.step >= spb)
            continue;
        int pitch = 60 + tonic + n.rel + passageShift;
        while (pitch < lo) pitch += 12;
        while (pitch > hiP) pitch -= 12;
        if (! allowed(pitch))
        {
            // Najbliższy dźwięk bez zgrzytu (pół / cały ton), przy remisie składnik akordu, potem w dół.
            int best = pitch;
            for (int d = 1; d <= 4 && best == pitch; ++d)
            {
                const bool downOk = allowed(pitch - d), upOk = allowed(pitch + d);
                if (downOk && upOk)
                    best = isChordTone(mod12(pitch + d - chord.root), chord.quality) && ! isChordTone(mod12(pitch - d - chord.root), chord.quality)
                               ? pitch + d : pitch - d;
                else if (downOk) best = pitch - d;
                else if (upOk) best = pitch + d;
            }
            pitch = best;
        }
        float v = float(n.vel) * dynScale * (p.role == 2 ? 1.08f : 1.0f) * (section == 1 ? 1.06f : 1.0f);
        if (accent && numPlanned == 0) v *= 1.12f;
        planned[numPlanned++] = { int8_t(n.step), int8_t(std::clamp<int>(n.len, 1, spb - n.step)), uint8_t(pitch),
                                  uint8_t(std::clamp(int(v), 20, 127)), 0 };
    }

    // Gitarzysta trzyma dźwięk do następnego (wokalne sylaby staccato brzmią na gitarze sucho);
    // dłuższe pauzy zostają, bo to oddech frazy.
    for (int i = 0; i < numPlanned; ++i)
    {
        auto& n = planned[i];
        const int next = i + 1 < numPlanned ? planned[i + 1].step : std::min(spb, 16);
        const int gap = next - (n.step + n.len);
        if (gap > 0 && gap <= 2)
            n.len = int8_t(n.len + gap);
    }

    // Ozdobniki gitarowe – oszczędnie, żeby melodia była czytelna.
    const float orn = std::clamp(p.ornaments + (p.role == 2 ? 0.2f : 0.0f), 0.0f, 1.0f);
    float avgLen = 0.0f;
    for (int i = 0; i < numPlanned; ++i) avgLen += float(planned[i].len);
    avgLen /= float(std::max(1, numPlanned));
    const bool mutedBar = ks && barTarget < 0.35f && numPlanned >= 7 && avgLen <= 1.5f;
    const int barEnd = std::min(spb, 16);
    const bool phraseEnd = passageBar == 3 || passageBar == 7;

    for (int i = 0; i < numPlanned; ++i)
    {
        auto& n = planned[i];
        if (mutedBar)
            n.flags |= Muted;
        if (i > 0 && ! mutedBar)
        {
            auto& prev = planned[i - 1];
            const int gap = n.step - (prev.step + prev.len);
            const int ad = std::abs(int(n.pitch) - int(prev.pitch));
            if (gap == 0 && ad != 0)
            {
                if (ad <= 2 && n.len <= 2 && rand01() < 0.5f * orn)
                {
                    // hammer-on / pull-off: zachodzące nuty, druga ciszej (Session Guitarist)
                    prev.flags |= LegatoOut;
                    n.flags |= LegatoIn;
                    n.vel = uint8_t(std::max(20, int(float(prev.vel) * 0.6f)));
                }
                else if (ks && ad >= 3 && ad <= 7 && n.len >= 3 && rand01() < 0.35f * orn)
                {
                    prev.flags |= LegatoOut;
                    n.flags |= LegatoIn | SlideIn;
                }
            }
        }
        if (! (n.flags & LegatoIn) && ! mutedBar && n.len >= 4 && p.art.bendRange >= 1
            && isChordTone(mod12(int(n.pitch) - chord.root), chord.quality) && rand01() < 0.25f * orn)
            n.flags |= Bend;
        if (n.len >= 6 && rand01() < 0.2f + 0.6f * orn)
            n.flags |= Vibrato;
        // zjazd po ostatniej nucie frazy (co 4 takty); robimy mu miejsce, skracając nutę o 16-tkę
        if (ks && ! mutedBar && phraseEnd && i == numPlanned - 1 && n.len >= 2 && rand01() < 0.6f * orn)
        {
            if (n.step + n.len >= barEnd && n.len >= 3)
                n.len = int8_t(n.len - 1);
            if (barEnd - (n.step + n.len) >= 1)
                n.flags |= FallOff;
        }
    }
    for (int i = 0; i < numPlanned; ++i)
        if (planned[i].flags & LegatoOut)
            planned[i].flags &= uint8_t(~(Bend | Vibrato | FallOff));
}

void GuitarEngine::scheduleNote(const Planned& n, double t, double stepLen, const GuitarParams& p)
{
    const uint8_t on = uint8_t(0x90 | channel), off = uint8_t(0x80 | channel);
    const bool ks = p.art.keyswitches;
    const double end = (n.flags & LegatoOut) ? t + n.len * stepLen + legatoOverlap
                                             : t + std::max(0.5, double(n.len) - 0.2) * stepLen;

    if (ks)
    {
        const int want = (n.flags & Muted) ? p.art.muted : p.art.open;
        if (want != currentArt)
        {
            push(t + ksLead, on, uint8_t(std::clamp(want, 0, 127)), 100);
            push(t + ksLead, off, uint8_t(std::clamp(want, 0, 127)), 0);
            currentArt = want;
        }
    }

    if (ks && (n.flags & SlideIn))
    {
        push(t + ksLead, on, uint8_t(std::clamp(p.art.slideIn, 0, 127)), 100);
        status.slides = status.slides + 1;
    }

    int pitch = n.pitch;
    const int range = std::clamp(p.art.bendRange, 1, 24);
    const int amt = (n.flags & Bend) ? std::min(2, range) : 0;
    if (amt > 0)
        pitch -= amt; // gitarzysta łapie niższy próg i podciąga strunę do dźwięku z frazy

    push(t, on, uint8_t(pitch), n.vel);

    if (ks && (n.flags & SlideIn))
        push(t + ksHold, off, uint8_t(std::clamp(p.art.slideIn, 0, 127)), 0);
    if (n.flags & LegatoIn)
        status.legatos = status.legatos + 1;

    if (amt > 0)
    {
        const double dur = end - t;
        const double b0 = t + std::min(0.2, 0.25 * dur), b1 = b0 + std::min(0.25, 0.35 * dur);
        const int target = 8192 + int(std::lround(8191.0 * double(amt) / double(range)));
        for (int k = 1; k <= 5; ++k)
        {
            const double x = double(k) / 5.0;
            const int v = 8192 + int(std::lround(double(target - 8192) * (1.0 - (1.0 - x) * (1.0 - x))));
            push(b0 + (b1 - b0) * x, uint8_t(0xE0 | channel), uint8_t(v & 127), uint8_t((v >> 7) & 127));
        }
        status.bends = status.bends + 1;
    }

    if (n.flags & Vibrato)
    {
        const double dur = end - t;
        const double v0 = t + 0.4 * dur;
        for (int k = 1; k <= 4; ++k)
            push(v0 + 0.08 * dur * k, uint8_t(0xB0 | channel), 1, uint8_t(18 * k));
    }

    double release = end;
    if (ks && (n.flags & FallOff))
    {
        // wysokie dźwięki zjeżdżają w dół, niskie podjeżdżają w górę
        const int key = std::clamp(n.pitch >= p.registerCenter ? p.art.slideDownEnd : p.art.slideUpEnd, 0, 127);
        push(end, on, uint8_t(key), 72);
        release = end + fallLength;
        push(release, off, uint8_t(key), 0);
        status.falls = status.falls + 1;
    }
    push(release, off, uint8_t(pitch), 0);
    if (amt > 0)
        push(release + 0.005, uint8_t(0xE0 | channel), 0, 64);
    if (n.flags & Vibrato)
        push(release + 0.005, uint8_t(0xB0 | channel), 1, 0);

    lastPitch = n.pitch;
    status.lastPitch = n.pitch;
}

} // namespace sk
