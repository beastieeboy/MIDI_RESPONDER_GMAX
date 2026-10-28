// Renderuje edytor wtyczki do PNG bez hosta (podgląd GUI w CI / bez macOS).
#include "../Source/Plugin/PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>

int main(int argc, char** argv)
{
    if (argc < 2) return 2;
    juce::ScopedJuceInitialiser_GUI gui;
    std::unique_ptr<juce::AudioProcessor> proc(createPluginFilter());
    std::unique_ptr<juce::AudioProcessorEditor> ed(proc->createEditor());
    for (int i = 0; i < 5; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
    auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.5f);
    juce::File out(argv[1]);
    out.deleteFile();
    juce::FileOutputStream os(out);
    juce::PNGImageFormat().writeImageToStream(img, os);
    ed.reset();
    return 0;
}
