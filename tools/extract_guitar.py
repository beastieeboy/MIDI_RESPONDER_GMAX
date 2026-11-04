#!/usr/bin/env python3
"""Ekstrakcja biblioteki melodii gitary prowadzącej z POP909.

Wynik: Source/Core/GuitarPhraseData.h (dane wkompilowane we wtyczkę Gitara).

Jednostka biblioteki to **pasaż**: 8 kolejnych taktów 4/4 z *jednej* piosenki,
czyli cała melodia zwrotki albo refrenu (zwykle A A' w dwóch frazach po 4 takty).
Dzięki temu gitara gra spójną, rozwijającą się melodię, a nie sklejkę urywków.

Wysokości zapisujemy względem toniki **durowej** tonacji fragmentu (moll ->
równoległy dur, np. a-moll -> C), razem z akordem każdego taktu jako stopniem
(półtony od toniki + jakość). W czasie gry pasaż jest transponowany do tonacji
Piano bez zmiany kształtu linii, a wybieramy te pasaże, których akordy najlepiej
pasują do bieżącej progresji (I-V-vi-IV jest w POP909 bardzo częste, więc zwykle
melodia gra na „swoich” akordach). Dźwięki, które mimo to zgrzytałyby z akordem,
silnik przesuwa o pół lub cały ton (patrz GuitarEngine.cpp).

Źródła w POP909:
  MELODY – linia wokalu (melodie zwrotek i refrenów),
  BRIDGE – instrumentalne kontrmelodie i wstawki między frazami wokalu.

Użycie:
  python3 tools/extract_guitar.py --pop909 /ścieżka/POP909 [--out Source/Core/GuitarPhraseData.h]
"""
import argparse, collections, hashlib, os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from extract_licks import parse_chord, midi_notes  # noqa: E402

BARS = 8
STEPS = 16 * BARS
MINORISH = (1, 4, 5)   # min, min7, dim

# Stopnie diatoniczne durowe: półtony od toniki -> jakości typowe w popie.
DIATONIC = {0: (0, 3), 2: (1, 4), 4: (1, 4), 5: (0, 3), 7: (0, 2), 9: (1, 4), 11: (5,), 10: (0,)}

# Progresje z Source/Core/Harmony.h jako (półtony od toniki durowej, molowy?) – do priorytetu.
PROGS = [
    [(0, 0), (7, 0), (9, 1), (5, 0)], [(9, 1), (5, 0), (0, 0), (7, 0)], [(0, 0), (9, 1), (5, 0), (7, 0)],
    [(0, 0), (5, 0), (9, 1), (7, 0)], [(5, 0), (0, 0), (7, 0), (9, 1)], [(0, 0), (5, 0), (0, 0), (7, 0)],
    [(2, 1), (7, 0), (0, 0), (0, 0)], [(0, 0), (4, 1), (5, 0), (7, 0)],
    [(0, 0), (7, 0), (9, 1), (4, 1), (5, 0), (0, 0), (5, 0), (7, 0)], [(0, 0), (5, 0)],
    [(0, 0), (10, 0), (5, 0), (0, 0)], [(5, 0), (7, 0), (4, 1), (9, 1)],
    # molowe zapisane względem toniki durowej (i = 9)
    [(9, 1), (5, 0), (0, 0), (7, 0)], [(9, 1), (7, 0), (5, 0), (4, 0)], [(9, 1), (2, 1), (5, 0), (4, 0)],
    [(9, 1), (5, 0), (7, 0), (9, 1)], [(9, 1), (2, 1), (7, 0), (0, 0)],
]


def bar_chord(chords, t0, t1):
    """Akord o największym udziale w takcie (albo None, gdy brak harmonii)."""
    best, best_ov = None, 0.0
    for a, b, c in chords:
        ov = min(b, t1) - max(a, t0)
        if ov > best_ov and c:
            best, best_ov = c, ov
    return best if best_ov > 0.5 * (t1 - t0) else None


def key_of_window(chs, hint):
    """Tonika durowa, w której akordy fragmentu są najbardziej diatoniczne."""
    best, best_s = None, -1.0
    for t in range(12):
        s = 0.0
        for root, q in chs:
            d = (root - t) % 12
            if d in DIATONIC:
                s += 1.0 if q in DIATONIC[d] or (q == 6 and d in (0, 5, 7)) else 0.5
        if t == hint:
            s += 0.25
        if s > best_s:
            best, best_s = t, s
    return best, best_s


def quantize(notes, bar_times):
    """Monofoniczna linia (górny głos) w siatce 16-tek, nuty przycięte do kreski taktowej."""
    by_step = {}
    for s, e, p, v in notes:
        if s < bar_times[0] - 0.03 or s >= bar_times[-1] - 0.03:
            continue
        b = max(0, min(BARS - 1, next(i for i in range(BARS) if s < bar_times[i + 1] - 0.03)))
        t0, t1 = bar_times[b], bar_times[b + 1]
        st = int(round((s - t0) / (t1 - t0) * 16))
        if st >= 16:
            b, st = b + 1, 0
            if b >= BARS:
                continue
            t0, t1 = bar_times[b], bar_times[b + 1]
        st = max(0, st)
        ln = max(1, int(round((e - s) / (t1 - t0) * 16)))
        k = b * 16 + st
        if k not in by_step or p > by_step[k][1]:
            by_step[k] = (ln, p, v)
    keys = sorted(by_step)
    out = []
    for i, k in enumerate(keys):
        ln, p, v = by_step[k]
        nxt = keys[i + 1] if i + 1 < len(keys) else STEPS
        bar_end = (k // 16 + 1) * 16
        out.append((k, max(1, min(ln, nxt - k, bar_end - k)), p, v))
    return out


def make_passage(q, chs, tonic):
    if len(q) < 12:
        return None
    bars_with = {k // 16 for k, *_ in q}
    if len(bars_with) < 5 or 0 not in bars_with:
        return None
    if any(sum(1 for k, *_ in q if k // 16 == b) > 14 for b in range(BARS)):
        return None
    pitches = [p for _, _, p, _ in q]
    if max(pitches) - min(pitches) > 21 or any(abs(a - b) > 12 for a, b in zip(pitches, pitches[1:])):
        return None
    ref = 60 + tonic
    mean_v = sum(v for *_, v in q) / len(q)
    notes = []
    for k, ln, p, v in q:
        rel = p - ref
        if not -24 <= rel <= 36:
            return None
        notes.append((k // 16, k % 16, ln, rel, max(30, min(127, int(round(90 + (v - mean_v) * 0.8))))))
    degs = [((r - tonic) % 12, qq) for r, qq in chs]
    return dict(notes=notes, chords=degs)


def features(ps):
    notes = ps["notes"]
    n = len(notes)
    rels = [r for *_, r, _ in notes]
    dens = n / STEPS
    sync = sum(1 for _, st, *_ in notes if st % 2) / n
    span = max(rels) - min(rels)
    ps.update(dens=dens, centre=sum(rels) / n,
              energy=min(1.0, 0.6 * dens / 0.4 + 0.2 * sync + 0.1 * span / 21 + 0.1 * (sum(rels) / n + 12) / 36))
    # dopasowanie do progresji z Harmony.h (z przesunięciem fazy)
    best = 0.0
    for pr in PROGS:
        L = len(pr)
        for ph in range(L):
            s = 0.0
            for i, (d, q) in enumerate(ps["chords"]):
                pd, pm = pr[(i + ph) % L]
                if d == pd:
                    s += 1.0 if (q in MINORISH) == bool(pm) else 0.6
            best = max(best, s / BARS)
    ps["match"] = best
    return ps


def key_hash(ps):
    return hashlib.md5(repr([(b, s, l, r) for b, s, l, r, _ in ps["notes"]]).encode()).hexdigest()


def pop909_passages(root, out, stats):
    for song in sorted(os.listdir(root)):
        d = os.path.join(root, song)
        mid = os.path.join(d, song + ".mid")
        if not os.path.isfile(mid):
            continue
        beats = [tuple(map(float, l.split())) for l in open(os.path.join(d, "beat_midi.txt")) if l.strip()]
        chords = []
        for l in open(os.path.join(d, "chord_midi.txt")):
            parts = l.split()
            if len(parts) >= 3:
                chords.append((float(parts[0]), float(parts[1]), parse_chord(parts[2])))
        hint = None
        kf = os.path.join(d, "key_audio.txt")
        if os.path.isfile(kf):
            for l in open(kf):
                parts = l.split()
                if len(parts) >= 3:
                    kc = parse_chord(parts[2])
                    if kc:
                        hint = (kc[0] + (3 if kc[1] == 1 else 0)) % 12
                    break
        downs = [i for i, b in enumerate(beats) if b[2] == 1.0]
        tracks = {}
        for track in ("MELODY", "BRIDGE"):
            try:
                tracks[track], _ = midi_notes(mid, lambda name, prog, drum, t=track: name.strip().upper() == t)
            except Exception as e:
                print("pomijam", mid, e, file=sys.stderr)
        stats["songs"] += 1
        for w in range(0, len(downs) - BARS, 4):
            idx = downs[w:w + BARS + 1]
            if any(b - a != 4 for a, b in zip(idx, idx[1:])):
                continue
            times = [beats[i][0] for i in idx]
            lens = [b - a for a, b in zip(times, times[1:])]
            if max(lens) > 1.12 * min(lens):
                continue
            chs = [bar_chord(chords, a, b) for a, b in zip(times, times[1:])]
            if any(c is None for c in chs):
                continue
            tonic, fit = key_of_window(chs, hint)
            if fit < 6.0:
                continue
            stats["windows"] += 1
            for track, src in (("MELODY", 0), ("BRIDGE", 1)):
                notes = tracks.get(track)
                if not notes:
                    continue
                ps = make_passage(quantize(notes, times), chs, tonic)
                if ps:
                    ps.update(src=src, song=song)
                    out.append(features(ps))


def select(passages, caps, per_song):
    seen, uniq = {}, []
    for ps in passages:
        k = key_hash(ps)
        if k in seen:
            seen[k]["count"] += 1
            continue
        ps["count"] = 1
        seen[k] = ps
        uniq.append(ps)
    out = []
    for src, cap in enumerate(caps):
        pool = [p for p in uniq if p["src"] == src]
        # hooki (powtarzane w piosence) i dobre dopasowanie do typowych progresji najpierw
        pool.sort(key=lambda p: -(p["match"] + 0.25 * min(2, p["count"] - 1)))
        bins = collections.defaultdict(list)
        for p in pool:
            bins[min(9, int(p["energy"] * 10))].append(p)
        per, chosen = collections.Counter(), []
        per_bin = cap // 10
        for _ in range(2):  # drugie przejście dobiera z pełnych przedziałów
            for b in range(10):
                taken = sum(1 for p in chosen if min(9, int(p["energy"] * 10)) == b)
                for p in bins[b]:
                    if taken >= per_bin or len(chosen) >= cap:
                        break
                    if p.get("taken") or per[p["song"]] >= per_song:
                        continue
                    p["taken"] = True
                    per[p["song"]] += 1
                    chosen.append(p)
                    taken += 1
            per_bin = cap  # w drugim przejściu bez limitu przedziału
        out += chosen
    return out


def emit(path, passages, stats):
    passages = sorted(passages, key=lambda p: (p["src"], p["energy"]))
    src = collections.Counter(p["src"] for p in passages)
    lines = ["// WYGENEROWANE przez tools/extract_guitar.py – nie edytuj ręcznie.",
             f"// Źródło: {stats['songs']} utworów POP909, {stats['windows']} fragmentów 8-taktowych w jednej tonacji;",
             f"// wybrano {src[0]} pasaży MELODY i {src[1]} pasaży BRIDGE.",
             "// POP909 (MIT, Music X Lab).",
             "#pragma once", "#include \"GuitarTypes.h\"", "", "namespace sk::data {", ""]
    notes, heads = [], []
    for p in passages:
        first = len(notes)
        notes += p["notes"]
        heads.append((first, p))
    lines.append("inline constexpr PassageNote guitarNotes[] = {")
    for i in range(0, len(notes), 6):
        lines.append("    " + " ".join(f"{{{a},{b},{c},{d},{e}}}," for a, b, c, d, e in notes[i:i + 6]))
    lines.append("};")
    lines.append("inline constexpr Passage guitarPassages[] = {")
    for first, p in heads:
        ch = ",".join(f"{{{d},{q}}}" for d, q in p["chords"])
        lines.append(f"    {{{first},{len(p['notes'])},{{{ch}}},{p['src']},{min(p['count'], 255)},"
                     f"{int(round(p['centre']))},{p['energy']:.3f}f,{p['dens']:.3f}f,{int(p['song'])}}},")
    lines.append("};")
    lines.append(f"inline constexpr int guitarPassageCount = {len(heads)};")
    lines.append("")
    lines.append("} // namespace sk::data")
    with open(path, "w", encoding="utf-8") as fh:
        fh.write("\n".join(lines) + "\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pop909", required=True)
    ap.add_argument("--melody-cap", type=int, default=600)
    ap.add_argument("--bridge-cap", type=int, default=300)
    ap.add_argument("--per-song", type=int, default=2)
    ap.add_argument("--out", default=os.path.join(os.path.dirname(__file__), "..", "Source", "Core", "GuitarPhraseData.h"))
    a = ap.parse_args()
    passages, stats = [], collections.Counter()
    pop909_passages(a.pop909, passages, stats)
    print(f"surowe pasaże: {len(passages)}; {dict(stats)}")
    chosen = select(passages, (a.melody_cap, a.bridge_cap), a.per_song)
    src = collections.Counter(p["src"] for p in chosen)
    good = sum(1 for p in chosen if p["match"] >= 0.75)
    print(f"wybrane: {len(chosen)} (MELODY {src[0]}, BRIDGE {src[1]}), dobrze pasujących do progresji: {good}")
    emit(a.out, chosen, stats)


if __name__ == "__main__":
    main()
