#include "VocalEditor.h"
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

    String noteName(float midi)
    {
        return MidiMessage::getMidiNoteName(roundToInt(midi), true, true, 4);
    }

    constexpr int W = 900, H = 860, top = 150; // baner niższy niż w Piano/Bass, żeby zmieścić piec
}

// Jeden sampel w piecu: żar (ile ziaren z niego leci), nazwa, wysokość, udział w mieszance.
class VocalEditor::Row : public Component
{
public:
    Row(VocalEditor& e) : owner(e)
    {
        weight.setSliderStyle(Slider::LinearBar);
        weight.setRange(0.0, 1.0, 0.01);
        weight.setColour(Slider::trackColourId, e.accent.withAlpha(0.28f));
        weight.setColour(Slider::backgroundColourId, palette::grid);
        weight.setTooltip(U("Udział sampla w mieszance"));
        weight.textFromValueFunction = [](double v) { return String(roundToInt(v * 100.0)) + " %"; };
        weight.onValueChange = [this] { if (sample) sample->weight = float(weight.getValue()); };
        addAndMakeVisible(weight);

        remove.setButtonText(U("✕"));
        remove.setTooltip(U("Wyjmij z pieca"));
        remove.onClick = [this] { owner.proc.removeSample(index); };
        addAndMakeVisible(remove);
    }

    void set(int row, std::shared_ptr<sk::VocalSample> s)
    {
        index = row;
        if (s != sample)
        {
            sample = std::move(s);
            lastCount = sample ? sample->grainsSpawned.load() : 0;
            heat = 0.0f;
        }
        if (sample)
            weight.setValue(sample->weight.load(), dontSendNotification);
        repaint();
    }

    void tick()
    {
        if (! sample) return;
        const uint32_t c = sample->grainsSpawned.load(std::memory_order_relaxed);
        const float burst = jmin(1.0f, float(c - lastCount) / 4.0f);
        lastCount = c;
        const float h = jmax(heat * 0.86f, burst);
        if (std::abs(h - heat) > 0.01f) { heat = h; repaint(); }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced(2, 3);
        remove.setBounds(r.removeFromRight(24));
        r.removeFromRight(6);
        weight.setBounds(r.removeFromRight(110));
    }

    void paint(Graphics& g) override
    {
        if (! sample) return;
        auto r = getLocalBounds().reduced(4, 0);
        const auto dot = Rectangle<float>(10.0f, 10.0f).withCentre({ float(r.getX()) + 7.0f, float(r.getCentreY()) });
        g.setColour(palette::grid.brighter(0.2f));
        g.fillEllipse(dot);
        g.setColour(owner.accent.interpolatedWith(Colour(0xffffd27a), heat).withAlpha(0.25f + 0.75f * heat));
        g.fillEllipse(dot.expanded(heat * 2.0f));
        r.removeFromLeft(20);
        r.removeFromRight(150);

        const float secs = float(sample->length() / sample->sampleRate);
        String info;
        if (sample->voicedFraction > 0.05f)
            info << noteName(sample->medianMidi) << U(" · ") << roundToInt(sample->voicedFraction * 100.0f) << U(" % śpiewu");
        else
            info << U("bez wysokości (szept / perkusja)");
        info << U(" · ") << String(secs, 1) << " s";

        g.setColour(palette::text);
        g.setFont(Font(FontOptions(13.0f, Font::bold)));
        g.drawText(String::fromUTF8(sample->name.c_str()), r.removeFromLeft(r.getWidth() / 2), Justification::centredLeft, true);
        g.setColour(palette::dim.brighter(0.4f));
        g.setFont(Font(FontOptions(11.5f)));
        g.drawText(info, r, Justification::centredRight, true);
    }

private:
    VocalEditor& owner;
    Slider weight;
    TextButton remove;
    std::shared_ptr<sk::VocalSample> sample;
    int index = 0;
    uint32_t lastCount = 0;
    float heat = 0.0f;
};

VocalEditor::VocalEditor(VocalProcessor& p)
    : AudioProcessorEditor(&p), proc(p), lnf(palette::vocal), accent(palette::vocal)
{
    setLookAndFeel(&lnf);
    lnf.setColour(TextButton::buttonColourId, palette::grid);
    lnf.setColour(TextButton::textColourOffId, palette::text);
    lnf.setColour(TextButton::textColourOnId, Colours::white);
    lnf.setColour(ListBox::backgroundColourId, palette::background.withAlpha(0.6f));
    lnf.setColour(ListBox::outlineColourId, palette::grid);
    lnf.setColour(ScrollBar::thumbColourId, accent.withAlpha(0.5f));

    banner = loadSvg(BinaryData::banernaglowek_svg, BinaryData::banernaglowek_svgSize);
    being = loadSvg(BinaryData::istotawokal_svg, BinaryData::istotawokal_svgSize);

    furnaceGroup.setText(U("PIEC · SAMPLE WOKALU (SUROWIEC)"));
    grainGroup.setText("ZIARNA");
    toneGroup.setText(U("STRÓJ I MIESZANIE"));
    reactGroup.setText(U("REAKCJA NA BĘBNIARZA"));
    harmonyGroup.setText(U("HARMONIA (Z PIANO) I BĘBNY"));
    for (auto* g : { &furnaceGroup, &grainGroup, &toneGroup, &reactGroup, &harmonyGroup })
        addAndMakeVisible(g);

    for (auto* b : { &addButton, &folderButton, &clearButton })
        addAndMakeVisible(b);
    addButton.onClick = [this] { chooseFiles(); };
    folderButton.onClick = [this] { chooseFolder(); };
    clearButton.onClick = [this] { proc.clearPool(); };
    folderButton.setTooltip(U("Folder obserwowany na bieżąco: każdy nowy plik audio, który tam zapiszesz, wpada do pieca"));

    furnaceList.setModel(this);
    furnaceList.setRowHeight(28);
    furnaceList.setOutlineThickness(1);
    addAndMakeVisible(furnaceList);

    addKnob(grainKnobs, "grainMs", "Ziarno");
    addKnob(grainKnobs, "density", U("Gęstość"));
    addKnob(grainKnobs, "position", "Pozycja");
    addKnob(grainKnobs, "scatter", "Rozrzut");
    addKnob(grainKnobs, "scan", "Skan");
    addKnob(grainKnobs, "reverse", "Wstecz");
    addKnob(grainKnobs, "shape", U("Kształt"));
    addKnob(grainKnobs, "width", U("Szerokość"));

    addKnob(toneKnobs, "retune", "Strojenie");
    addKnob(toneKnobs, "octave", "Oktawa");
    addKnob(toneKnobs, "spread", U("Rozpiętość"));
    addKnob(toneKnobs, "voiced", U("Samogłoski"));
    addKnob(toneKnobs, "freshness", U("Świeżość"));
    addKnob(toneKnobs, "gain", U("Głośność"));

    addKnob(reactKnobs, "hatDensity", U("HH → gęstość"));
    addKnob(reactKnobs, "swell", U("Tomy → puchn."));
    addKnob(reactKnobs, "pulse", "Puls");
    addKnob(reactKnobs, "dynamics", "Dynamika");
    addKnob(reactKnobs, "reactivity", U("Reaktywność"));
    addAndMakeVisible(playAlwaysToggle);
    playAlwaysAtt = std::make_unique<ButtonAttachment>(proc.apvts, "playAlways", playAlwaysToggle);

    addCombo(keyBox, keyLabel, "key", "Tonacja", keyAtt);
    addCombo(progABox, progALabel, "progA", "Zwrotka", progAAtt);
    addCombo(progBBox, progBLabel, "progB", "Refren", progBAtt);
    addAndMakeVisible(fillToggle);
    fillAtt = std::make_unique<ButtonAttachment>(proc.apvts, "fillSwitch", fillToggle);
    addCombo(groupBox, groupLabel, "group", "Link", groupAtt);
    addCombo(mapBox, mapLabel, "drumMap", "Mapa", mapAtt);
    addNumber("kickNote", "Stopa");
    addNumber("snareNote", "Werbel");
    addNumber("tom1Note", "Tom 1");
    addNumber("tom2Note", "Tom 2");
    addNumber("hhNote", "Hi-hat");

    setSize(W, H);
    startTimerHz(30);
}

VocalEditor::~VocalEditor()
{
    stopTimer();
    furnaceList.setModel(nullptr);
    setLookAndFeel(nullptr);
}

VocalEditor::Control& VocalEditor::addKnob(OwnedArray<Control>& into, const String& paramId, const String& name)
{
    auto* c = into.add(new Control());
    c->slider.setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
    c->slider.setTextBoxStyle(Slider::TextBoxBelow, false, 62, 16);
    c->label.setText(name, dontSendNotification);
    c->label.setJustificationType(Justification::centred);
    c->label.setFont(Font(FontOptions(11.5f)));
    addAndMakeVisible(c->slider);
    addAndMakeVisible(c->label);
    c->attachment = std::make_unique<SliderAttachment>(proc.apvts, paramId, c->slider);
    return *c;
}

VocalEditor::Control& VocalEditor::addNumber(const String& paramId, const String& name)
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
    c->slider.textFromValueFunction = [](double v)
    { return String(roundToInt(v)) + " " + MidiMessage::getMidiNoteName(roundToInt(v), true, true, 4); };
    c->slider.valueFromTextFunction = [](const String& s) { return (double) s.getIntValue(); };
    c->slider.updateText();
    return *c;
}

void VocalEditor::addCombo(ComboBox& box, Label& label, const String& paramId, const String& name,
                           std::unique_ptr<ComboAttachment>& att)
{
    if (auto* param = dynamic_cast<AudioParameterChoice*>(proc.apvts.getParameter(paramId)))
        box.addItemList(param->choices, 1);
    label.setText(name, dontSendNotification);
    addAndMakeVisible(box);
    addAndMakeVisible(label);
    att = std::make_unique<ComboAttachment>(proc.apvts, paramId, box);
}

// ---------------------------------------------------------------- piec

int VocalEditor::getNumRows()
{
    return proc.samplePool().size();
}

void VocalEditor::paintListBoxItem(int row, Graphics& g, int w, int h, bool)
{
    g.setColour(row % 2 ? palette::panel : palette::grid.withAlpha(0.45f));
    g.fillRect(0, 0, w, h);
}

Component* VocalEditor::refreshComponentForRow(int row, bool, Component* existing)
{
    auto* r = dynamic_cast<Row*>(existing);
    if (row >= proc.samplePool().size())
    {
        delete existing;
        return nullptr;
    }
    if (r == nullptr)
    {
        delete existing;
        r = new Row(*this);
    }
    r->set(row, proc.samplePool().at(row));
    return r;
}

bool VocalEditor::isInterestedInFileDrag(const StringArray& files)
{
    for (const auto& f : files)
        if (File(f).isDirectory() || VocalProcessor::isAudioFile(File(f)))
            return true;
    return false;
}

void VocalEditor::filesDropped(const StringArray& files, int, int)
{
    dragging = false;
    proc.addFiles(files);
    repaint();
}

void VocalEditor::chooseFiles()
{
    chooser = std::make_unique<FileChooser>(U("Wsyp sample wokalu do pieca"), File(), VocalProcessor::audioWildcard());
    chooser->launchAsync(FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles
                             | FileBrowserComponent::canSelectMultipleItems,
                         [this](const FileChooser& fc)
                         {
                             StringArray paths;
                             for (const auto& f : fc.getResults()) paths.add(f.getFullPathName());
                             proc.addFiles(paths);
                         });
}

void VocalEditor::chooseFolder()
{
    chooser = std::make_unique<FileChooser>(U("Folder pieca: nowe pliki audio będą wpadać same"), proc.getFurnaceFolder());
    chooser->launchAsync(FileBrowserComponent::openMode | FileBrowserComponent::canSelectDirectories,
                         [this](const FileChooser& fc)
                         {
                             const auto f = fc.getResult();
                             if (f.isDirectory())
                                 proc.setFurnaceFolder(f);
                         });
}

// ---------------------------------------------------------------- układ

void VocalEditor::layoutKnobs(OwnedArray<Control>& knobs, Rectangle<int> area)
{
    const int kw = area.getWidth() / jmax(1, knobs.size());
    for (auto* k : knobs)
    {
        auto cell = area.removeFromLeft(kw);
        k->label.setBounds(cell.removeFromBottom(15));
        k->slider.setBounds(cell);
    }
}

void VocalEditor::resized()
{
    auto area = getLocalBounds();
    area.removeFromTop(top);
    footerArea = area.removeFromBottom(30).reduced(16, 4);
    area = area.reduced(16, 4);

    auto left = area.removeFromLeft(250);
    beingArea = left.removeFromTop(250);
    statusArea = left.withTrimmedTop(8);
    area.removeFromLeft(16);

    // piec
    auto fg = area.removeFromTop(190);
    furnaceGroup.setBounds(fg);
    auto inner = fg.reduced(10).withTrimmedTop(14);
    auto buttons = inner.removeFromTop(26);
    addButton.setBounds(buttons.removeFromLeft(118));
    buttons.removeFromLeft(6);
    folderButton.setBounds(buttons.removeFromLeft(118));
    buttons.removeFromLeft(6);
    clearButton.setBounds(buttons.removeFromRight(80));
    folderInfoArea = buttons.reduced(8, 0);
    inner.removeFromTop(6);
    furnaceList.setBounds(inner);

    area.removeFromTop(8);
    auto gg = area.removeFromTop(98);
    grainGroup.setBounds(gg);
    layoutKnobs(grainKnobs, gg.reduced(8).withTrimmedTop(12));

    area.removeFromTop(8);
    auto tg = area.removeFromTop(98);
    toneGroup.setBounds(tg);
    layoutKnobs(toneKnobs, tg.reduced(8).withTrimmedTop(12));

    area.removeFromTop(8);
    auto rg = area.removeFromTop(98);
    reactGroup.setBounds(rg);
    auto rk = rg.reduced(8).withTrimmedTop(12);
    playAlwaysToggle.setBounds(rk.removeFromRight(150).withSizeKeepingCentre(150, 40));
    layoutKnobs(reactKnobs, rk);

    area.removeFromTop(8);
    harmonyGroup.setBounds(area);
    auto hg = area.reduced(10, 6).withTrimmedTop(14);
    linkInfoArea = hg.removeFromTop(18);
    hg.removeFromTop(4);
    chordRow = hg.removeFromTop(24);
    hg.removeFromTop(4);
    auto row = hg.removeFromTop(24);
    keyLabel.setBounds(row.removeFromLeft(52));
    keyBox.setBounds(row.removeFromLeft(62));
    row.removeFromLeft(8);
    progALabel.setBounds(row.removeFromLeft(56));
    progABox.setBounds(row.removeFromLeft(150));
    row.removeFromLeft(8);
    progBLabel.setBounds(row.removeFromLeft(50));
    progBBox.setBounds(row);
    hg.removeFromTop(4);
    row = hg.removeFromTop(24);
    fillToggle.setBounds(row.removeFromLeft(196));
    groupLabel.setBounds(row.removeFromLeft(34));
    groupBox.setBounds(row.removeFromLeft(110));
    row.removeFromLeft(10);
    mapLabel.setBounds(row.removeFromLeft(40));
    mapBox.setBounds(row);
    hg.removeFromTop(4);
    row = hg.removeFromTop(24);
    const int cw = row.getWidth() / jmax(1, numbers.size());
    for (auto* n : numbers)
    {
        auto cell = row.removeFromLeft(cw).reduced(2, 0);
        n->label.setBounds(cell.removeFromLeft(44));
        n->slider.setBounds(cell);
    }
}

void VocalEditor::timerCallback()
{
    const uint32_t f = proc.drumFlash.exchange(0, std::memory_order_relaxed);
    for (int i = 0; i < 6; ++i)
        flash[i] = (f >> i & 1u) ? 1.0f : flash[i] * 0.82f;
    const bool manualNotes = mapBox.getSelectedItemIndex() <= 0;
    for (auto* n : numbers)
        n->slider.setEnabled(manualNotes);

    const auto& st = proc.engine().status;
    const bool followPiano = st.followingPiano.load();
    for (auto* c : { &keyBox, &progABox, &progBBox })
        c->setEnabled(! followPiano);
    fillToggle.setEnabled(! followPiano);

    if (shownVersion != proc.poolVersion())
    {
        shownVersion = proc.poolVersion();
        furnaceList.updateContent();
        furnaceList.repaint();
    }
    for (int i = 0; i < proc.samplePool().size(); ++i)
        if (auto* r = dynamic_cast<Row*>(furnaceList.getComponentForRowNumber(i)))
            r->tick();

    shownEnergy += 0.15f * (st.energy.load() - shownEnergy);
    shownSwell += 0.25f * (st.swell.load() - shownSwell);
    shownGrains += 0.2f * (float(st.activeGrains.load()) - shownGrains);
    phase += 1.0 / 30.0;
    repaint(0, top, 250 + 32, H - top);
    repaint(folderInfoArea);
    repaint(linkInfoArea.getUnion(chordRow));
    repaint(footerArea);
}

// ---------------------------------------------------------------- rysowanie

void VocalEditor::paintBeing(Graphics& g)
{
    const auto b = beingArea.toFloat().reduced(6.0f);
    const auto c = Point<float>(b.getCentreX(), b.getY() + b.getHeight() * 0.49f);
    const float pulse = 0.5f + 0.5f * float(std::sin(phase * 2.2));
    const float glow = 0.08f + 0.3f * shownEnergy + 0.35f * shownSwell + 0.2f * flash[4];

    ColourGradient halo(accent.withAlpha(jlimit(0.0f, 0.75f, glow + 0.05f * pulse)), c.x, c.y,
                        accent.withAlpha(0.0f), c.x + b.getWidth() * 0.55f, c.y, true);
    g.setGradientFill(halo);
    g.fillEllipse(b.expanded(8.0f));
    if (being)
        being->drawWithin(g, b, RectanglePlacement::centred, 1.0f);

    // ziarna krążące wokół istoty – tyle, ile gra naprawdę
    const float r = jmin(b.getWidth(), b.getHeight()) * 0.5f;
    Random rnd(17);
    const int n = jmin(64, roundToInt(shownGrains));
    for (int i = 0; i < n; ++i)
    {
        const float a = rnd.nextFloat() * MathConstants<float>::twoPi + float(phase) * (0.3f + 0.4f * rnd.nextFloat());
        const float rr = r * (0.96f + 0.12f * rnd.nextFloat()) + 6.0f * shownSwell * rnd.nextFloat();
        const float s = 2.0f + 2.5f * rnd.nextFloat();
        g.setColour((i % 3 ? accent : palette::vocal2).withAlpha(0.45f + 0.5f * rnd.nextFloat()));
        g.fillEllipse(Rectangle<float>(s, s).withCentre({ c.x + rr * std::cos(a), c.y + rr * std::sin(a) }));
    }
}

void VocalEditor::paintStatus(Graphics& g)
{
    const auto& st = proc.engine().status;
    auto a = statusArea;

    static const char* names[] = { "STOPA", "WERBEL", "TOMY", "HI-HAT", "TALERZ" };
    auto lights = a.removeFromTop(40);
    const int lw = lights.getWidth() / 5;
    for (int i = 0; i < 5; ++i)
    {
        auto cell = lights.removeFromLeft(lw).toFloat();
        const auto dot = Rectangle<float>(12.0f, 12.0f).withCentre({ cell.getCentreX(), cell.getY() + 10.0f });
        g.setColour(palette::grid);
        g.fillEllipse(dot);
        g.setColour(palette::violet.withAlpha(flash[i + 1]));
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
    meter(a.removeFromTop(30), U("Puchnięcie (tomy)"), shownSwell, palette::piano);
    meter(a.removeFromTop(30), U("Ziarna w powietrzu: ") + String(st.activeGrains.load()) + U("  ·  ")
                                   + String(roundToInt(st.grainsPerSecond.load())) + " /s",
          shownGrains / 64.0f, palette::vocal2);

    // akord, na który strojone są ziarna
    a.removeFromTop(6);
    auto chord = a.removeFromTop(46);
    g.setColour(palette::grid);
    g.fillRoundedRectangle(chord.toFloat(), 6.0f);
    g.setColour(accent);
    g.setFont(Font(FontOptions(26.0f, Font::bold)));
    g.drawText(chordName(st.chordRoot.load(), st.chordQuality.load()), chord.removeFromLeft(110), Justification::centred);
    g.setColour(palette::dim.brighter(0.3f));
    g.setFont(Font(FontOptions(11.5f)));
    g.drawFittedText(U("ziarna strojone\nna składniki akordu"), chord.reduced(6, 2), Justification::centredLeft, 2);

    String mode;
    if (proc.samplePool().size() == 0)
        mode = proc.pendingLoads() > 0 ? U("Rozpalam piec…") : U("Piec pusty – wrzuć sample wokalu");
    else if (st.activity.load() < 0.05f)
        mode = U("Czekam na bębniarza…");
    else if (shownSwell > 0.3f)
        mode = U("Przejście na tomach – chmura puchnie");
    else
        mode = U("Gram z pieca");
    a.removeFromTop(8);
    g.setColour(palette::text);
    g.setFont(Font(FontOptions(12.5f)));
    g.drawFittedText(mode, a.removeFromTop(34), Justification::centredLeft, 2);
}

void VocalEditor::paintChords(Graphics& g, Rectangle<int> row)
{
    const auto& st = proc.engine().status;
    const auto h = sk::HarmonySettings::unpack(st.harmonyWord.load());
    const auto hh = h.valid ? h : proc.currentParams().harmony;
    const int sec = st.section.load();
    const int prog = jlimit(0, sk::numProgressions - 1, sec ? hh.progB : hh.progA);

    g.setFont(Font(FontOptions(11.0f, Font::bold)));
    g.setColour(accent);
    g.drawText(sec ? "REFREN" : "ZWROTKA", row.removeFromLeft(70), Justification::centredLeft);
    const auto& pr = sk::progressions[prog];
    const int n = pr.length;
    const float gap = 5.0f;
    const float cw = (float(row.getWidth()) - gap * float(n - 1)) / float(n);
    const bool playing = st.playing.load() && st.bar.load() >= 0;
    for (int i = 0; i < n; ++i)
    {
        const auto chip = Rectangle<float>(float(row.getX()) + float(i) * (cw + gap), float(row.getY()), cw, float(row.getHeight()));
        const auto ch = sk::chordFor(hh.key, prog, i);
        const bool now = playing && st.chordPos.load() == i;
        g.setColour(now ? accent.withAlpha(0.3f) : palette::grid);
        g.fillRoundedRectangle(chip, 5.0f);
        g.setColour(now ? accent : palette::dim.withAlpha(0.6f));
        g.drawRoundedRectangle(chip.reduced(0.5f), 5.0f, now ? 1.5f : 1.0f);
        g.setColour(now ? Colours::white : palette::text);
        g.setFont(Font(FontOptions(n > 4 ? 12.0f : 14.0f, Font::bold)));
        g.drawText(chordName(ch.root, ch.quality), chip, Justification::centred);
    }
}

void VocalEditor::paint(Graphics& g)
{
    g.fillAll(palette::background);
    g.setColour(palette::grid.withAlpha(0.35f));
    for (int x = 0; x < W; x += 24) g.drawVerticalLine(x, float(top), float(H));
    for (int y = top; y < H; y += 24) g.drawHorizontalLine(y, 0.0f, float(W));

    if (banner)
    {
        Graphics::ScopedSaveState s(g);
        g.reduceClipRegion(0, 0, W, top);
        banner->drawWithin(g, Rectangle<float>(0.0f, 0.0f, float(W), float(W) * 0.2f), RectanglePlacement::stretchToFit, 1.0f);
    }

    auto title = Rectangle<float>(300.0f, 80.0f, 580.0f, 60.0f);
    g.setColour(palette::background.withAlpha(0.78f));
    g.fillRoundedRectangle(title, 8.0f);
    g.setColour(accent);
    g.fillRect(title.removeFromLeft(4.0f));
    g.setColour(palette::text);
    g.setFont(Font(FontOptions(26.0f, Font::bold)));
    g.drawText("SEKCJA KRZEMOWA", title.withTrimmedLeft(16.0f).removeFromTop(34.0f), Justification::bottomLeft);
    g.setColour(accent);
    g.setFont(Font(FontOptions(13.0f, Font::bold)));
    g.drawText(U("WOKAL  ·  ziarna z Twoich sampli, słucha bębnów i Piano"),
               title.withTrimmedLeft(16.0f).withTrimmedTop(36.0f), Justification::topLeft);

    paintBeing(g);
    paintStatus(g);

    const auto& st = proc.engine().status;
    String foot;
    if (st.playing.load() && st.bar.load() >= 0)
        foot << "Takt " << (st.bar.load() + 1) << U("  ·  ") << (st.section.load() ? "Refren" : "Zwrotka") << U("  ·  ");
    foot << "Piano: " << (st.followingPiano.load() ? U("połączony") : "brak") << " (" << groupBox.getText() << ")"
         << U("  ·  piec: ") << proc.samplePool().size() << "/" << sk::PoolSnapshot::maxSamples;
    if (proc.pendingLoads() > 0)
        foot << U("  ·  wczytuję ") << proc.pendingLoads();
    g.setColour(palette::dim);
    g.setFont(Font(FontOptions(12.0f)));
    g.drawText(foot, footerArea, Justification::centredLeft);
    g.setColour(st.followingPiano.load() ? accent : palette::dim);
    g.fillEllipse(Rectangle<float>(8.0f, 8.0f).withCentre({ float(footerArea.getRight()) - 4.0f, float(footerArea.getCentreY()) }));
}

void VocalEditor::paintOverChildren(Graphics& g)
{
    const auto& st = proc.engine().status;

    // folder pieca / błąd wczytywania
    g.setFont(Font(FontOptions(11.5f)));
    String folder;
    if (proc.lastError().isNotEmpty())
    {
        g.setColour(palette::piano2);
        folder = proc.lastError();
    }
    else
    {
        g.setColour(palette::dim.brighter(0.3f));
        const auto f = proc.getFurnaceFolder();
        folder = f.isDirectory() ? U("obserwuję: ") + f.getFileName() : U("bez folderu – przeciągnij pliki albo folder");
    }
    g.drawFittedText(folder, folderInfoArea, Justification::centredLeft, 1);

    if (proc.samplePool().size() == 0)
    {
        g.setColour(palette::dim.brighter(0.2f));
        g.setFont(Font(FontOptions(14.0f)));
        g.drawFittedText(proc.pendingLoads() > 0
                             ? U("Wczytuję i analizuję wysokość…")
                             : U("Przeciągnij tu sample wokalu (WAV, AIFF, FLAC, MP3) albo cały folder.\n"
                                 "Możesz dorzucać w trakcie grania – każdy nowy sampel wpada do mieszanki."),
                         furnaceList.getBounds().reduced(20), Justification::centred, 3);
    }
    if (dragging)
    {
        g.setColour(accent.withAlpha(0.12f));
        g.fillRoundedRectangle(furnaceGroup.getBounds().toFloat(), 6.0f);
        g.setColour(accent);
        g.drawRoundedRectangle(furnaceGroup.getBounds().toFloat().reduced(1.0f), 6.0f, 2.0f);
    }

    g.setFont(Font(FontOptions(12.5f)));
    g.setColour(st.followingPiano.load() ? palette::text : palette::piano2);
    g.drawFittedText(st.followingPiano.load()
                         ? U("Połączono z Piano – ziarna idą za jego progresją (ustawienia poniżej nieaktywne).")
                         : U("Brak Piano na tym linku – gram własną progresję z ustawień poniżej."),
                     linkInfoArea, Justification::centredLeft, 1);
    paintChords(g, chordRow);
}
