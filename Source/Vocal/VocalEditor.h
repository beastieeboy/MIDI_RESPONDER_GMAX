#pragma once
#include "VocalProcessor.h"
#include "../Plugin/SiliconLookAndFeel.h"

class VocalEditor : public juce::AudioProcessorEditor,
                    public juce::FileDragAndDropTarget,
                    private juce::ListBoxModel,
                    private juce::Timer
{
public:
    explicit VocalEditor(VocalProcessor&);
    ~VocalEditor() override;

    void paint(juce::Graphics&) override;
    void paintOverChildren(juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void fileDragEnter(const juce::StringArray&, int, int) override { dragging = true; repaint(); }
    void fileDragExit(const juce::StringArray&) override { dragging = false; repaint(); }
    void filesDropped(const juce::StringArray& files, int, int) override;

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

    class Row;

    // ListBoxModel
    int getNumRows() override;
    void paintListBoxItem(int row, juce::Graphics&, int w, int h, bool selected) override;
    juce::Component* refreshComponentForRow(int row, bool selected, juce::Component* existing) override;

    void timerCallback() override;
    Control& addKnob(juce::OwnedArray<Control>& into, const juce::String& paramId, const juce::String& name);
    Control& addNumber(const juce::String& paramId, const juce::String& name);
    void addCombo(juce::ComboBox&, juce::Label&, const juce::String& paramId, const juce::String& name,
                  std::unique_ptr<ComboAttachment>&);
    void layoutKnobs(juce::OwnedArray<Control>& knobs, juce::Rectangle<int> area);
    void paintBeing(juce::Graphics&);
    void paintStatus(juce::Graphics&);
    void paintChords(juce::Graphics&, juce::Rectangle<int> row);
    void chooseFiles();
    void chooseFolder();

    VocalProcessor& proc;
    SiliconLookAndFeel lnf;
    const juce::Colour accent;
    std::unique_ptr<juce::Drawable> banner, being;

    juce::GroupComponent furnaceGroup, grainGroup, toneGroup, reactGroup, harmonyGroup;
    juce::TextButton addButton { U("Wsyp pliki…") }, folderButton { U("Folder pieca…") }, clearButton { U("Wyczyść") };
    juce::ListBox furnaceList;
    juce::Rectangle<int> folderInfoArea;

    juce::OwnedArray<Control> grainKnobs, toneKnobs, reactKnobs, numbers;
    juce::ToggleButton playAlwaysToggle { U("Graj bez bębniarza") }, fillToggle { U("Przejście zmienia sekcję") };
    std::unique_ptr<ButtonAttachment> playAlwaysAtt, fillAtt;

    juce::ComboBox keyBox, progABox, progBBox, groupBox, mapBox, modeBox, syncBox;
    juce::Label keyLabel, progALabel, progBLabel, groupLabel, mapLabel, modeLabel, syncLabel;
    std::unique_ptr<ComboAttachment> keyAtt, progAAtt, progBAtt, groupAtt, mapAtt, modeAtt, syncAtt;

    std::unique_ptr<juce::FileChooser> chooser;

    juce::Rectangle<int> beingArea, statusArea, modeArea, footerArea, linkInfoArea, chordRow;
    float flash[6] {};
    float shownEnergy = 0.0f, shownSwell = 0.0f, shownGrains = 0.0f;
    double phase = 0.0;
    bool dragging = false;
    int shownVersion = -1;
    std::vector<std::shared_ptr<sk::VocalSample>> contents;   // kopia pieca dla listy

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VocalEditor)
};
