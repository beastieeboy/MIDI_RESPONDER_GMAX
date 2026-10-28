#pragma once
#include "PluginProcessor.h"
#include "SiliconLookAndFeel.h"

class SekcjaEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SekcjaEditor(SekcjaProcessor&);
    ~SekcjaEditor() override;

    void paint(juce::Graphics&) override;
    void paintOverChildren(juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct Control
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<SliderAttachment> attachment;
    };

    void timerCallback() override;
    Control& addKnob(const juce::String& paramId, const juce::String& name);
    Control& addNumber(const juce::String& paramId, const juce::String& name, bool noteName);
    void addCombo(juce::ComboBox&, juce::Label&, const juce::String& paramId, const juce::String& name,
                  std::unique_ptr<ComboAttachment>&);
    void paintChords(juce::Graphics&, juce::Rectangle<int> row, const juce::String& title, int prog, int key,
                     bool activeSection);
    void paintBeing(juce::Graphics&);
    void paintStatus(juce::Graphics&);

    SekcjaProcessor& proc;
    SiliconLookAndFeel lnf;
    const juce::Colour accent;

    std::unique_ptr<juce::Drawable> banner, being;

    juce::GroupComponent harmonyGroup, feelGroup, drumGroup;
    juce::ComboBox keyBox, progABox, progBBox, groupBox;
    juce::Label keyLabel, progALabel, progBLabel, groupLabel;
    std::unique_ptr<ComboAttachment> keyAtt, progAAtt, progBAtt, groupAtt;
    juce::ToggleButton fillToggle { U("Przejście na tomach zmienia sekcję") }, giveWayToggle { U("Ustępuj basowi (bez niskich nut)") };
    std::unique_ptr<ButtonAttachment> fillAtt, giveWayAtt;

    juce::OwnedArray<Control> knobs, numbers;

    juce::Rectangle<int> beingArea, statusArea, chordRowA, chordRowB, footerArea, linkInfoArea;
    float flash[6] {};
    float shownEnergy = 0.0f;
    double phase = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SekcjaEditor)
};
