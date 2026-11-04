#!/usr/bin/env python3
"""Ekstrakcja biblioteki fraz gitary prowadzącej z POP909 (i opcjonalnie Lakh MIDI).

Wynik: Source/Core/GuitarPhraseData.h (dane wkompilowane we wtyczkę Gitara).

Fraza = dwa takty 4/4 (siatka 32 szesnastek), w każdym takcie jeden akord.
Źródła w POP909:
  MELODY – linia wokalu (to są refreny i zwrotki znanych popowych piosenek),
  BRIDGE – instrumentalne kontrmelodie i wstawki między frazami wokalu
           (najbardziej „gitarowy” materiał w zbiorze).
Każdą nutę zapisujemy względem pryma akordu *swojego* taktu, razem z jakością
obu akordów, więc w czasie gry fraza da się przenieść na dowolne dwa akordy
z progresji Piano (bez zgrzytów, patrz remapInterval w Harmony.h).

Frazy, które powtarzają się w piosence (hooki refrenu), mają pierwszeństwo.
Do biblioteki dochodzi zestaw krótkich idiomów popowej gitary (IDIOMS niżej),
napisanych ręcznie: pentatonika, seksty, zawieszenia, ósemki „na delay”.

Lakh (--lakh DIR) dodaje frazy ze ścieżek gitary prowadzącej (GM 24–31,
monofoniczne). Lakh zawiera materiał chroniony prawem autorskim, więc taka
biblioteka jest wyłącznie do użytku prywatnego.

Użycie:
  python3 tools/extract_guitar.py --pop909 /ścieżka/POP909 [--lakh /ścieżka/clean_midi] \
         [--out Source/Core/GuitarPhraseData.h]
"""
import argparse, collections, hashlib, os, re, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from extract_licks import parse_chord, midi_notes, chord_at, guess_chord, POP_ARTISTS  # noqa: E402

STEPS = 32
REF_LO = 60          # prymę akordu liczymy w oktawie C4..B4
MAJ, MIN = 0, 1

# Idiomy popowej gitary prowadzącej: (jakość akordu taktu 1, taktu 2, nuty).
# Nuta: (krok 0..31, długość w 16-tkach, interwał od pryma akordu swojego taktu, dynamika).
# Interwały: 0 pryma, 2 nona, 4/3 tercja, 7 kwinta, 9 seksta, 12 oktawa.
IDIOMS = [
    # hook pentatoniczny z długim zakończeniem na tercji
    (MAJ, MAJ, [(0, 2, 7, 96), (2, 2, 9, 84), (4, 4, 12, 100), (10, 2, 9, 80), (12, 4, 7, 90),
                (16, 2, 4, 92), (18, 2, 7, 84), (20, 10, 4, 98)]),
    # ósemki „na delay” (kwinta-oktawa-nona)
    (MAJ, MAJ, [(s, 2, (7, 12, 14, 12)[s // 2 % 4], 88 if s % 4 == 0 else 72) for s in range(0, 32, 2)]),
    # wejście z przedtaktu, seksty w dół
    (MAJ, MAJ, [(2, 2, 16, 90), (4, 6, 14, 96), (12, 2, 12, 82), (14, 2, 9, 80), (16, 8, 7, 94),
                (26, 2, 4, 80), (28, 4, 7, 86)]),
    # zawieszenie 2 -> 3, długi dźwięk z wibracją
    (MAJ, MAJ, [(0, 3, 0, 90), (3, 3, 2, 86), (6, 10, 4, 100), (20, 2, 7, 84), (22, 2, 4, 80), (24, 8, 2, 88)]),
    # riff synkopowany wokół kwinty
    (MAJ, MAJ, [(0, 1, 7, 100), (3, 1, 7, 84), (6, 2, 9, 92), (8, 2, 7, 84), (11, 3, 4, 96),
                (16, 1, 7, 100), (19, 1, 7, 84), (22, 2, 9, 92), (24, 8, 12, 98)]),
    # odpowiedź: cisza, potem zjazd z oktawy
    (MAJ, MAJ, [(8, 2, 16, 90), (10, 2, 14, 84), (12, 4, 12, 96), (16, 2, 9, 86), (18, 2, 7, 80),
                (20, 12, 4, 94)]),
    # moll: pentatonika molowa z tercją na końcu
    (MIN, MIN, [(0, 2, 7, 96), (2, 2, 10, 86), (4, 6, 12, 100), (12, 2, 10, 82), (14, 2, 7, 80),
                (16, 4, 3, 94), (20, 2, 5, 80), (22, 10, 3, 96)]),
    # moll: arpeggio w górę i zejście
    (MIN, MIN, [(0, 2, 0, 90), (2, 2, 3, 82), (4, 2, 7, 86), (6, 6, 12, 98), (16, 2, 10, 86),
                (18, 2, 7, 80), (20, 4, 3, 90), (24, 8, 0, 92)]),
    # moll: ósemki z kwintą na „i”
    (MIN, MIN, [(s, 2, (12, 7, 10, 7)[s // 2 % 4], 90 if s % 4 == 0 else 70) for s in range(0, 32, 2)]),
    # spokojny motyw: dwie długie nuty i ozdobnik
    (MAJ, MAJ, [(0, 8, 4, 92), (8, 2, 2, 80), (10, 6, 0, 86), (16, 8, 7, 94), (24, 2, 9, 80), (26, 6, 7, 86)]),
    # energiczne 16-tki (solo): skala pentatoniczna w górę
    (MAJ, MAJ, [(s, 1, (0, 2, 4, 7, 9, 12, 14, 16)[s % 8] + (12 if s >= 16 else 0) - (12 if s >= 24 else 0),
                 100 if s % 4 == 0 else 78) for s in range(0, 28)] + [(28, 4, 12, 104)]),
    # krótkie wtrącenie na końcu drugiego taktu (miejsce dla wokalu)
    (MAJ, MAJ, [(20, 1, 7, 90), (21, 1, 9, 84), (22, 2, 12, 96), (24, 2, 9, 86), (26, 6, 7, 92)]),
]


def quantize(notes, t0, t1):
    """Kwantyzacja dwóch taktów do 32 kroków; linia monofoniczna (górny głos)."""
    dur = t1 - t0
    by_step = {}
    for s, e, p, v in notes:
        if s < t0 - dur / 64 or s >= t1 - dur / 64:
            continue
        st = int(round((s - t0) / dur * STEPS))
        if not 0 <= st < STEPS:
            continue
        ln = max(1, int(round((e - s) / dur * STEPS)))
        if st not in by_step or p > by_step[st][1]:
            by_step[st] = (ln, p, v)
    steps = sorted(by_step)
    out = []
    for i, st in enumerate(steps):
        ln, p, v = by_step[st]
        nxt = steps[i + 1] if i + 1 < len(steps) else STEPS
        lim = (16 if st < 16 else 32) - st          # nuta nie przechodzi przez kreskę taktową
        out.append((st, max(1, min(ln, nxt - st, lim)), p, v))
    return out


def make_phrase(q, ch0, ch1):
    if len(q) < 4 or not any(st < 16 for st, *_ in q) or q[0][0] >= 12:
        return None
    if sum(1 for st, *_ in q if st < 16) > 14 or sum(1 for st, *_ in q if st >= 16) > 14:
        return None
    pitches = [p for _, _, p, _ in q]
    if max(pitches) - min(pitches) > 19:
        return None
    if any(abs(a - b) > 9 for a, b in zip(pitches, pitches[1:])):
        return None
    mean_v = sum(v for *_, v in q) / len(q)
    notes = []
    for st, ln, p, v in q:
        root = ch0[0] if st < 16 else ch1[0]
        ref = REF_LO + ((root - REF_LO) % 12)
        rel = p - ref
        if not -24 <= rel <= 30:
            return None
        vel = max(30, min(127, int(round(90 + (v - mean_v) * 0.8))))
        notes.append((st, ln, rel, vel))
    return dict(notes=notes, q=(ch0[1], ch1[1]))


def features(ph):
    notes = ph["notes"]
    mask = 0
    for st, *_ in notes:
        mask |= 1 << st
    n = len(notes)
    dens = n / STEPS
    sync = sum(1 for st, *_ in notes if st % 2 == 1) / n
    rels = [r for _, _, r, _ in notes]
    span = max(rels) - min(rels)
    longest = max(ln for _, ln, _, _ in notes)
    centre = sum(rels) / n
    ph.update(mask=mask, dens=dens, sync=sync, span=span, centre=centre,
              energy=min(1.0, 0.55 * dens / 0.45 + 0.2 * sync + 0.15 * span / 19 + 0.1 * (1 - min(1, longest / 8))))
    return ph


def key_of(ph):
    return hashlib.md5(repr((ph["q"][0] in (1, 4), ph["q"][1] in (1, 4),
                             [(a, b, c) for a, b, c, _ in ph["notes"]])).encode()).hexdigest()


def pop909_phrases(root, out, stats):
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
        downs = [i for i, b in enumerate(beats) if b[2] == 1.0]
        for track, src in (("MELODY", 0), ("BRIDGE", 1)):
            try:
                notes, _ = midi_notes(mid, lambda name, prog, drum, t=track: name.strip().upper() == t)
            except Exception as e:
                print("pomijam", mid, e, file=sys.stderr)
                break
            if not notes:
                continue
            for a, b, c in zip(downs, downs[1:], downs[2:]):
                if b - a != 4 or c - b != 4:
                    continue
                t0, t1, t2 = beats[a][0], beats[b][0], beats[c][0]
                if abs((t2 - t1) - (t1 - t0)) > 0.1 * (t1 - t0):
                    continue
                ch0, ch1 = chord_at(chords, t0, t1), chord_at(chords, t1, t2)
                if not ch0 or not ch1:
                    continue
                ph = make_phrase(quantize(notes, t0, t2), ch0, ch1)
                stats["phrases"] += 1
                if ph:
                    ph["src"] = src
                    ph["song"] = "pop909:" + song
                    out.append(features(ph))
        stats["songs"] += 1


def lakh_phrases(root, out, stats, limit):
    files = []
    for artist in sorted(os.listdir(root)):
        if POP_ARTISTS.search(artist):
            ad = os.path.join(root, artist)
            files += [os.path.join(ad, f) for f in sorted(os.listdir(ad)) if f.lower().endswith(".mid")]
    for path in files[:limit]:
        try:
            lead, mf = midi_notes(path, lambda name, prog, drum: (not drum) and 24 <= prog <= 31
                                  and not re.search(r"rhythm|chord|strum", name or "", re.I))
            rest, _ = midi_notes(path, lambda name, prog, drum: (not drum) and not 24 <= prog <= 31)
        except Exception:
            continue
        if not lead:
            continue
        tempos = [m.tempo for tr in mf.tracks for m in tr if m.type == "set_tempo"]
        if len(set(tempos)) > 1:
            continue
        bar = 4 * (tempos[0] if tempos else 500000) / 1e6
        end = max(e for _, e, _, _ in lead)
        t = 0.0
        while t + 2 * bar <= end:
            h0 = [(p, e - s) for s, e, p, _ in rest if s < t + bar and e > t]
            h1 = [(p, e - s) for s, e, p, _ in rest if s < t + 2 * bar and e > t + bar]
            ch0, ch1 = guess_chord(h0), guess_chord(h1)
            if ch0 and ch1:
                ph = make_phrase(quantize(lead, t, t + 2 * bar), ch0, ch1)
                if ph:
                    ph["src"] = 3
                    ph["song"] = "lakh:" + path
                    out.append(features(ph))
            t += 2 * bar
        stats["lakh_songs"] += 1


def select(phrases, cap, per_song):
    seen, uniq = {}, []
    for ph in phrases:
        k = key_of(ph)
        if k in seen:
            seen[k]["count"] += 1
            continue
        ph["count"] = 1
        seen[k] = ph
        uniq.append(ph)
    uniq.sort(key=lambda ph: (-ph["count"], ph["src"] != 1))  # hooki, potem BRIDGE
    bins = collections.defaultdict(list)
    for ph in uniq:
        bins[min(9, int(ph["energy"] * 10))].append(ph)
    out, per = [], collections.Counter()
    per_bin = cap // 10
    for b in range(10):
        taken = 0
        for ph in bins[b]:
            if taken >= per_bin:
                break
            if per[ph["song"]] >= per_song:
                continue
            per[ph["song"]] += 1
            out.append(ph)
            taken += 1
    return out


def emit(path, phrases, stats):
    phrases = sorted(phrases, key=lambda ph: ph["energy"])
    lines = ["// WYGENEROWANE przez tools/extract_guitar.py – nie edytuj ręcznie.",
             f"// Źródło: {stats['songs']} utworów POP909 (MELODY + BRIDGE), {stats['phrases']} par taktów"
             f" z jednym akordem w takcie, {len(IDIOMS)} idiomów gitarowych.",
             "// POP909 (MIT, Music X Lab). Dane Lakh (jeśli są) wyłącznie do użytku prywatnego.",
             "#pragma once", "#include \"GuitarTypes.h\"", "", "namespace sk::data {", ""]
    notes, heads = [], []
    for ph in phrases:
        first = len(notes)
        notes += ph["notes"]
        heads.append((first, len(ph["notes"]), ph["q"][0], ph["q"][1], ph["src"], ph["energy"], ph["dens"],
                      ph["mask"], int(round(ph["centre"])), ph["count"]))
    lines.append("inline constexpr PhraseNote guitarNotes[] = {")
    for i in range(0, len(notes), 6):
        lines.append("    " + " ".join(f"{{{a},{b},{c},{d}}}," for a, b, c, d in notes[i:i + 6]))
    lines.append("};")
    lines.append("inline constexpr Phrase guitarPhrases[] = {")
    for f, c, q0, q1, src, e, d, m, ce, cnt in heads:
        lines.append(f"    {{{f},{c},{{{q0},{q1}}},{src},{min(cnt, 255)},{ce},{e:.3f}f,{d:.3f}f,0x{m:08x}u}},")
    lines.append("};")
    lines.append(f"inline constexpr int guitarPhraseCount = {len(heads)};")
    lines.append("")
    lines.append("} // namespace sk::data")
    with open(path, "w", encoding="utf-8") as fh:
        fh.write("\n".join(lines) + "\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pop909", required=True)
    ap.add_argument("--lakh")
    ap.add_argument("--lakh-limit", type=int, default=3000)
    ap.add_argument("--cap", type=int, default=700)
    ap.add_argument("--per-song", type=int, default=3)
    ap.add_argument("--out", default=os.path.join(os.path.dirname(__file__), "..", "Source", "Core", "GuitarPhraseData.h"))
    a = ap.parse_args()
    phrases, stats = [], collections.Counter()
    pop909_phrases(a.pop909, phrases, stats)
    if a.lakh:
        lakh_phrases(a.lakh, phrases, stats, a.lakh_limit)
    print(f"surowe frazy: {len(phrases)}; {dict(stats)}")
    chosen = select(phrases, a.cap, a.per_song)
    for q0, q1, ns in IDIOMS:
        ph = features(dict(notes=sorted(ns), q=(q0, q1), src=2, song="idiom", count=3))
        chosen.append(ph)
    src = collections.Counter(ph["src"] for ph in chosen)
    print(f"wybrane: {len(chosen)} (MELODY {src[0]}, BRIDGE {src[1]}, idiomy {src[2]}, Lakh {src[3]})")
    emit(a.out, chosen, stats)


if __name__ == "__main__":
    main()
