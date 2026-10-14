#pragma once
#include "PluginProcessor.h"
#include "Presets/PresetManager.h"
#include "UI/StationStrip.h"
#include "UI/Theme.h"
#include "UI/TunerPanel.h"
#include <array>

class FrequencyEditor : public juce::AudioProcessorEditor,
                        public juce::FileDragAndDropTarget,
                        private juce::Timer
{
public:
    explicit FrequencyEditor(FrequencyProcessor& p);
    ~FrequencyEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    // Перетаскивание аудиофайлов (из проводника ОС и из DAW, если хост отдаёт настоящий файловый drag)
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void fileDragEnter(const juce::StringArray& files, int x, int y) override;
    void fileDragMove(const juce::StringArray& files, int x, int y) override;
    void fileDragExit(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

private:
    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAtt  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<SliderAtt> att;
    };

    void timerCallback() override;
    void updateDropHighlight(int x, int y);
    void showPresetMenu();
    void promptSavePreset();
    void stepPreset(int delta);
    static void showError(const juce::String& message);
    void addKnob(Knob& k, const char* paramId, const juce::String& text, const juce::String& tip);

    FrequencyProcessor& proc_;
    freq::ui::FrequencyLookAndFeel laf_;
    juce::TooltipWindow tooltip_ { this, 500 };

    freq::PresetManager presets_;
    freq::ui::TunerPanel tuner_;
    freq::ui::StationStrip strip_;

    std::array<Knob, 8> knobs_;
    juce::TextButton searchBtn_ { "SEARCH" }, scanBtn_ { "SCAN" }, stopBtn_ { "STOP" }, lockBtn_ { "LOCK" };
    juce::ComboBox modeBox_;
    juce::ToggleButton monoBtn_ { "MONO" }, limBypassBtn_ { "LIMITER BYPASS" };
    std::unique_ptr<ComboAtt> modeAtt_;
    std::unique_ptr<ButtonAtt> monoAtt_, limAtt_;

    juce::TextButton presetBtn_ { "Init" }, prevPresetBtn_ { "<" }, nextPresetBtn_ { ">" }, savePresetBtn_ { "SAVE" };
    juce::String shownPreset_;
    bool dragging_ = false;

    juce::Rectangle<int> meterArea_;
    float meter_ = 0.f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FrequencyEditor)
};
