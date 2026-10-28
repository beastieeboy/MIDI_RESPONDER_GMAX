#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// Kolory z grafik istot krzemowych (tools/generuj_grafiki.py).
namespace palette {
    inline const juce::Colour background { 0xff0b0f16 };
    inline const juce::Colour panel { 0xff121925 };
    inline const juce::Colour grid { 0xff1f2836 };
    inline const juce::Colour dim { 0xff55607a };
    inline const juce::Colour text { 0xffd9dde8 };
    inline const juce::Colour violet { 0xffa99bff };
    inline const juce::Colour piano { 0xffffbe73 };
    inline const juce::Colour piano2 { 0xffff8f6b };
    inline const juce::Colour bass { 0xff55e6d6 };
    inline const juce::Colour bass2 { 0xff4c9dff };
    inline const juce::Colour vocal { 0xffff7ad9 };
    inline const juce::Colour vocal2 { 0xffb07bff };
}

class SiliconLookAndFeel : public juce::LookAndFeel_V4
{
public:
    explicit SiliconLookAndFeel(juce::Colour accent);

    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end,
                          juce::Slider&) override;
    void drawComboBox(juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawGroupComponentOutline(juce::Graphics&, int w, int h, const juce::String& text,
                                   const juce::Justification&, juce::GroupComponent&) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;
    juce::Font getLabelFont(juce::Label&) override;
    juce::Font getPopupMenuFont() override;

    juce::Colour accent;
};
