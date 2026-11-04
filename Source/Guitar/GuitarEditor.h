#pragma once
#include "GuitarProcessor.h"
#include "../Plugin/SiliconLookAndFeel.h"

class GuitarEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit GuitarEditor(GuitarProcessor&);
    ~GuitarEditor() override;

    void paint(juce::Graphics&) override;
    void paintOverChildren(juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct Control
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<SliderAttachment> attachment;
    };

    void timerCallback() override;
    Control& addKnob(const juce::String& paramId, const juce::String& name);
    Control& addNumber(juce::OwnedArray<Control>& dst, const juce::String& paramId, const juce::String& name, int noteStyle);
    void addCombo(juce::ComboBox&, juce::Label&, const juce::String& paramId, const juce::String& name,
                  std::unique_ptr<ComboAttachment>&);
    void paintChords(juce::Graphics&, juce::Rectangle<int> row, const juce::String& title, int prog, int key, bool activeSection);
    void paintBeing(juce::Graphics&);
    void paintStatus(juce::Graphics&);
    void paintFretboard(juce::Graphics&, juce::Rectangle<int>);

    GuitarProcessor& proc;
    SiliconLookAndFeel lnf;
    const juce::Colour accent;

    std::unique_ptr<juce::Drawable> banner, being;

    juce::GroupComponent melodyGroup, feelGroup, kontaktGroup;
    juce::ComboBox roleBox, profileBox, groupBox, mapBox;
    juce::Label roleLabel, profileLabel, groupLabel, mapLabel;
    std::unique_ptr<ComboAttachment> roleAtt, profileAtt, groupAtt, mapAtt;

    juce::OwnedArray<Control> knobs, keyswitches, drumNotes;

    juce::Rectangle<int> beingArea, statusArea, chordRowA, chordRowB, linkInfoArea, phraseArea, fretArea, footerArea;
    float flash[6] {};
    float shownEnergy = 0.0f;
    double phase = 0.0;
    int lastPitchShown = -1;
    float pitchGlow = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GuitarEditor)
};
