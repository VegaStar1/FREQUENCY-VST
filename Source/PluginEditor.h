#pragma once
#include "PluginProcessor.h"

// Временный редактор на стандартных контролах: проверка привязки параметров.
// Кастомный интерфейс (ручка, шкала, станции) - этап 4.
class FrequencyEditor : public juce::AudioProcessorEditor
{
public:
    explicit FrequencyEditor(FrequencyProcessor& p);
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    juce::GenericAudioProcessorEditor generic_;
};
