#include "PluginEditor.h"

FrequencyEditor::FrequencyEditor(FrequencyProcessor& p)
    : AudioProcessorEditor(&p), generic_(p)
{
    addAndMakeVisible(generic_);
    setSize(520, 520);
}

void FrequencyEditor::paint(juce::Graphics& g) { g.fillAll(juce::Colour(0xff1c1d1f)); }
void FrequencyEditor::resized() { generic_.setBounds(getLocalBounds()); }
