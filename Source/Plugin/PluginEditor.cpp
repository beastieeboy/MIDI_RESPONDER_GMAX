#include "PluginEditor.h"
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

    constexpr int W = 900, H = 640;
}

SekcjaEditor::SekcjaEditor(SekcjaProcessor& p)
    : AudioProcessorEditor(&p), proc(p),
      lnf(SekcjaProcessor::isBass ? palette::bass : palette::piano),
      accent(SekcjaProcessor::isBass ? palette::bass : palette::piano)
{
    setLookAndFeel(&lnf);

    banner = loadSvg(BinaryData::banernaglowek_svg, BinaryData::banernaglowek_svgSize);
    being = SekcjaProcessor::isBass ? loadSvg(BinaryData::istotabass_svg, BinaryData::istotabass_svgSize)
                                    : loadSvg(BinaryData::istotapiano_svg, BinaryData::istotapiano_svgSize);

    harmonyGroup.setText(SekcjaProcessor::isBass ? "HARMONIA (Z INSTANCJI PIANO)" : U("PROGRESJA AKORDÓW"));
    feelGroup.setText(U("REAKCJA NA BĘBNIARZA"));
    drumGroup.setText(U("BĘBNY I WYJŚCIE"));
    for (auto* g : { &harmonyGroup, &feelGroup, &drumGroup })
        addAndMakeVisible(g);

    if (! SekcjaProcessor::isBass)
    {
        addCombo(keyBox, keyLabel, "key", "Tonacja", keyAtt);
        addCombo(progABox, progALabel, "progA", "Zwrotka", progAAtt);
        addCombo(progBBox, progBLabel, "progB", "Refren", progBAtt);
        addAndMakeVisible(fillToggle);
        addAndMakeVisible(giveWayToggle);
        fillAtt = std::make_unique<ButtonAttachment>(proc.apvts, "fillSwitch", fillToggle);
        giveWayAtt = std::make_unique<ButtonAttachment>(proc.apvts, "giveWay", giveWayToggle);
    }

    addKnob("reactivity", U("Reaktywność"));
    addKnob("variation", "Wariacje");
    addKnob("dynamics", "Dynamika");
    addKnob("feel", "Feel");
    addKnob("density", U("Gęstość"));
    if (SekcjaProcessor::isBass)
    {
        addKnob("kickLock", U("Trzymaj stopę"));
        addKnob("octave", "Oktawa");
    }
    else
    {
        addKnob("register", "Rejestr");
    }

    addNumber("kickNote", "Stopa", true);
    addNumber("snareNote", "Werbel", true);
    addNumber("tom1Note", "Tom 1", true);
    addNumber("tom2Note", "Tom 2", true);
    addNumber("hhNote", "Hi-hat", true);
    addNumber("midiCh", U("Kanał wyj."), false);
    addCombo(groupBox, groupLabel, "group", "Link", groupAtt);
    addCombo(mapBox, mapLabel, "drumMap", "Mapa", mapAtt);

    setSize(W, H);
    startTimerHz(30);
}

SekcjaEditor::~SekcjaEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

SekcjaEditor::Control& SekcjaEditor::addKnob(const String& paramId, const String& name)
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

SekcjaEditor::Control& SekcjaEditor::addNumber(const String& paramId, const String& name, bool noteName)
{
    auto* c = numbers.add(new Control());
    c->slider.setSliderStyle(Slider::LinearBar);
    c->slider.setColour(Slider::trackColourId, accent.withAlpha(0.22f));
    c->slider.setColour(Slider::backgroundColourId, palette::grid);
    c->slider.setTooltip(U("Przeciągnij albo kliknij dwukrotnie i wpisz numer nuty"));
    c->label.setText(name, dontSendNotification);
    c->label.setJustificationType(Justification::centredLeft);
    addAndMakeVisible(c->slider);
    addAndMakeVisible(c->label);
    c->attachment = std::make_unique<SliderAttachment>(proc.apvts, paramId, c->slider);
    if (noteName)
    {
        c->slider.textFromValueFunction = [](double v)
        { return String(roundToInt(v)) + " " + MidiMessage::getMidiNoteName(roundToInt(v), true, true, 4); };
        c->slider.valueFromTextFunction = [](const String& s) { return (double) s.getIntValue(); };
        c->slider.updateText();
    }
    return *c;
}

void SekcjaEditor::addCombo(ComboBox& box, Label& label, const String& paramId, const String& name,
                            std::unique_ptr<ComboAttachment>& att)
{
    if (auto* param = dynamic_cast<AudioParameterChoice*>(proc.apvts.getParameter(paramId)))
        box.addItemList(param->choices, 1);
    label.setText(name, dontSendNotification);
    addAndMakeVisible(box);
    addAndMakeVisible(label);
    att = std::make_unique<ComboAttachment>(proc.apvts, paramId, box);
}

void SekcjaEditor::resized()
{
    auto area = getLocalBounds();
    area.removeFromTop(184);
    footerArea = area.removeFromBottom(30).reduced(16, 4);
    area = area.reduced(16, 4);

    auto left = area.removeFromLeft(260);
    beingArea = left.removeFromTop(274);
    statusArea = left.withTrimmedTop(8);
    area.removeFromLeft(16);

    // Harmonia
    auto hg = area.removeFromTop(186);
    harmonyGroup.setBounds(hg);
    auto inner = hg.reduced(14).withTrimmedTop(14);
    if (! SekcjaProcessor::isBass)
    {
        auto row = inner.removeFromTop(26);
        keyLabel.setBounds(row.removeFromLeft(58));
        keyBox.setBounds(row.removeFromLeft(70));
        row.removeFromLeft(12);
        progALabel.setBounds(row.removeFromLeft(60));
        progABox.setBounds(row.removeFromLeft(150));
        row.removeFromLeft(12);
        progBLabel.setBounds(row.removeFromLeft(52));
        progBBox.setBounds(row);
        inner.removeFromTop(10);
        chordRowA = inner.removeFromTop(34);
        inner.removeFromTop(6);
        chordRowB = inner.removeFromTop(34);
        inner.removeFromTop(8);
        auto toggles = inner.removeFromTop(24);
        fillToggle.setBounds(toggles.removeFromLeft(toggles.getWidth() / 2 + 20));
        giveWayToggle.setBounds(toggles);
    }
    else
    {
        linkInfoArea = inner.removeFromTop(40);
        inner.removeFromTop(6);
        chordRowA = inner.removeFromTop(34);
        inner.removeFromTop(6);
        chordRowB = inner.removeFromTop(34);
    }

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
    drumGroup.setBounds(area);
    auto dr = area.reduced(12).withTrimmedTop(16);
    auto row1 = dr.removeFromTop(dr.getHeight() / 2);
    auto row2 = dr;
    const int cw = row1.getWidth() / 4;
    for (int i = 0; i < numbers.size(); ++i)
    {
        auto& row = i < 4 ? row1 : row2;
        auto cell = row.removeFromLeft(cw).reduced(2, 3);
        numbers[i]->label.setBounds(cell.removeFromLeft(66));
        numbers[i]->slider.setBounds(cell.withSizeKeepingCentre(cell.getWidth(), 24));
    }
    auto cell = row2.removeFromLeft(cw).reduced(2, 3);
    groupLabel.setBounds(cell.removeFromLeft(40));
    groupBox.setBounds(cell.withSizeKeepingCentre(cell.getWidth(), 24));
    cell = row2.reduced(2, 3);
    mapLabel.setBounds(cell.removeFromLeft(44));
    mapBox.setBounds(cell.withSizeKeepingCentre(cell.getWidth(), 24));
}

void SekcjaEditor::timerCallback()
{
    const uint32_t f = proc.drumFlash.exchange(0, std::memory_order_relaxed);
    for (int i = 0; i < 6; ++i)
        flash[i] = (f >> i & 1u) ? 1.0f : flash[i] * 0.82f;
    // Gotowa mapa perkusji zastępuje ręczne numery nut.
    const bool manualNotes = mapBox.getSelectedItemIndex() <= 0;
    for (int i = 0; i < numbers.size() - 1; ++i)
        numbers[i]->slider.setEnabled(manualNotes);
    shownEnergy += 0.15f * (proc.engine().status.energy.load() - shownEnergy);
    phase += 1.0 / 30.0;
    repaint();
}

void SekcjaEditor::paintBeing(Graphics& g)
{
    const auto b = beingArea.toFloat();
    const auto c = Point<float>(b.getCentreX(), b.getY() + b.getHeight() * 0.49f);
    const float pulse = 0.5f + 0.5f * float(std::sin(phase * 2.2));
    const float glow = 0.08f + 0.35f * shownEnergy + 0.25f * (flash[1] + flash[2]) * 0.5f;

    ColourGradient halo(accent.withAlpha(jlimit(0.0f, 0.7f, glow + 0.05f * pulse)), c.x, c.y,
                        accent.withAlpha(0.0f), c.x + b.getWidth() * 0.55f, c.y, true);
    g.setGradientFill(halo);
    g.fillEllipse(b.expanded(10.0f));

    if (being)
        being->drawWithin(g, b, RectanglePlacement::centred, 1.0f);

    // pierścień energii wokół istoty
    const float r = jmin(b.getWidth(), b.getHeight()) * 0.5f;
    Path ring;
    const float start = MathConstants<float>::pi * 0.75f;
    ring.addCentredArc(c.x, c.y, r, r, 0.0f, start, start + MathConstants<float>::pi * 1.5f * jlimit(0.0f, 1.0f, shownEnergy), true);
    g.setColour(accent.withAlpha(0.85f));
    g.strokePath(ring, PathStrokeType(3.0f, PathStrokeType::curved, PathStrokeType::rounded));
}

void SekcjaEditor::paintChords(Graphics& g, Rectangle<int> row, const String& title, int prog, int key, bool activeSection)
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

void SekcjaEditor::paintStatus(Graphics& g)
{
    const auto& st = proc.engine().status;
    auto a = statusArea;

    // lampki bębnów
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
    meter(a.removeFromTop(30), U("Gęstość hi-hatu"), st.hatDensity.load(), palette::violet);

    static const String modes[] = { U("Czekam na bębniarza…"), "Gram", U("Bębniarz gra przejście – zostawiam miejsce"), "Trzymam akord" };
    g.setColour(palette::text);
    g.setFont(Font(FontOptions(12.5f)));
    g.drawFittedText(modes[jlimit(0, 3, st.mode.load())], a.removeFromTop(34), Justification::centredLeft, 2);
}

void SekcjaEditor::paint(Graphics& g)
{
    g.fillAll(palette::background);
    g.setColour(palette::grid.withAlpha(0.35f));
    for (int x = 0; x < W; x += 24) g.drawVerticalLine(x, 184.0f, float(H));
    for (int y = 184; y < H; y += 24) g.drawHorizontalLine(y, 0.0f, float(W));

    const auto bannerArea = Rectangle<float>(0.0f, 0.0f, float(W), float(W) * 0.2f);
    if (banner)
        banner->drawWithin(g, bannerArea, RectanglePlacement::stretchToFit, 1.0f);

    // tytuł na banerze
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
    g.drawText(SekcjaProcessor::isBass ? U("BASS  ·  słucha bębnów i Piano") : U("PIANO  ·  lider harmonii, słucha bębnów"),
               title.withTrimmedLeft(16.0f).withTrimmedTop(36.0f), Justification::topLeft);

    paintBeing(g);
    paintStatus(g);

    const auto& st = proc.engine().status;
    const int sec = st.section.load();

    // stopka
    String foot;
    if (st.bar.load() >= 0 && st.mode.load() != 0)
        foot << "Takt " << (st.bar.load() + 1) << U("  ·  ") << (sec ? "Refren" : "Zwrotka") << U("  ·  ")
             << chordName(st.chordRoot.load(), st.chordQuality.load()) << U("  ·  lick #") << st.lick.load() << "   ";
    foot << (SekcjaProcessor::isBass ? "Piano: " : "Bass: ")
         << (st.partnerAlive.load() ? U("połączony") : "brak") << " (" << groupBox.getText() << ", "
         << (st.linkShared.load() ? U("pamięć współdzielona") : "tryb lokalny") << ")";
    g.setColour(palette::dim);
    g.setFont(Font(FontOptions(12.0f)));
    g.drawText(foot, footerArea, Justification::centredLeft);
    g.setColour(st.partnerAlive.load() ? accent : palette::dim);
    g.fillEllipse(Rectangle<float>(8.0f, 8.0f).withCentre({ float(footerArea.getRight()) - 4.0f, float(footerArea.getCentreY()) }));
}

void SekcjaEditor::paintOverChildren(Graphics& g)
{
    const auto& st = proc.engine().status;
    const auto h = sk::HarmonySettings::unpack(st.harmonyWord.load());
    if (SekcjaProcessor::isBass)
    {
        g.setFont(Font(FontOptions(13.0f)));
        g.setColour(st.partnerAlive.load() ? palette::text : palette::piano2);
        g.drawFittedText(st.partnerAlive.load()
                             ? U("Połączono z Piano. Tonacja ") + String(sk::noteNames[h.key])
                                   + ", progresje ustawiasz w oknie Piano."
                             : (h.valid ? String(U("Brak aktywnego Piano w tym zespole – gram ostatnią znaną progresję."))
                                        : String(U("Brak Piano w tym zespole – gram I-V-vi-IV w C. Dodaj Piano na tym samym linku."))),
                         linkInfoArea, Justification::centredLeft, 2);
    }
    const auto hh = h.valid ? h : proc.currentParams().harmony;
    const int sec = st.section.load();
    paintChords(g, chordRowA, "ZWROTKA", hh.progA, hh.key, sec == 0);
    paintChords(g, chordRowB, "REFREN", hh.progB, hh.key, sec == 1);

}
