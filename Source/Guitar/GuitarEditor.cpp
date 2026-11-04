#include "GuitarEditor.h"
#include "BinaryData.h"

using namespace juce;

namespace {
    std::unique_ptr<Drawable> loadSvg(const char* data, int size)
    {
        if (auto xml = XmlDocument::parse(String::fromUTF8(data, size)))
            return Drawable::createFromSVG(*xml);
        return {};
    }

    String chordName(int root, int quality)
    {
        static const char* suffix[] = { "", "m", "7", "maj7", "m7", "dim", "sus4" };
        return String(sk::noteNames[sk::mod12(root)]) + suffix[jlimit(0, 6, quality)];
    }

    // Nazwy nut: 0 = numer, 1 = jak w REAPER-ze (C4 = 60), 2 = jak w Kontakcie (C3 = 60).
    String noteText(int v, int style)
    {
        if (style == 0) return String(v);
        return String(v) + " " + MidiMessage::getMidiNoteName(v, true, true, style == 2 ? 3 : 4);
    }

    constexpr int W = 900, H = 760;
}

GuitarEditor::GuitarEditor(GuitarProcessor& p)
    : AudioProcessorEditor(&p), proc(p), lnf(palette::guitar), accent(palette::guitar)
{
    setLookAndFeel(&lnf);

    banner = loadSvg(BinaryData::banernaglowek_svg, BinaryData::banernaglowek_svgSize);
    being = loadSvg(BinaryData::istotagitara_svg, BinaryData::istotagitara_svgSize);

    melodyGroup.setText("MELODIA (HARMONIA Z PIANO, RYTM Z PIANO I BASU)");
    feelGroup.setText(U("REAKCJA NA ZESPÓŁ"));
    kontaktGroup.setText(U("KONTAKT, BĘBNY I WYJŚCIE"));
    for (auto* g : { &melodyGroup, &feelGroup, &kontaktGroup })
        addAndMakeVisible(g);

    addCombo(roleBox, roleLabel, "role", "Rola", roleAtt);

    addKnob("reactivity", U("Reaktywność"));
    addKnob("variation", "Wariacje");
    addKnob("dynamics", "Dynamika");
    addKnob("feel", "Feel");
    addKnob("density", U("Gęstość"));
    addKnob("ornaments", U("Ozdobniki"));
    addKnob("space", "Miejsce");
    addKnob("register", "Rejestr");
    knobs[5]->slider.setTooltip(U("Slajdy, bendy, hammer-ony / pull-offy, wibrato i zjazdy na końcu fraz"));
    knobs[6]->slider.setTooltip(U("Jak mocno gitara omija rytm Piano i Basu (gra w ich pauzach)"));
    knobs[7]->slider.textFromValueFunction = [](double v) { return noteText(roundToInt(v), 1); };
    knobs[7]->slider.updateText();

    addCombo(profileBox, profileLabel, "profile", "Instrument", profileAtt);
    addNumber(keyswitches, "bendRange", U("Bend ±"), 0);
    addNumber(keyswitches, "lowest", U("Najniższa"), 1);
    addNumber(keyswitches, "ksOpen", "Open", 2);
    addNumber(keyswitches, "ksMuted", "Mute", 2);
    addNumber(keyswitches, "ksSlideIn", "Slajd", 2);
    addNumber(keyswitches, "ksSlideDown", U("Zjazd ↓"), 2);
    addNumber(keyswitches, "ksSlideUp", U("Zjazd ↑"), 2);
    keyswitches[0]->slider.setTooltip(U("Ustaw tyle samo półtonów, ile ma pitch bend w instrumencie Kontakt"));
    keyswitches[4]->slider.setTooltip(U("Trzymany między dwiema nutami – druga wchodzi slajdem (Electric Neon: B1)"));

    addNumber(drumNotes, "kickNote", "Stopa", 1);
    addNumber(drumNotes, "snareNote", "Werbel", 1);
    addNumber(drumNotes, "tom1Note", "Tom 1", 1);
    addNumber(drumNotes, "tom2Note", "Tom 2", 1);
    addNumber(drumNotes, "hhNote", "Hi-hat", 1);
    addNumber(drumNotes, "midiCh", U("Kanał wyj."), 0);
    addCombo(groupBox, groupLabel, "group", "Link", groupAtt);
    addCombo(mapBox, mapLabel, "drumMap", "Mapa", mapAtt);

    setSize(W, H);
    startTimerHz(30);
}

GuitarEditor::~GuitarEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

GuitarEditor::Control& GuitarEditor::addKnob(const String& paramId, const String& name)
{
    auto* c = knobs.add(new Control());
    c->slider.setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
    c->slider.setTextBoxStyle(Slider::TextBoxBelow, false, 64, 18);
    c->label.setText(name, dontSendNotification);
    c->label.setJustificationType(Justification::centred);
    addAndMakeVisible(c->slider);
    addAndMakeVisible(c->label);
    c->attachment = std::make_unique<SliderAttachment>(proc.apvts, paramId, c->slider);
    return *c;
}

GuitarEditor::Control& GuitarEditor::addNumber(OwnedArray<Control>& dst, const String& paramId, const String& name, int noteStyle)
{
    auto* c = dst.add(new Control());
    c->slider.setSliderStyle(Slider::LinearBar);
    c->slider.setColour(Slider::trackColourId, accent.withAlpha(0.22f));
    c->slider.setColour(Slider::backgroundColourId, palette::grid);
    if (c->slider.getTooltip().isEmpty())
        c->slider.setTooltip(U("Przeciągnij albo kliknij dwukrotnie i wpisz numer nuty"));
    c->label.setText(name, dontSendNotification);
    c->label.setJustificationType(Justification::centredLeft);
    addAndMakeVisible(c->slider);
    addAndMakeVisible(c->label);
    c->attachment = std::make_unique<SliderAttachment>(proc.apvts, paramId, c->slider);
    if (noteStyle > 0)
    {
        c->slider.textFromValueFunction = [noteStyle](double v) { return noteText(roundToInt(v), noteStyle); };
        c->slider.valueFromTextFunction = [](const String& s) { return (double) s.getIntValue(); };
        c->slider.updateText();
    }
    return *c;
}

void GuitarEditor::addCombo(ComboBox& box, Label& label, const String& paramId, const String& name,
                            std::unique_ptr<ComboAttachment>& att)
{
    if (auto* param = dynamic_cast<AudioParameterChoice*>(proc.apvts.getParameter(paramId)))
        box.addItemList(param->choices, 1);
    label.setText(name, dontSendNotification);
    addAndMakeVisible(box);
    addAndMakeVisible(label);
    att = std::make_unique<ComboAttachment>(proc.apvts, paramId, box);
}

void GuitarEditor::resized()
{
    auto area = getLocalBounds();
    area.removeFromTop(184);
    footerArea = area.removeFromBottom(30).reduced(16, 4);
    area = area.reduced(16, 4);

    auto left = area.removeFromLeft(260);
    beingArea = left.removeFromTop(274);
    statusArea = left.withTrimmedTop(8);
    area.removeFromLeft(16);

    // Melodia
    auto mg = area.removeFromTop(232);
    melodyGroup.setBounds(mg);
    auto inner = mg.reduced(14).withTrimmedTop(14);
    auto row = inner.removeFromTop(26);
    roleLabel.setBounds(row.removeFromLeft(44));
    roleBox.setBounds(row.removeFromLeft(250));
    row.removeFromLeft(12);
    phraseArea = row;
    inner.removeFromTop(4);
    linkInfoArea = inner.removeFromTop(32);
    chordRowA = inner.removeFromTop(30);
    inner.removeFromTop(5);
    chordRowB = inner.removeFromTop(30);
    inner.removeFromTop(8);
    fretArea = inner;

    area.removeFromTop(10);
    auto fg = area.removeFromTop(124);
    feelGroup.setBounds(fg);
    auto kr = fg.reduced(10).withTrimmedTop(14);
    const int kw = kr.getWidth() / jmax(1, knobs.size());
    for (auto* k : knobs)
    {
        auto cell = kr.removeFromLeft(kw);
        k->label.setBounds(cell.removeFromBottom(16));
        k->slider.setBounds(cell);
    }

    area.removeFromTop(10);
    kontaktGroup.setBounds(area);
    auto kg = area.reduced(12).withTrimmedTop(14);
    const int rh = kg.getHeight() / 4;
    auto r1 = kg.removeFromTop(rh), r2 = kg.removeFromTop(rh), r3 = kg.removeFromTop(rh), r4 = kg;

    auto place = [](Control& c, Rectangle<int> cell, int labelW)
    {
        cell = cell.reduced(2, 3);
        c.label.setBounds(cell.removeFromLeft(labelW));
        c.slider.setBounds(cell.withSizeKeepingCentre(cell.getWidth(), 24));
    };

    profileLabel.setBounds(r1.removeFromLeft(76).reduced(2, 3));
    profileBox.setBounds(r1.removeFromLeft(270).withSizeKeepingCentre(266, 24));
    r1.removeFromLeft(8);
    place(*keyswitches[0], r1.removeFromLeft(r1.getWidth() / 2 - 10), 52);
    place(*keyswitches[1], r1, 70);

    const int kw5 = r2.getWidth() / 5;
    for (int i = 2; i < 7; ++i)
        place(*keyswitches[i], r2.removeFromLeft(kw5), 50);

    const int dw = r3.getWidth() / 5;
    for (int i = 0; i < 5; ++i)
        place(*drumNotes[i], r3.removeFromLeft(dw), 50);

    const int cw = r4.getWidth() / 3;
    place(*drumNotes[5], r4.removeFromLeft(cw), 70);
    auto cell = r4.removeFromLeft(cw).reduced(2, 3);
    groupLabel.setBounds(cell.removeFromLeft(40));
    groupBox.setBounds(cell.withSizeKeepingCentre(cell.getWidth(), 24));
    cell = r4.reduced(2, 3);
    mapLabel.setBounds(cell.removeFromLeft(44));
    mapBox.setBounds(cell.withSizeKeepingCentre(cell.getWidth(), 24));
}

void GuitarEditor::timerCallback()
{
    const uint32_t f = proc.drumFlash.exchange(0, std::memory_order_relaxed);
    for (int i = 0; i < 6; ++i)
        flash[i] = (f >> i & 1u) ? 1.0f : flash[i] * 0.82f;
    const bool manualNotes = mapBox.getSelectedItemIndex() <= 0;
    for (int i = 0; i < 5; ++i)
        drumNotes[i]->slider.setEnabled(manualNotes);
    const bool ks = profileBox.getSelectedItemIndex() <= 0;
    for (int i = 2; i < keyswitches.size(); ++i)
        keyswitches[i]->slider.setEnabled(ks);

    const int lp = proc.engine().status.lastPitch.load();
    if (lp != lastPitchShown)
    {
        lastPitchShown = lp;
        pitchGlow = 1.0f;
    }
    pitchGlow *= 0.9f;
    shownEnergy += 0.15f * (proc.engine().status.energy.load() - shownEnergy);
    phase += 1.0 / 30.0;
    repaint();
}

void GuitarEditor::paintBeing(Graphics& g)
{
    const auto b = beingArea.toFloat();
    const auto c = Point<float>(b.getCentreX(), b.getY() + b.getHeight() * 0.49f);
    const float pulse = 0.5f + 0.5f * float(std::sin(phase * 2.2));
    const float glow = 0.08f + 0.35f * shownEnergy + 0.25f * pitchGlow;

    ColourGradient halo(accent.withAlpha(jlimit(0.0f, 0.7f, glow + 0.05f * pulse)), c.x, c.y,
                        accent.withAlpha(0.0f), c.x + b.getWidth() * 0.55f, c.y, true);
    g.setGradientFill(halo);
    g.fillEllipse(b.expanded(10.0f));
    if (being)
        being->drawWithin(g, b, RectanglePlacement::centred, 1.0f);

    const float r = jmin(b.getWidth(), b.getHeight()) * 0.5f;
    Path ring;
    const float start = MathConstants<float>::pi * 0.75f;
    ring.addCentredArc(c.x, c.y, r, r, 0.0f, start, start + MathConstants<float>::pi * 1.5f * jlimit(0.0f, 1.0f, shownEnergy), true);
    g.setColour(accent.withAlpha(0.85f));
    g.strokePath(ring, PathStrokeType(3.0f, PathStrokeType::curved, PathStrokeType::rounded));
}

void GuitarEditor::paintChords(Graphics& g, Rectangle<int> row, const String& title, int prog, int key, bool activeSection)
{
    const auto& st = proc.engine().status;
    const bool playing = st.bar.load() >= 0 && st.mode.load() != 0;
    g.setFont(Font(FontOptions(11.0f, Font::bold)));
    g.setColour(activeSection ? accent : palette::dim);
    g.drawText(title, row.removeFromLeft(70), Justification::centredLeft);

    const auto& pr = sk::progressions[jlimit(0, sk::numProgressions - 1, prog)];
    const int n = pr.length;
    const float gap = 6.0f;
    const float cw = (float(row.getWidth()) - gap * float(n - 1)) / float(n);
    for (int i = 0; i < n; ++i)
    {
        const auto chip = Rectangle<float>(float(row.getX()) + float(i) * (cw + gap), float(row.getY()), cw, float(row.getHeight()));
        const auto ch = sk::chordFor(key, prog, i);
        const bool now = activeSection && playing && st.chordPos.load() == i;
        g.setColour(now ? accent.withAlpha(0.3f) : palette::grid);
        g.fillRoundedRectangle(chip, 5.0f);
        g.setColour(now ? accent : palette::dim.withAlpha(0.6f));
        g.drawRoundedRectangle(chip.reduced(0.5f), 5.0f, now ? 1.5f : 1.0f);
        g.setColour(now ? Colours::white : palette::text);
        g.setFont(Font(FontOptions(n > 4 ? 13.0f : 15.0f, Font::bold)));
        g.drawText(chordName(ch.root, ch.quality), chip, Justification::centred);
    }
}

void GuitarEditor::paintFretboard(Graphics& g, Rectangle<int> area)
{
    // Gryf: 6 strun (E A D G H E), 15 progów, kropka = ostatnia zagrana nuta.
    const auto& st = proc.engine().status;
    auto info = area.removeFromRight(170);
    auto b = area.toFloat().reduced(2.0f, 4.0f);
    g.setColour(palette::grid);
    g.fillRoundedRectangle(b, 4.0f);
    constexpr int frets = 15;
    static const int open[6] = { 64, 59, 55, 50, 45, 40 }; // od góry: wysoka E
    const float fw = b.getWidth() / float(frets + 1);
    const float sh = b.getHeight() / 6.0f;
    for (int f = 1; f <= frets; ++f)
    {
        const float x = b.getX() + fw * float(f);
        g.setColour(palette::dim.withAlpha(f == 12 ? 0.9f : 0.45f));
        g.drawVerticalLine(int(x), b.getY(), b.getBottom());
    }
    for (int f : { 3, 5, 7, 9, 15 })
    {
        g.setColour(palette::dim.withAlpha(0.35f));
        g.fillEllipse(Rectangle<float>(4.0f, 4.0f).withCentre({ b.getX() + fw * (float(f) + 0.5f), b.getCentreY() }));
    }
    for (int s = 0; s < 6; ++s)
    {
        const float y = b.getY() + sh * (float(s) + 0.5f);
        g.setColour(palette::text.withAlpha(0.25f + 0.07f * float(s)));
        g.drawLine(b.getX(), y, b.getRight(), y, 0.8f + 0.25f * float(s));
    }

    // Brzmienie gitary jest oktawę niżej od zapisu (konwencja bibliotek Kontakt), więc
    // na gryfie pokazujemy nutę -12 – tak, jak zagrałby ją gitarzysta.
    const int pitch = st.lastPitch.load();
    if (pitch >= 0)
    {
        const int sounding = pitch - 12;
        int bestS = -1, bestF = 99;
        for (int s = 0; s < 6; ++s)
        {
            const int f = sounding - open[s];
            if (f >= 0 && f <= frets && std::abs(f - 7) < std::abs(bestF - 7)) { bestS = s; bestF = f; }
        }
        if (bestS >= 0)
        {
            const auto dot = Point<float>(b.getX() + fw * (float(bestF) + 0.5f) - (bestF == 0 ? fw * 0.3f : 0.0f),
                                          b.getY() + sh * (float(bestS) + 0.5f));
            g.setColour(accent.withAlpha(0.25f + 0.5f * pitchGlow));
            g.fillEllipse(Rectangle<float>(18.0f, 18.0f).withCentre(dot));
            g.setColour(accent);
            g.fillEllipse(Rectangle<float>(9.0f, 9.0f).withCentre(dot));
        }
    }

    g.setFont(Font(FontOptions(11.5f)));
    g.setColour(palette::text);
    String s;
    s << U("slajdy ") << st.slides.load() << U("  ·  bendy ") << st.bends.load() << "\n"
      << U("legato ") << st.legatos.load() << U("  ·  zjazdy ") << st.falls.load();
    g.drawFittedText(s, info.reduced(10, 4), Justification::centredLeft, 3);
}

void GuitarEditor::paintStatus(Graphics& g)
{
    const auto& st = proc.engine().status;
    auto a = statusArea;

    static const char* names[] = { "STOPA", "WERBEL", "TOMY", "HI-HAT", "TALERZ" };
    auto lights = a.removeFromTop(40);
    const int lw = lights.getWidth() / 5;
    for (int i = 0; i < 5; ++i)
    {
        auto cell = lights.removeFromLeft(lw).toFloat();
        const float v = flash[i + 1];
        const auto dot = Rectangle<float>(12.0f, 12.0f).withCentre({ cell.getCentreX(), cell.getY() + 10.0f });
        g.setColour(palette::grid);
        g.fillEllipse(dot);
        g.setColour(palette::violet.withAlpha(v));
        g.fillEllipse(dot);
        g.setColour(palette::dim);
        g.setFont(Font(FontOptions(9.5f, Font::bold)));
        g.drawText(names[i], cell.withTrimmedTop(20.0f), Justification::centredTop);
    }

    auto meter = [&](Rectangle<int> r, const String& label, float v, Colour col)
    {
        g.setColour(palette::text);
        g.setFont(Font(FontOptions(11.5f)));
        g.drawText(label, r.removeFromTop(16), Justification::centredLeft);
        auto bar = r.removeFromTop(8).toFloat();
        g.setColour(palette::grid);
        g.fillRoundedRectangle(bar, 4.0f);
        g.setColour(col);
        g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * jlimit(0.0f, 1.0f, v)), 4.0f);
    };
    a.removeFromTop(4);
    meter(a.removeFromTop(30), U("Energia bębniarza"), shownEnergy, accent);

    static const String modes[] = { U("Czekam na bębniarza…"), U("Gram melodię zwrotki"), U("Gram melodię refrenu"),
                                    U("Wchodzę z początkiem następnej frazy"), U("Bębniarz gra przejście – czekam") };
    g.setColour(palette::text);
    g.setFont(Font(FontOptions(12.5f)));
    g.drawFittedText(modes[jlimit(0, 4, st.mode.load())], a.removeFromTop(40), Justification::centredLeft, 2);

    auto lamp = [&](Rectangle<int> r, const String& who, bool on)
    {
        g.setColour(on ? accent : palette::dim);
        g.fillEllipse(Rectangle<float>(8.0f, 8.0f).withCentre({ float(r.getX()) + 5.0f, float(r.getCentreY()) }));
        g.setColour(on ? palette::text : palette::dim);
        g.setFont(Font(FontOptions(12.0f)));
        g.drawText(who + (on ? U(": słucham") : U(": brak")), r.withTrimmedLeft(16), Justification::centredLeft);
    };
    lamp(a.removeFromTop(20), "Piano", st.pianoAlive.load());
    lamp(a.removeFromTop(20), "Bass", st.bassAlive.load());
}

void GuitarEditor::paint(Graphics& g)
{
    g.fillAll(palette::background);
    g.setColour(palette::grid.withAlpha(0.35f));
    for (int x = 0; x < W; x += 24) g.drawVerticalLine(x, 184.0f, float(H));
    for (int y = 184; y < H; y += 24) g.drawHorizontalLine(y, 0.0f, float(W));

    const auto bannerArea = Rectangle<float>(0.0f, 0.0f, float(W), float(W) * 0.2f);
    if (banner)
        banner->drawWithin(g, bannerArea, RectanglePlacement::stretchToFit, 1.0f);

    auto title = Rectangle<float>(300.0f, 112.0f, 580.0f, 60.0f);
    g.setColour(palette::background.withAlpha(0.78f));
    g.fillRoundedRectangle(title, 8.0f);
    g.setColour(accent);
    g.fillRect(title.removeFromLeft(4.0f));
    g.setColour(palette::text);
    g.setFont(Font(FontOptions(26.0f, Font::bold)));
    g.drawText("SEKCJA KRZEMOWA", title.withTrimmedLeft(16.0f).removeFromTop(34.0f), Justification::bottomLeft);
    g.setColour(accent);
    g.setFont(Font(FontOptions(13.0f, Font::bold)));
    g.drawText(U("GITARA  ·  melodie i zagrywki, słucha bębnów, Piano i Bass"),
               title.withTrimmedLeft(16.0f).withTrimmedTop(36.0f), Justification::topLeft);

    paintBeing(g);
    paintStatus(g);

    const auto& st = proc.engine().status;
    String foot;
    if (st.bar.load() >= 0 && st.mode.load() != 0)
        foot << "Takt " << (st.bar.load() + 1) << U("  ·  ") << (st.section.load() ? "Refren" : "Zwrotka") << U("  ·  ")
             << chordName(st.chordRoot.load(), st.chordQuality.load()) << "   ";
    foot << "Link: " << groupBox.getText() << " (" << (st.linkShared.load() ? U("pamięć współdzielona") : "tryb lokalny")
         << U(")  ·  wyjście MIDI → Kontakt");
    g.setColour(palette::dim);
    g.setFont(Font(FontOptions(12.0f)));
    g.drawText(foot, footerArea, Justification::centredLeft);
}

void GuitarEditor::paintOverChildren(Graphics& g)
{
    const auto& st = proc.engine().status;
    const auto h = sk::HarmonySettings::unpack(st.harmonyWord.load());

    g.setFont(Font(FontOptions(12.5f)));
    g.setColour(st.pianoAlive.load() ? palette::text : palette::piano2);
    g.drawFittedText(st.pianoAlive.load()
                         ? U("Połączono z Piano. Tonacja ") + String(sk::noteNames[h.key])
                               + U(", progresje ustawiasz w oknie Piano. Melodie tylko z dźwięków bez zgrzytów.")
                         : String(U("Brak Piano w tym zespole – gram ostatnią znaną progresję. Dodaj Piano na tym samym linku.")),
                     linkInfoArea, Justification::centredLeft, 2);

    const auto hh = h.valid ? h : sk::HarmonySettings {};
    const int sec = st.section.load();
    paintChords(g, chordRowA, "ZWROTKA", hh.progA, hh.key, sec == 0);
    paintChords(g, chordRowB, "REFREN", hh.progB, hh.key, sec == 1);

    static const String sources[] = { U("melodia"), U("kontrmelodia") };
    String ph;
    if (st.phrase.load() >= 0)
        ph << sources[jlimit(0, 1, st.phraseSource.load())] << U(" POP909 nr ") << st.phraseSong.load()
           << U(" · takt ") << (st.phraseBar.load() + 1) << "/8 · pasuje " << roundToInt(st.phraseFit.load() * 100.0f) << " %";
    else
        ph << U("Melodia: —");
    g.setColour(palette::dim);
    g.setFont(Font(FontOptions(12.0f)));
    g.drawText(ph, phraseArea, Justification::centredRight);

    paintFretboard(g, fretArea);
}
