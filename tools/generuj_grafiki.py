"""Generator grafik 'Sekcja Krzemowa' – istota krzemowa (SVG, zgodne z juce::Drawable:
tylko wielokąty, ścieżki, gradienty i przezroczystość; bez filtrów i tekstu)."""
import math, random, os, sys

OUT = sys.argv[1]

P = dict(
    T=(200, 44), TL=(140, 66), TR=(260, 66), L1=(104, 112), R1=(296, 112),
    L2=(92, 172), R2=(308, 172), L3=(104, 232), R3=(296, 232), L4=(134, 284), R4=(266, 284),
    CL=(170, 322), CR=(230, 322), C=(200, 332),
    F=(200, 92), FL=(158, 118), FR=(242, 118), BL=(150, 160), BR=(250, 160), N=(200, 158),
    NT=(200, 226), CKL=(146, 236), CKR=(254, 236), M=(200, 272), ML=(172, 268), MR=(228, 268), J=(200, 306),
)
S = 0.86
P = {k: (200 + (x - 200) * S, 196 + (y - 190) * S) for k, (x, y) in P.items()}
HT = f'transform="translate(200 196) scale({S}) translate(-200 -190)"'
TRIS = """T TL F|T F TR|TL FL F|TR F FR|TL L1 FL|TR FR R1|L1 BL FL|R1 FR BR|F FL N|F N FR|FL BL N|FR N BR|
L1 L2 BL|R1 BR R2|L2 CKL BL|R2 BR CKR|BL CKL NT|BL NT N|BR N NT|BR NT CKR|L2 L3 CKL|R2 CKR R3|L3 L4 CKL|
R3 CKR R4|CKL ML NT|NT ML M|NT M MR|NT MR CKR|L4 CL ML|L4 ML CKL|R4 MR CR|R4 CKR MR|CL J ML|ML J M|M J MR|
MR J CR|CL C J|J C CR"""

VARIANTS = {
    "logo":  dict(acc="#a99bff", acc2="#5ee8dc", hi="#f2efff", halo="rhythm"),
    "piano": dict(acc="#ffbe73", acc2="#ff8f6b", hi="#fff4e4", halo="keys"),
    "bass":  dict(acc="#55e6d6", acc2="#4c9dff", hi="#e6fffb", halo="strings"),
    "wokal": dict(acc="#ff7ad9", acc2="#b07bff", hi="#ffe9f8", halo="grains"),
}

CX, CY, R = 200, 205, 190


def mix(a, b, t):
    a = [int(a[i:i + 2], 16) for i in (1, 3, 5)]
    b = [int(b[i:i + 2], 16) for i in (1, 3, 5)]
    return "#" + "".join(f"{round(x + (y - x) * t):02x}" for x, y in zip(a, b))


def pts(seq):
    return " ".join(f"{x:.1f},{y:.1f}" for x, y in seq)


def defs(v, pre):
    return f"""<defs>
 <linearGradient id="{pre}iri" x1="0" y1="0" x2="1" y2="1">
  <stop offset="0" stop-color="{v['acc2']}"/><stop offset="0.5" stop-color="{v['acc']}"/><stop offset="1" stop-color="#ffd27a"/>
 </linearGradient>
 <radialGradient id="{pre}bg" cx="0.5" cy="0.42" r="0.6">
  <stop offset="0" stop-color="#1e2738"/><stop offset="0.7" stop-color="#121822"/><stop offset="1" stop-color="#0b0f16"/>
 </radialGradient>
 <radialGradient id="{pre}glow" cx="0.5" cy="0.5" r="0.5">
  <stop offset="0" stop-color="{v['acc']}" stop-opacity="0.85"/><stop offset="0.45" stop-color="{v['acc']}" stop-opacity="0.25"/>
  <stop offset="1" stop-color="{v['acc']}" stop-opacity="0"/>
 </radialGradient>
 <radialGradient id="{pre}aura" cx="0.5" cy="0.45" r="0.5">
  <stop offset="0" stop-color="{v['acc']}" stop-opacity="0.22"/><stop offset="1" stop-color="{v['acc']}" stop-opacity="0"/>
 </radialGradient>
 <clipPath id="{pre}wafer"><path d="M126,380 A190,190 0 1 1 274,380 Z"/></clipPath>
</defs>"""


def traces(rng):
    out = []
    starts = [("L1", -1), ("L2", -1), ("L3", -1), ("L4", -1), ("R1", 1), ("R2", 1), ("R3", 1), ("R4", 1),
              ("TL", -1), ("TR", 1)]
    for name, d in starts:
        for k in range(2):
            x, y = P[name]
            y += (k * 14 - 7)
            path = [(x, y)]
            x += d * rng.randint(14, 30); path.append((x, y))
            dy = rng.choice([-1, 1]) * rng.randint(10, 26)
            x += d * abs(dy); y += dy; path.append((x, y))
            while math.hypot(x + d * 8 - CX, y - CY) < R - 16:
                x += d * 8
            path.append((x, y))
            out.append(path)
    # przewody szyi w dół
    for nx in (178, 192, 208, 222):
        out.append([(nx, 330), (nx, 350), (nx + (nx - 200) * 1.6, 350 + abs(nx - 200) * 1.6), (nx + (nx - 200) * 1.6, 380)])
    return out


def halo(v, pre):
    kind = v["halo"]; g = []
    a0, a1 = math.radians(-152), math.radians(-28)
    if kind == "rhythm":  # 2 takty szesnastek – siatka, której słucha instancja
        for i in range(32):
            a = a0 + (a1 - a0) * i / 31
            beat = i % 4 == 0
            r0 = 166 if beat else 174
            g.append(f'<line x1="{CX + r0 * math.cos(a):.1f}" y1="{CY + r0 * math.sin(a):.1f}" '
                     f'x2="{CX + 184 * math.cos(a):.1f}" y2="{CY + 184 * math.sin(a):.1f}" stroke="{v["acc"] if beat else "#6d7a93"}" '
                     f'stroke-width="{3 if beat else 1.6}" stroke-linecap="round" opacity="{1 if beat else 0.7}"/>')
    elif kind == "keys":  # oktawa+ klawiatury jako aureola
        n = 15
        for i in range(n):
            s = a0 + (a1 - a0) * i / n + 0.006; e = a0 + (a1 - a0) * (i + 1) / n - 0.006
            g.append(arcseg(s, e, 164, 186, "#d9dde8", 0.85))
        black = [0, 1, 3, 4, 5, 7, 8, 10, 11, 12]
        for i in black:
            m = a0 + (a1 - a0) * (i + 1) / n
            g.append(arcseg(m - 0.022, m + 0.022, 174, 187, "#0b0f16", 1))
        for i in (2, 5, 9):  # „wciśnięte” klawisze akordu
            s = a0 + (a1 - a0) * i / n + 0.006; e = a0 + (a1 - a0) * (i + 1) / n - 0.006
            g.append(arcseg(s, e, 164, 174, v["acc"], 1))
    elif kind == "grains":  # chmura ziaren: sample wokalu pocięte na drobiny, gęstniejące ku środkowi
        rng = random.Random(11)
        for i in range(90):
            t = rng.random()
            a = a0 + (a1 - a0) * t
            dens = math.sin(math.pi * t)
            r = 175 + rng.gauss(0, 4 + 5 * (1 - dens))
            size = 1.0 + 3.2 * dens * rng.random()
            col = rng.choice([v["acc"], v["acc"], v["acc2"], v["hi"]])
            g.append(f'<circle cx="{CX + r * math.cos(a):.1f}" cy="{CY + r * math.sin(a):.1f}" r="{size:.2f}" '
                     f'fill="{col}" opacity="{0.35 + 0.6 * dens * rng.random():.2f}"/>')
        for k in range(5):  # kilka ziaren z obwiednią (okno Hanna)
            t = 0.18 + 0.16 * k
            am = a0 + (a1 - a0) * t
            d = []
            for i in range(25):
                aa = am + (i - 12) * 0.006
                rr = 160 - 9 * math.sin(math.pi * i / 24) ** 2
                d.append(("M" if i == 0 else "L") + f"{CX + rr * math.cos(aa):.1f},{CY + rr * math.sin(aa):.1f}")
            g.append(f'<path d="{" ".join(d)}" fill="none" stroke="{v["acc"]}" stroke-width="1.8" stroke-linecap="round" opacity="0.9"/>')
    else:  # 4 struny basu, najniższa drga
        for j, r in enumerate((166, 172, 178, 184)):
            d = []
            for i in range(121):
                a = a0 + (a1 - a0) * i / 120
                amp = 3.2 * math.sin(math.pi * i / 120) * math.sin(i * 0.9) if j == 0 else 0
                rr = r + amp
                d.append(("M" if i == 0 else "L") + f"{CX + rr * math.cos(a):.1f},{CY + rr * math.sin(a):.1f}")
            g.append(f'<path d="{" ".join(d)}" fill="none" stroke="{v["acc"] if j == 0 else "#8b97ad"}" '
                     f'stroke-width="{2.4 - j * 0.35:.2f}" opacity="{1 if j == 0 else 0.75}" stroke-linecap="round"/>')
    return "\n".join(g)


def arcseg(s, e, r0, r1, col, op):
    p = lambda r, a: f"{CX + r * math.cos(a):.1f},{CY + r * math.sin(a):.1f}"
    return (f'<path d="M{p(r0, s)} A{r0},{r0} 0 0 1 {p(r0, e)} L{p(r1, e)} A{r1},{r1} 0 0 0 {p(r1, s)} Z" '
            f'fill="{col}" opacity="{op}"/>')


def emblem(vname, pre=""):
    v = VARIANTS[vname]; pre = pre or vname[0]
    rng = random.Random(7)
    s = [defs(v, pre)]
    # wafel krzemowy z płaskim ścięciem (notch) i siatką die
    s.append(f'<path d="M126,380 A190,190 0 1 1 274,380 Z" fill="url(#{pre}bg)"/>')
    s.append(f'<g clip-path="url(#{pre}wafer)" stroke="#2a3446" stroke-width="1">')
    for k in range(10, 400, 26):
        s.append(f'<line x1="{k}" y1="0" x2="{k}" y2="420"/><line x1="0" y1="{k + 4}" x2="400" y2="{k + 4}"/>')
    s.append("</g>")
    s.append(f'<circle cx="200" cy="190" r="150" fill="url(#{pre}aura)"/>')
    # ścieżki obwodów
    s.append(f'<g clip-path="url(#{pre}wafer)" fill="none" stroke="url(#{pre}iri)" stroke-width="1.6" opacity="0.55">')
    pads = []
    for t in traces(rng):
        s.append(f'<polyline points="{pts(t)}" stroke-linejoin="round"/>'); pads.append(t[-1])
    s.append("</g>")
    s.append(f'<g clip-path="url(#{pre}wafer)">')
    for x, y in pads:
        s.append(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="3" fill="#0b0f16" stroke="{v["acc"]}" stroke-width="1.4" opacity="0.9"/>')
    s.append("</g>")
    s.append(halo(v, pre))
    # szyja / ramiona
    s.append(f'<g clip-path="url(#{pre}wafer)">'
             f'<polygon points="172,320 228,320 238,372 162,372" fill="#1a2232" stroke="url(#{pre}iri)" stroke-width="1" stroke-opacity="0.6"/>'
             f'<polygon points="162,372 238,372 330,400 330,420 70,420 70,400" fill="#202a3c" stroke="url(#{pre}iri)" stroke-width="1" stroke-opacity="0.5"/>'
             f'<line x1="200" y1="372" x2="200" y2="420" stroke="{v["acc"]}" stroke-width="1.2" opacity="0.6"/></g>')
    # fasety głowy
    dark, light = "#162031", "#4a5c7c"
    for tri in TRIS.replace("\n", "").split("|"):
        ps = [P[n] for n in tri.split()]
        cx = sum(p[0] for p in ps) / 3; cy = sum(p[1] for p in ps) / 3
        t = 0.55 + (200 - cx) / 260 - (cy - 190) / 520 + rng.uniform(-0.12, 0.12)
        t = max(0.0, min(1.0, t))
        s.append(f'<polygon points="{pts(ps)}" fill="{mix(dark, light, t)}"/>')
    s.append(f'<polygon points="{pts([P[n] for n in "T TR R1 R2 R3 R4 CR C CL L4 L3 L2 L1 TL".split()])}" '
             f'fill="url(#{pre}iri)" opacity="0.16"/>')
    s.append(f'<g fill="none" stroke="url(#{pre}iri)" stroke-width="1" stroke-linejoin="round" opacity="0.75">')
    for tri in TRIS.replace("\n", "").split("|"):
        s.append(f'<polygon points="{pts([P[n] for n in tri.split()])}"/>')
    s.append("</g>")
    s.append(f'<g {HT}>')
    # chip na czole
    cx, cy, h = 200, 116, 13
    s.append(f'<polygon points="{cx},{cy - h} {cx + h},{cy} {cx},{cy + h} {cx - h},{cy}" fill="#0b0f16" stroke="{v["acc"]}" stroke-width="1.6"/>')
    s.append(f'<polygon points="{cx},{cy - 5} {cx + 5},{cy} {cx},{cy + 5} {cx - 5},{cy}" fill="{v["acc"]}"/>')
    for k in (-6, 0, 6):
        for sx, sy in ((1, -1), (1, 1), (-1, 1), (-1, -1)):
            bx = cx + sx * (h / 2) + sy * k * 0.5 * -sx; by = cy + sy * (h / 2) + k * 0.5
            s.append(f'<line x1="{bx:.1f}" y1="{by:.1f}" x2="{bx + sx * 5:.1f}" y2="{by + sy * 5:.1f}" stroke="{v["acc"]}" stroke-width="1" opacity="0.8"/>')
    # oczy – poświata + szczelina
    for sgn in (-1, 1):
        ex = 200 + sgn * 33
        s.append(f'<ellipse cx="{ex}" cy="176" rx="38" ry="20" fill="url(#{pre}glow)"/>')
        eye = [(200 + sgn * 14, 172), (200 + sgn * 52, 166), (200 + sgn * 50, 178), (200 + sgn * 16, 182)]
        s.append(f'<polygon points="{pts(eye)}" fill="{v["acc"]}"/>')
        s.append(f'<line x1="{200 + sgn * 20}" y1="175" x2="{200 + sgn * 46}" y2="171.5" stroke="{v["hi"]}" stroke-width="1.6" stroke-linecap="round"/>')
    # usta – fala dźwiękowa
    d = []
    for i in range(61):
        x = 168 + i * 64 / 60
        env = math.sin(math.pi * i / 60)
        y = 270 + env * 6 * math.sin(i * 0.75)
        d.append(("M" if i == 0 else "L") + f"{x:.1f},{y:.1f}")
    s.append(f'<ellipse cx="200" cy="270" rx="40" ry="12" fill="url(#{pre}glow)" opacity="0.6"/>')
    s.append(f'<path d="{" ".join(d)}" fill="none" stroke="{v["hi"]}" stroke-width="1.8" stroke-linecap="round"/>')
    s.append("</g>")
    # obrys wafla
    s.append(f'<path d="M126,380 A190,190 0 1 1 274,380 Z" fill="none" stroke="url(#{pre}iri)" stroke-width="2"/>')
    return "\n".join(s)


def write(name, body, vb):
    with open(os.path.join(OUT, name), "w") as f:
        f.write(f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="{vb}" width="{vb.split()[2]}" height="{vb.split()[3]}">\n{body}\n</svg>\n')


for k in VARIANTS:
    write(f"istota-{k}.svg", emblem(k), "0 0 400 420")

# baner nagłówka 1200x240 (bez tekstu – nazwę rysuje JUCE)
rng = random.Random(3)
v = VARIANTS["logo"]
b = [f"""<defs><linearGradient id="bnbg" x1="0" y1="0" x2="1" y2="0">
<stop offset="0" stop-color="#141b27"/><stop offset="1" stop-color="#0b0f16"/></linearGradient>
<linearGradient id="bnline" x1="0" y1="0" x2="1" y2="0"><stop offset="0" stop-color="{v['acc']}" stop-opacity="0.9"/>
<stop offset="0.6" stop-color="#ffbe73" stop-opacity="0.5"/><stop offset="1" stop-color="#55e6d6" stop-opacity="0.15"/></linearGradient></defs>
<rect width="1200" height="240" fill="url(#bnbg)"/>"""]
for k in range(0, 1200, 26):
    b.append(f'<line x1="{k}" y1="0" x2="{k}" y2="240" stroke="#1f2836" stroke-width="1"/>')
for k in range(8, 240, 26):
    b.append(f'<line x1="0" y1="{k}" x2="1200" y2="{k}" stroke="#1f2836" stroke-width="1"/>')
for i in range(9):
    y = 26 + i * 18 + rng.randint(-3, 3); x = 236
    pth = [(x, 120 + (y - 120) * 0.35), (x + 40, 120 + (y - 120) * 0.35)]
    x2 = x + 40 + abs(y - pth[0][1]); pth.append((x2, y))
    pth.append((rng.randint(700, 1180), y))
    b.append(f'<polyline points="{pts(pth)}" fill="none" stroke="url(#bnline)" stroke-width="1.4"/>')
    b.append(f'<circle cx="{pth[-1][0]}" cy="{y}" r="2.6" fill="#0b0f16" stroke="{v["acc"]}" stroke-width="1.2" opacity="0.7"/>')
# puls perkusji (kick/snare/hh) wzdłuż dołu
x0, base = 300, 214
for i in range(56):
    x = x0 + i * 15.5
    h = 26 if i % 8 == 0 else 18 if i % 8 == 4 else 7
    b.append(f'<line x1="{x:.1f}" y1="{base}" x2="{x:.1f}" y2="{base - h}" stroke="{v["acc"] if h > 20 else "#ffbe73" if h > 10 else "#55607a"}" stroke-width="2" stroke-linecap="round" opacity="{max(0.15, 1 - i / 60):.2f}"/>')
b.append(f'<g transform="translate(18,4) scale(0.555)">{emblem("logo", "bn")}</g>')
write("baner-naglowek.svg", "\n".join(b), "0 0 1200 240")
print("ok")
