#include "SiliconLookAndFeel.h"

using namespace juce;

SiliconLookAndFeel::SiliconLookAndFeel(Colour a) : accent(a)
{
    setColour(ResizableWindow::backgroundColourId, palette::background);
    setColour(Label::textColourId, palette::text);
    setColour(Slider::textBoxTextColourId, palette::text);
    setColour(Slider::textBoxOutlineColourId, Colours::transparentBlack);
    setColour(Slider::textBoxBackgroundColourId, palette::grid.withAlpha(0.6f));
    setColour(Slider::rotarySliderFillColourId, accent);
    setColour(Slider::rotarySliderOutlineColourId, palette::grid);
    setColour(ComboBox::backgroundColourId, palette::grid);
    setColour(ComboBox::textColourId, palette::text);
    setColour(ComboBox::outlineColourId, palette::dim);
    setColour(ComboBox::arrowColourId, accent);
    setColour(PopupMenu::backgroundColourId, palette::panel);
    setColour(PopupMenu::textColourId, palette::text);
    setColour(PopupMenu::highlightedBackgroundColourId, accent.withAlpha(0.25f));
    setColour(PopupMenu::highlightedTextColourId, Colours::white);
    setColour(ToggleButton::textColourId, palette::text);
    setColour(GroupComponent::textColourId, accent);
    setColour(GroupComponent::outlineColourId, palette::grid);
}

void SiliconLookAndFeel::drawRotarySlider(Graphics& g, int x, int y, int w, int h, float pos, float start, float end, Slider&)
{
    const auto b = Rectangle<int>(x, y, w, h).toFloat().reduced(6.0f);
    const float r = jmin(b.getWidth(), b.getHeight()) * 0.5f;
    const auto c = b.getCentre();
    const float angle = start + pos * (end - start);

    // tarcza: wielokąt jak "twarz" istoty
    Path hex;
    for (int i = 0; i < 6; ++i)
    {
        const float a = MathConstants<float>::twoPi * float(i) / 6.0f;
        const Point<float> p(c.x + r * 0.72f * std::sin(a), c.y - r * 0.72f * std::cos(a));
        i == 0 ? hex.startNewSubPath(p) : hex.lineTo(p);
    }
    hex.closeSubPath();
    g.setColour(palette::grid);
    g.fillPath(hex);
    g.setColour(palette::dim.withAlpha(0.6f));
    g.strokePath(hex, PathStrokeType(1.0f));

    Path track, value;
    track.addCentredArc(c.x, c.y, r, r, 0.0f, start, end, true);
    value.addCentredArc(c.x, c.y, r, r, 0.0f, start, angle, true);
    g.setColour(palette::grid.brighter(0.2f));
    g.strokePath(track, PathStrokeType(3.0f, PathStrokeType::curved, PathStrokeType::rounded));
    g.setColour(accent);
    g.strokePath(value, PathStrokeType(3.0f, PathStrokeType::curved, PathStrokeType::rounded));

    const Point<float> tip(c.x + r * 0.62f * std::sin(angle), c.y - r * 0.62f * std::cos(angle));
    g.setColour(accent.brighter(0.4f));
    g.drawLine({ c, tip }, 2.0f);
    g.fillEllipse(Rectangle<float>(5.0f, 5.0f).withCentre(tip));
}

void SiliconLookAndFeel::drawComboBox(Graphics& g, int w, int h, bool, int, int, int, int, ComboBox& box)
{
    const auto b = Rectangle<int>(w, h).toFloat().reduced(0.5f);
    g.setColour(box.findColour(ComboBox::backgroundColourId));
    g.fillRoundedRectangle(b, 4.0f);
    g.setColour(box.hasKeyboardFocus(true) ? accent : palette::dim.withAlpha(0.7f));
    g.drawRoundedRectangle(b, 4.0f, 1.0f);
    Path arrow;
    const float ax = float(w) - 14.0f, ay = float(h) * 0.5f;
    arrow.addTriangle(ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
    g.setColour(accent);
    g.fillPath(arrow);
}

void SiliconLookAndFeel::drawToggleButton(Graphics& g, ToggleButton& b, bool highlighted, bool)
{
    const auto r = b.getLocalBounds().toFloat();
    const auto box = Rectangle<float>(r.getX() + 2.0f, r.getCentreY() - 8.0f, 30.0f, 16.0f);
    const bool on = b.getToggleState();
    g.setColour(on ? accent.withAlpha(0.35f) : palette::grid);
    g.fillRoundedRectangle(box, 8.0f);
    g.setColour(on ? accent : palette::dim);
    g.drawRoundedRectangle(box, 8.0f, 1.0f);
    const float knobX = on ? box.getRight() - 14.0f : box.getX() + 2.0f;
    g.fillEllipse(knobX, box.getY() + 2.0f, 12.0f, 12.0f);
    g.setColour(highlighted ? Colours::white : palette::text);
    g.setFont(Font(FontOptions(13.0f)));
    g.drawFittedText(b.getButtonText(), r.withTrimmedLeft(40.0f).toNearestInt(), Justification::centredLeft, 2);
}

void SiliconLookAndFeel::drawGroupComponentOutline(Graphics& g, int w, int h, const String& text,
                                                   const Justification&, GroupComponent&)
{
    const auto b = Rectangle<int>(w, h).toFloat().reduced(0.5f);
    g.setColour(palette::panel);
    g.fillRoundedRectangle(b, 6.0f);
    g.setColour(palette::grid.brighter(0.15f));
    g.drawRoundedRectangle(b, 6.0f, 1.0f);
    g.setColour(accent);
    g.fillRect(12.0f, 0.5f, 28.0f, 2.0f);
    g.setFont(Font(FontOptions(11.5f, Font::bold)));
    g.drawText(text, Rectangle<float>(12.0f, 6.0f, b.getWidth() - 24.0f, 14.0f), Justification::centredLeft);
}

Font SiliconLookAndFeel::getComboBoxFont(ComboBox&) { return Font(FontOptions(13.0f)); }
Font SiliconLookAndFeel::getLabelFont(Label&) { return Font(FontOptions(12.5f)); }
Font SiliconLookAndFeel::getPopupMenuFont() { return Font(FontOptions(13.5f)); }
