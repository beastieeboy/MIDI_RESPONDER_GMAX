#include "Engine.h"
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

    int progLength(int prog) { return progressions[std::clamp(prog, 0, numProgressions - 1)].length; }
    bool progMinor(int prog) { return progressions[std::clamp(prog, 0, numProgressions - 1)].minorKey; }

    void unpackBar(uint64_t w, int& bar, int& section, int& pos)
    {
        bar = int(int32_t(uint32_t(w >> 32)));
        section = int((w >> 8) & 255);
        pos = int(w & 255);
    }
}

Engine::Engine(Role r, SharedLink& l) : myRole(r), link(l)
{
    set = &lickSet(r); // inicjalizacja statycznej biblioteki poza wątkiem audio
}

void Engine::prepare()
{
    drums.reset();
    wasPlaying = false;
    decidedBar = INT32_MIN;
    plan = PlanKind::Rest;
    numPlanned = 0;
    numActive = 0;
    currentLick = -1;
}

float Engine::rand01()
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return float(rng & 0xffffff) / float(0x1000000);
}

bool Engine::partnerAlive(const EngineParams& p) const
{
    auto& g = link.group(p.group);
    const int64_t hb = myRole == Role::Piano ? g.bassHeartbeat.load(std::memory_order_relaxed)
                                             : g.pianoHeartbeat.load(std::memory_order_relaxed);
    return hb != 0 && SharedLink::nowMs() - hb < SharedLink::aliveMs;
}

void Engine::heartbeat(const EngineParams& p)
{
    auto& g = link.group(p.group);
    if (myRole == Role::Piano)
    {
        g.harmony.store(p.harmony.pack(), std::memory_order_relaxed);
        g.pianoHeartbeat.store(SharedLink::nowMs(), std::memory_order_relaxed);
    }
    else
    {
        g.bassHeartbeat.store(SharedLink::nowMs(), std::memory_order_relaxed);
    }
    status.partnerAlive = partnerAlive(p);
    status.linkShared = link.isShared();
    status.harmonyWord = myRole == Role::Piano ? p.harmony.pack() : g.harmony.load(std::memory_order_relaxed);
}

void Engine::process(const BlockInfo& info, const EngineParams& p, const DrumHit* hits, int numHits, MidiOutBuffer& out)
{
    if (! info.playing || info.ppqPerSample <= 0.0 || info.numSamples <= 0)
    {
        if (wasPlaying)
            allNotesOff(p, out);
        wasPlaying = false;
        decidedBar = INT32_MIN;
        plan = PlanKind::Rest;
        status.mode = 0;
        return;
    }

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
        // skok kursora albo pętla – zaczynamy od nowa, bez wiszących nut
        allNotesOff(p, out);
        decidedBar = INT32_MIN;
    }
    wasPlaying = true;
    expectedPpq = p1;
    drums.map = p.drums;

    const double origin = info.barOrigin;
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

    const bool alive = partnerAlive(p);

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
        else if (myRole == Role::Bass && alive && step < spb / 2 && plan == PlanKind::Lick)
        {
            // Piano mogło zdecydować o tym takcie dopiero po nas (inny wątek) – dopasuj harmonię.
            const uint64_t pb = link.group(p.group).pianoBar.load(std::memory_order_acquire);
            int pbBar, pbSec, pbPos;
            unpackBar(pb, pbBar, pbSec, pbPos);
            if (pbBar == bar && pb != usedPianoBar)
            {
                usedPianoBar = pb;
                if (pbSec != section || pbPos != chordPos % std::max(1, progLength(section ? harmony.progB : harmony.progA)))
                {
                    section = pbSec;
                    chordPos = pbPos;
                    sectionStart = bar - pbPos;
                    chord = chordFor(harmony.key, section ? harmony.progB : harmony.progA, chordPos);
                    buildLickPlan(currentLick, p, spb, false);
                }
            }
        }

        releaseUntil(t, info, p, out);

        // Wejście zespołu, gdy bębniarz zaczyna grać w środku taktu.
        if (plan == PlanKind::Rest && step % 4 == 0 && step < spb - 2 && drums.barInProgress(bar).hits >= 2)
        {
            buildPad(step, p, spb);
            plan = PlanKind::Pad;
            status.mode = 3;
        }

        // Bębniarz gra przejście – Piano milknie, Bass gra tylko na bitach.
        const bool fill = drums.fillInProgress(bar) && step >= spb / 2;
        if (fill)
            status.mode = 2;

        const int offset = std::clamp(int((t - p0) / info.ppqPerSample), 0, info.numSamples - 1);
        for (int i = 0; i < numPlanned; ++i)
        {
            const auto& n = planned[i];
            if (n.step != step)
                continue;
            if (fill && (myRole == Role::Piano || step % 4 != 0))
                continue;
            const double end = t + std::max(0.5, double(n.len) - 0.1) * stepLen;
            noteOn(offset, n.pitch, n.vel, end, p, out);
        }
        ++gs;
    }

    feedUntil(p1 + 1e9);
    releaseUntil(p1, info, p, out);

    status.energy = drums.energy();
    status.hatDensity = std::min(1.0f, float(drums.lastBar().hats) / float(spb));
}

void Engine::decideBar(int bar, const BlockInfo& info, const EngineParams& p)
{
    const int spb = std::clamp(info.stepsPerBar, 1, 32);
    const bool continuous = decidedBar == bar - 1;
    const BarFeatures f = drums.finishBar(bar - 1, p.reactivity, spb);
    const PlanKind previous = plan;

    resolveHarmony(bar, f, p, continuous);
    decidedBar = bar;

    if (drums.silent() >= 1)
    {
        if (drums.silent() == 1 && continuous && previous != PlanKind::Rest)
        {
            buildPad(0, p, spb); // bębniarz zrobił pauzę: trzymamy akord
            plan = PlanKind::Pad;
            status.mode = 3;
        }
        else
        {
            plan = PlanKind::Rest; // czekamy na bębniarza
            numPlanned = 0;
            planMask = 0;
            status.mode = 0;
        }
    }
    else
    {
        float target = std::clamp(drums.energy() + 0.25f * p.density, 0.0f, 1.0f);
        if (myRole == Role::Bass && partnerAlive(p))
            target = 0.75f * target + 0.25f * float(link.group(p.group).pianoEnergy.load()) / 1000.0f;
        if (sectionChanged)
            target = std::min(1.0f, target + 0.1f);

        const int li = chooseLick(target, f, p);
        buildLickPlan(li, p, spb, sectionChanged);
        plan = PlanKind::Lick;
        status.mode = 1;
    }

    auto& g = link.group(p.group);
    if (myRole == Role::Piano)
    {
        g.pianoRhythm.store(uint64_t(uint32_t(bar)) << 32 | planMask, std::memory_order_relaxed);
        g.pianoEnergy.store(uint32_t(drums.energy() * 1000.0f), std::memory_order_relaxed);
        g.pianoBar.store(packBar(bar, section, chordPos % progLength(section ? harmony.progB : harmony.progA)),
                         std::memory_order_release);
    }
    else
    {
        g.bassRhythm.store(planMask, std::memory_order_relaxed);
    }

    status.bar = bar;
    status.section = section;
    status.chordPos = chordPos % progLength(section ? harmony.progB : harmony.progA);
    status.chordRoot = chord.root;
    status.chordQuality = chord.quality;
    status.lick = plan == PlanKind::Lick ? currentLick : -1;
}

void Engine::resolveHarmony(int bar, const BarFeatures& f, const EngineParams& p, bool continuous)
{
    const int previousSection = section;
    sectionChanged = false;

    auto ownLogic = [&]
    {
        if (! continuous)
        {
            section = 0;      // start / skok: zwrotka, akordy liczone od początku projektu
            sectionStart = 0;
        }
        else if (f.fill && harmony.fillSwitchesSection)
        {
            section ^= 1;     // przejście na tomach = zmiana zwrotka <-> refren
            sectionStart = bar;
        }
    };

    if (myRole == Role::Piano)
    {
        harmony = p.harmony;
        ownLogic();
    }
    else
    {
        auto& g = link.group(p.group);
        const auto shared = HarmonySettings::unpack(g.harmony.load(std::memory_order_relaxed));
        harmony = shared.valid ? shared : p.harmony;

        const uint64_t pb = g.pianoBar.load(std::memory_order_acquire);
        int pbBar, pbSec, pbPos;
        unpackBar(pb, pbBar, pbSec, pbPos);
        const bool alive = partnerAlive(p);

        if (alive && pbBar == bar)
        {
            usedPianoBar = pb;
            section = pbSec;
            sectionStart = bar - pbPos;
        }
        else if (alive && pbBar == bar - 1)
        {
            // Piano jeszcze nie zdecydowało: przewidujemy tą samą regułą (te same bębny).
            section = pbSec;
            sectionStart = bar - 1 - pbPos;
            if (f.fill && harmony.fillSwitchesSection)
            {
                section ^= 1;
                sectionStart = bar;
            }
        }
        else
        {
            ownLogic();
        }
    }

    sectionChanged = continuous && section != previousSection;
    chordPos = std::max(0, bar - sectionStart);
    chord = chordFor(harmony.key, section ? harmony.progB : harmony.progA, chordPos);
}

int Engine::chooseLick(float target, const BarFeatures& f, const EngineParams& p)
{
    const int phrase = p.variation < 0.3f ? 4 : (p.variation < 0.7f ? 2 : 1);
    const float keepTolerance = 0.08f + 0.12f * (1.0f - p.variation);
    if (currentLick >= 0 && ! sectionChanged && ! f.fill
        && decidedBar - lickSince < phrase && std::abs(target - lickTarget) < keepTolerance)
        return currentLick;

    const float lt = set->minEnergy + target * (set->maxEnergy - set->minEnergy);
    const uint32_t kick = f.kickMask & 0xffffu;
    const int kickN = popcount(kick);
    uint32_t pianoMask = 0;
    if (myRole == Role::Bass && partnerAlive(p))
        pianoMask = uint32_t(link.group(p.group).pianoRhythm.load(std::memory_order_relaxed)) & 0xffffu;

    float best = -1e9f;
    int bestIndex = 0;
    for (int i = 0; i < set->count; ++i)
    {
        const auto& l = set->licks[i];
        float s = -std::abs(l.energy - lt) * 4.0f;
        for (int r : recent)
            if (r == i) s -= 0.6f;
        s += rand01() * (0.15f + 0.6f * p.variation);

        const int m = std::max(1, popcount(l.onsetMask));
        if (kickN > 0)
        {
            const float hitKick = float(popcount(l.onsetMask & kick)) / float(kickN);
            const float offKick = float(popcount(l.onsetMask & ~kick & 0xaaaau)) / float(m);
            if (myRole == Role::Bass)
                s += p.kickLock * (0.8f * hitKick - 0.4f * offKick);
            else
                s += 0.15f * hitKick;
        }
        if (pianoMask != 0)
        {
            // Bass dopełnia Piano: mniej "przeszkadzania" na słabych 16-tkach, gdy Piano jest gęste.
            const float busy = float(popcount(pianoMask)) / 16.0f;
            s -= 0.5f * busy * float(popcount(l.onsetMask & pianoMask & 0xaaaau)) / float(m);
        }
        if (s > best)
        {
            best = s;
            bestIndex = i;
        }
    }

    for (int k = 3; k > 0; --k)
        recent[k] = recent[k - 1];
    recent[0] = bestIndex;
    currentLick = bestIndex;
    lickSince = decidedBar;
    lickTarget = target;
    return bestIndex;
}

void Engine::buildLickPlan(int li, const EngineParams& p, int spb, bool accent)
{
    numPlanned = 0;
    planMask = 0;
    if (li < 0 || li >= set->count)
        return;

    const auto& l = set->licks[li];
    const int prog = section ? harmony.progB : harmony.progA;
    const uint16_t scale = scaleMask(harmony.key, progMinor(prog));
    const bool giveWay = myRole == Role::Piano && p.giveWayToBass && partnerAlive(p);
    const float dynScale = std::clamp(1.0f + p.dynamics * (drums.dynamics() * 1.25f - 0.75f), 0.4f, 1.4f);

    int rels[64];
    const int n = std::min<int>(l.count, 64);
    float mean = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        rels[i] = remapInterval(set->notes[l.first + i].rel, l.quality, chord.quality, chord.root, scale);
        mean += float(rels[i]);
    }
    mean /= float(std::max(1, n));

    int ref, shift = 0, lo, hiP;
    if (myRole == Role::Piano)
    {
        ref = 48 + mod12(chord.root - 48);
        const float avg = float(ref) + mean;
        const float tgt = 0.5f * centroid + 0.5f * float(p.registerCenter);
        float bestD = 1e9f;
        for (int s : { -12, 0, 12 })
            if (std::abs(avg + float(s) - tgt) < bestD) { bestD = std::abs(avg + float(s) - tgt); shift = s; }
        centroid = avg + float(shift);
        lo = giveWay ? 52 : 36;
        hiP = 96;
    }
    else
    {
        ref = 28 + mod12(chord.root - 28) + 12 * std::clamp(p.octave, -1, 1);
        lo = 28 + (p.octave < 0 ? -4 : 0);
        hiP = 67;
    }

    auto add = [&](int step, int len, int pitch, int vel)
    {
        if (step >= spb || numPlanned >= 64)
            return;
        while (pitch < lo) pitch += 12;
        while (pitch > hiP) pitch -= 12;
        for (int k = 0; k < numPlanned; ++k)
            if (planned[k].step == step && planned[k].pitch == pitch)
                return;
        planned[numPlanned++] = { int8_t(step), int8_t(std::clamp(len, 1, spb - step)), uint8_t(pitch),
                                  uint8_t(std::clamp(vel, 1, 127)) };
        planMask |= 1u << step;
    };

    for (int rep = 0; rep * 16 < spb; ++rep)
        for (int i = 0; i < n; ++i)
        {
            const auto& note = set->notes[l.first + i];
            const float a = (accent && note.step == 0 && rep == 0) ? 1.15f : 1.0f;
            add(note.step + rep * 16, note.len, ref + rels[i] + shift, int(float(note.vel) * dynScale * a));
        }

    // Bass "na stopę": dopisz pryma tam, gdzie bębniarz gra stopę, a lick milczy.
    if (myRole == Role::Bass && p.kickLock >= 0.75f)
    {
        const uint32_t kick = drums.lastBar().kickMask;
        for (int s = 0; s < spb && s < 32; ++s)
            if ((kick >> s & 1u) && ! (planMask >> s & 1u))
            {
                int len = 1;
                while (s + len < spb && len < 4 && ! (planMask >> (s + len) & 1u) && ! (kick >> (s + len) & 1u))
                    ++len;
                add(s, len, ref, int(90.0f * dynScale));
            }
    }
}

void Engine::buildPad(int fromStep, const EngineParams& p, int spb)
{
    numPlanned = 0;
    planMask = 0;
    const auto sh = shapeOf(chord.quality);
    const float dynScale = std::clamp(1.0f + p.dynamics * (drums.dynamics() * 1.25f - 0.75f), 0.4f, 1.2f);
    const int len = spb - fromStep;
    auto add = [&](int pitch, int vel)
    {
        if (numPlanned < 64)
            planned[numPlanned++] = { int8_t(fromStep), int8_t(len), uint8_t(std::clamp(pitch, 0, 127)),
                                      uint8_t(std::clamp(int(float(vel) * dynScale), 1, 127)) };
    };

    if (myRole == Role::Piano)
    {
        const int r = p.registerCenter - 6 + mod12(chord.root - (p.registerCenter - 6));
        add(r, 64);
        add(r + sh.third, 58);
        add(r + sh.fifth, 58);
        if (sh.seventh >= 0) add(r + sh.seventh, 52);
        if (! (p.giveWayToBass && partnerAlive(p))) add(r - 12, 60);
    }
    else
    {
        add(28 + mod12(chord.root - 28) + 12 * std::clamp(p.octave, -1, 1), 80);
    }
    planMask = 1u << fromStep;
}

void Engine::noteOn(int offset, uint8_t pitch, uint8_t vel, double endPpq, const EngineParams& p, MidiOutBuffer& out)
{
    const uint8_t ch = uint8_t(std::clamp(p.midiChannel, 1, 16) - 1);
    for (int i = 0; i < numActive; ++i)
        if (active[i].pitch == pitch)
        {
            out.add(offset, uint8_t(0x80 | ch), pitch, 0);
            active[i] = active[--numActive];
            break;
        }
    if (numActive == 64)
    {
        out.add(offset, uint8_t(0x80 | ch), active[0].pitch, 0);
        active[0] = active[--numActive];
    }
    out.add(offset, uint8_t(0x90 | ch), pitch, vel);
    active[numActive++] = { pitch, endPpq };
}

void Engine::releaseUntil(double ppq, const BlockInfo& info, const EngineParams& p, MidiOutBuffer& out)
{
    const uint8_t ch = uint8_t(std::clamp(p.midiChannel, 1, 16) - 1);
    for (int i = 0; i < numActive;)
    {
        if (active[i].endPpq <= ppq)
        {
            const int off = std::clamp(int((active[i].endPpq - info.ppqStart) / info.ppqPerSample), 0, info.numSamples - 1);
            out.add(off, uint8_t(0x80 | ch), active[i].pitch, 0);
            active[i] = active[--numActive];
        }
        else
            ++i;
    }
}

void Engine::allNotesOff(const EngineParams& p, MidiOutBuffer& out)
{
    const uint8_t ch = uint8_t(std::clamp(p.midiChannel, 1, 16) - 1);
    for (int i = 0; i < numActive; ++i)
        out.add(0, uint8_t(0x80 | ch), active[i].pitch, 0);
    numActive = 0;
    out.add(0, uint8_t(0xB0 | ch), 123, 0);
}

} // namespace sk
