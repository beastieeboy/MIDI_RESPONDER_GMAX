#!/usr/bin/env python3
"""Ekstrakcja biblioteki licków piano/bass z POP909 (i opcjonalnie Lakh MIDI).

Wynik: Source/Core/LickLibraryData.h (dane wkompilowane we wtyczki).

Licki są jednotaktowe (4/4, siatka 16-tek). Wysokość dźwięku zapisujemy względem
pryma akordu (interwał w półtonach) razem z jakością akordu źródłowego, więc
w runtime lick da się przenieść na dowolny akord z okna progresji.

POP909 (MIT) nie ma ścieżki basu – linię basu wyciągamy z najniższych dźwięków
lewej ręki partii PIANO. Lakh (--lakh DIR) daje prawdziwe ścieżki basowe
(program GM 32–39), ale nie ma adnotacji akordów: akord taktu wnioskujemy wtedy
z dźwięków wszystkich pozostałych ścieżek. Lakh to dane do użytku prywatnego.

Użycie:
  python3 tools/extract_licks.py --pop909 /ścieżka/POP909 [--lakh /ścieżka/clean_midi] \
         [--out Source/Core/LickLibraryData.h]
"""
import argparse, collections, hashlib, os, random, re, sys
import mido

QUAL = {"maj": 0, "min": 1, "7": 2, "maj7": 3, "min7": 4, "dim": 5, "sus": 6}
QUAL_NAMES = {v: k for k, v in QUAL.items()}
PCS = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}

POP_ARTISTS = re.compile(
    r"(beatles|abba|madonna|michael jackson|whitney|mariah|elton john|george michael|"
    r"backstreet|spice girls|britney|celine dion|phil collins|bon jovi|queen|"
    r"coldplay|maroon 5|adele|taylor swift|robbie williams|take that|roxette|"
    r"a-ha|duran duran|tears for fears|wham|cyndi lauper|lionel richie|"
    r"stevie wonder|bee gees|eurythmics|simply red|toto|fleetwood mac|"
    r"billy joel|elton|sting|police|u2|oasis|keane|james blunt|norah jones|"
    r"bruno mars|ed sheeran|katy perry|lady gaga|rihanna|beyonce|justin)", re.I)


def parse_chord(name):
    """'C#:min7/b3' -> (root_pc, quality) albo None dla 'N'."""
    if name in ("N", "X"):
        return None
    root, _, rest = name.partition(":")
    pc = PCS.get(root[0])
    if pc is None:
        return None
    for acc in root[1:]:
        pc += 1 if acc == "#" else -1 if acc == "b" else 0
    q = rest.split("/")[0].split("(")[0]
    if q.startswith("maj7") or q in ("maj9", "maj6"):
        qual = QUAL["maj7"]
    elif q.startswith("min7") or q in ("min9", "min11", "hdim7"):
        qual = QUAL["min7"] if q != "hdim7" else QUAL["dim"]
    elif q.startswith("min"):
        qual = QUAL["min"]
    elif q in ("7", "9", "11", "13"):
        qual = QUAL["7"]
    elif q.startswith("dim"):
        qual = QUAL["dim"]
    elif q.startswith("sus"):
        qual = QUAL["sus"]
    else:
        qual = QUAL["maj"]
    return pc % 12, qual


def midi_notes(path, track_filter):
    """Zwraca listę (start_s, end_s, pitch, vel) dla ścieżek spełniających filtr."""
    mf = mido.MidiFile(path)
    tempo_map = []  # (tick, tempo)
    for tr in mf.tracks:
        t = 0
        for msg in tr:
            t += msg.time
            if msg.type == "set_tempo":
                tempo_map.append((t, msg.tempo))
    tempo_map.sort()
    if not tempo_map or tempo_map[0][0] != 0:
        tempo_map.insert(0, (0, 500000))

    def t2s(tick):
        s, last_t, last_tempo = 0.0, 0, tempo_map[0][1]
        for tt, tempo in tempo_map[1:]:
            if tt > tick:
                break
            s += (tt - last_t) * last_tempo / 1e6 / mf.ticks_per_beat
            last_t, last_tempo = tt, tempo
        return s + (tick - last_t) * last_tempo / 1e6 / mf.ticks_per_beat

    out = []
    for tr in mf.tracks:
        program, is_drum = 0, False
        notes, t, on = [], 0, {}
        for msg in tr:
            t += msg.time
            if msg.type == "program_change":
                program = msg.program
            if msg.type in ("note_on", "note_off"):
                if getattr(msg, "channel", 0) == 9:
                    is_drum = True
                key = (msg.channel, msg.note)
                if msg.type == "note_on" and msg.velocity > 0:
                    on[key] = (t, msg.velocity)
                elif key in on:
                    st, v = on.pop(key)
                    notes.append((st, t, msg.note, v))
        if notes and track_filter(tr.name, program, is_drum):
            out.extend((t2s(a), t2s(b), p, v) for a, b, p, v in notes)
    return out, mf


def quantize_bar(notes, bar_t0, bar_t1):
    """Kwantyzacja do 16 kroków. Zwraca listę (step, len, pitch, vel)."""
    dur = bar_t1 - bar_t0
    res = []
    for s, e, p, v in notes:
        if s < bar_t0 - dur / 32 or s >= bar_t1 - dur / 32:
            continue
        step = int(round((s - bar_t0) / dur * 16))
        if not 0 <= step < 16:
            continue
        ln = max(1, int(round((e - s) / dur * 16)))
        res.append((step, min(ln, 16 - step), p, v))
    return res


def bass_line(qnotes, max_pitch=55):
    """Monofoniczna linia z najniższych dźwięków (po jednym na krok)."""
    by_step = collections.defaultdict(list)
    for st, ln, p, v in qnotes:
        if p <= max_pitch:
            by_step[st].append((p, ln, v))
    steps = sorted(by_step)
    line = []
    for i, st in enumerate(steps):
        p, ln, v = min(by_step[st])
        nxt = steps[i + 1] if i + 1 < len(steps) else 16
        line.append((st, max(1, min(ln, nxt - st)), p, v))
    return line


def features(q):
    onsets = sorted({n[0] for n in q})
    mask = 0
    for s in onsets:
        mask |= 1 << s
    dens = len(onsets) / 16.0
    sync = sum(1 for s in onsets if s % 2 == 1) / max(1, len(onsets))
    vel = sum(n[3] for n in q) / max(1, len(q)) / 127.0
    poly = len(q) / max(1, len(onsets))
    return dens, sync, vel, poly, mask


def make_lick(q, root_pc, qual, ref_lo, kind):
    """Interwały względem pryma w oktawie zaczynającej się od ref_lo."""
    ref = ref_lo + ((root_pc - ref_lo) % 12)
    notes = []
    for st, ln, p, v in q:
        rel = p - ref
        if -24 <= rel <= 36:
            notes.append((st, ln, rel, v))
    if len(notes) < (1 if kind == "bass" else 2):
        return None
    notes.sort()
    dens, sync, vel, poly, mask = features([(a, b, c, d) for a, b, c, d in notes])
    return dict(notes=notes, qual=qual, dens=dens, sync=sync, vel=vel, poly=poly, mask=mask)


def lick_key(l):
    return hashlib.md5(repr((l["qual"] in (1, 4), [(a, b, c) for a, b, c, _ in l["notes"]])).encode()).hexdigest()


def chord_at(chords, t0, t1):
    """Akord trwający przez (prawie) cały takt, inaczej None."""
    best = None
    for a, b, c in chords:
        ov = min(b, t1) - max(a, t0)
        if ov > 0.85 * (t1 - t0):
            best = c
    return best


def pop909_licks(root, piano, bass, stats):
    for song in sorted(os.listdir(root)):
        d = os.path.join(root, song)
        mid = os.path.join(d, song + ".mid")
        if not os.path.isfile(mid):
            continue
        try:
            notes, _ = midi_notes(mid, lambda name, prog, drum: name.strip().upper() == "PIANO")
        except Exception as e:  # uszkodzony plik
            print("pomijam", mid, e, file=sys.stderr)
            continue
        beats = [tuple(map(float, l.split())) for l in open(os.path.join(d, "beat_midi.txt")) if l.strip()]
        chords = []
        for l in open(os.path.join(d, "chord_midi.txt")):
            parts = l.split()
            if len(parts) >= 3:
                chords.append((float(parts[0]), float(parts[1]), parse_chord(parts[2])))
        downs = [i for i, b in enumerate(beats) if b[2] == 1.0]
        for a, b in zip(downs, downs[1:]):
            if b - a != 4:
                continue  # tylko takty 4/4
            t0, t1 = beats[a][0], beats[b][0]
            ch = chord_at(chords, t0, t1)
            if not ch:
                continue
            q = quantize_bar(notes, t0, t1)
            if not q:
                continue
            stats["bars"] += 1
            lp = make_lick(q, ch[0], ch[1], 48, "piano")
            if lp:
                lp["src"] = "pop909:" + song
                piano.append(lp)
            lb = make_lick(bass_line(q), ch[0], ch[1], 28, "bass")
            if lb:
                lb["src"] = "pop909:" + song
                bass.append(lb)
        stats["songs"] += 1


def guess_chord(pitches_w):
    """Najprostsze dopasowanie szablonów triad/septym do histogramu klas wysokości."""
    tmpl = {0: (0, 4, 7), 1: (0, 3, 7), 2: (0, 4, 7, 10), 3: (0, 4, 7, 11), 4: (0, 3, 7, 10)}
    hist = [0.0] * 12
    for p, w in pitches_w:
        hist[p % 12] += w
    tot = sum(hist)
    if tot <= 0:
        return None
    best, bs = None, -1e9
    for r in range(12):
        for q, iv in tmpl.items():
            s = sum(hist[(r + i) % 12] for i in iv) - 0.6 * sum(hist[(r + i) % 12] for i in range(12) if i not in iv)
            s -= 0.05 * len(iv) * tot / 12  # lekka kara za septymy
            if s > bs:
                best, bs = (r, q), s
    return best if bs > 0.35 * tot else None


def lakh_licks(root, piano, bass, stats, limit):
    files = []
    for dp, _, fn in os.walk(root):
        artist = os.path.basename(dp)
        if not POP_ARTISTS.search(artist):
            continue
        files += [os.path.join(dp, f) for f in fn if f.lower().endswith((".mid", ".midi"))]
    random.Random(7).shuffle(files)
    for path in files[:limit]:
        try:
            allnotes, mf = midi_notes(path, lambda n, prog, drum: not drum)
            bassn, _ = midi_notes(path, lambda n, prog, drum: not drum and 32 <= prog <= 39)
            pnon, _ = midi_notes(path, lambda n, prog, drum: not drum and prog <= 7)
        except Exception:
            continue
        ts = [m for tr in mf.tracks for m in tr if m.type == "time_signature"]
        if ts and (ts[0].numerator, ts[0].denominator) != (4, 4):
            continue
        tempos = [m.tempo for tr in mf.tracks for m in tr if m.type == "set_tempo"]
        if len(set(tempos)) > 1:
            continue  # tylko stałe tempo – prosta siatka taktów
        bar = 4 * (tempos[0] if tempos else 500000) / 1e6
        end = max((e for _, e, _, _ in allnotes), default=0)
        t = 0.0
        while t + bar <= end:
            harm = [(p, e - s) for s, e, p, _ in allnotes if s < t + bar and e > t]
            ch = guess_chord(harm)
            if ch:
                stats["bars"] += 1
                if bassn:
                    lb = make_lick(bass_line(quantize_bar(bassn, t, t + bar), 60), ch[0], ch[1], 28, "bass")
                    if lb:
                        lb["src"] = "lakh"
                        bass.append(lb)
                if pnon:
                    lp = make_lick(quantize_bar(pnon, t, t + bar), ch[0], ch[1], 48, "piano")
                    if lp:
                        lp["src"] = "lakh"
                        piano.append(lp)
            t += bar
        stats["songs"] += 1


def energy(l, kind):
    if kind == "piano":
        return min(1.0, 0.45 * l["dens"] / 0.6 + 0.2 * l["sync"] + 0.25 * l["vel"] + 0.1 * min(1, l["poly"] / 4))
    return min(1.0, 0.55 * l["dens"] / 0.5 + 0.2 * l["sync"] + 0.25 * l["vel"])


def select(licks, kind, cap, per_song=6):
    """Deduplikacja + równomierny rozkład energii + limit licków z jednej piosenki."""
    seen, uniq = {}, []
    for l in licks:
        k = lick_key(l)
        if k in seen:
            seen[k]["count"] += 1
            continue
        l["count"] = 1
        l["energy"] = energy(l, kind)
        seen[k] = l
        uniq.append(l)
    # popularne wzorce (powtarzane w wielu taktach) są bardziej "popowe"
    uniq.sort(key=lambda l: -l["count"])
    bins = collections.defaultdict(list)
    for l in uniq:
        bins[min(9, int(l["energy"] * 10))].append(l)
    out, song_n = [], collections.Counter()
    per_bin = cap // 10
    for b in range(10):
        taken = 0
        for l in bins[b]:
            if taken >= per_bin:
                break
            if song_n[l["src"]] >= per_song and l["src"] != "lakh":
                continue
            song_n[l["src"]] += 1
            out.append(l)
            taken += 1
    out.sort(key=lambda l: l["energy"])
    return out


def emit(path, piano, bass, stats):
    lines = ["// WYGENEROWANE przez tools/extract_licks.py – nie edytuj ręcznie.",
             f"// Źródło: {stats['songs']} utworów, {stats['bars']} taktów 4/4 z jednym akordem.",
             "// POP909 (MIT, Music X Lab). Dane Lakh (jeśli są) wyłącznie do użytku prywatnego.",
             "#pragma once", "#include \"LickTypes.h\"", "", "namespace sk::data {", ""]
    for name, licks in (("piano", piano), ("bass", bass)):
        notes, heads = [], []
        for l in licks:
            first = len(notes)
            notes += l["notes"]
            heads.append((first, len(l["notes"]), l["qual"], l["energy"], l["dens"], l["sync"], l["mask"]))
        lines.append(f"inline constexpr LickNote {name}Notes[] = {{")
        for i in range(0, len(notes), 6):
            lines.append("    " + " ".join(f"{{{a},{b},{c},{d}}}," for a, b, c, d in notes[i:i + 6]))
        lines.append("};")
        lines.append(f"inline constexpr Lick {name}Licks[] = {{")
        for f, c, q, e, d, s, m in heads:
            lines.append(f"    {{{f},{c},{q},{e:.3f}f,{d:.3f}f,{s:.3f}f,0x{m:04x}}},")
        lines.append("};")
        lines.append(f"inline constexpr int {name}LickCount = {len(heads)};")
        lines.append("")
    lines.append("} // namespace sk::data")
    with open(path, "w", encoding="utf-8") as fh:
        fh.write("\n".join(lines) + "\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pop909", required=True)
    ap.add_argument("--lakh")
    ap.add_argument("--lakh-limit", type=int, default=3000)
    ap.add_argument("--piano-cap", type=int, default=800)
    ap.add_argument("--bass-cap", type=int, default=500)
    ap.add_argument("--out", default=os.path.join(os.path.dirname(__file__), "..", "Source", "Core", "LickLibraryData.h"))
    a = ap.parse_args()
    piano, bass, stats = [], [], collections.Counter()
    pop909_licks(a.pop909, piano, bass, stats)
    if a.lakh:
        lakh_licks(a.lakh, piano, bass, stats, a.lakh_limit)
    print(f"surowe: piano {len(piano)}, bass {len(bass)}; {dict(stats)}")
    p, b = select(piano, "piano", a.piano_cap), select(bass, "bass", a.bass_cap)
    print(f"wybrane: piano {len(p)}, bass {len(b)}")
    emit(a.out, p, b, stats)


if __name__ == "__main__":
    main()
