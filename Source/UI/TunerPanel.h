#pragma once
#include "PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace freq::ui
{
    // Центральная часть: большая ручка настройки, крупная частота, имя станции, качество приёма и шкала.
    class TunerPanel : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
    {
    public:
        explicit TunerPanel(FrequencyProcessor& p);
        ~TunerPanel() override { stopTimer(); }

        void paint(juce::Graphics&) override;
        void resized() override;
        void mouseDown(const juce::MouseEvent&) override;
        void mouseDrag(const juce::MouseEvent&) override;
        void mouseUp(const juce::MouseEvent&) override;
        void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    private:
        void timerCallback() override;
        void setFrequencyFromX(float x);
        void nudge(float deltaMHz);
        juce::String currentStationName() const;

        FrequencyProcessor& proc_;
        juce::Slider knob_;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> knobAtt_;
        juce::Rectangle<float> scale_;
        juce::Rectangle<int> readout_;
        bool draggingScale_ = false;
        float shown_ = 98.f, quality_ = 0.f;
    };
}
