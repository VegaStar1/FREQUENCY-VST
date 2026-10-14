#pragma once
#include "PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace freq::ui
{
    // 16 слотов станций (8x2): клик по пустому - загрузка файла, клик по загруженному - настройка на станцию,
    // правая кнопка - меню (заменить, удалить, переименовать, частота, режим, вкл/выкл, перезапуск).
    class StationStrip : public juce::Component, public juce::TooltipClient, private juce::Timer
    {
    public:
        explicit StationStrip(FrequencyProcessor& p);
        ~StationStrip() override { stopTimer(); }

        void paint(juce::Graphics&) override;
        void mouseDown(const juce::MouseEvent&) override;
        juce::String getTooltip() override;

    private:
        static constexpr int kCols = 8, kRows = 2;
        void timerCallback() override { repaint(); }
        juce::Rectangle<int> slotBounds(int slot) const;
        int slotAt(juce::Point<int> pos) const;
        void chooseFile(int slot);
        void showMenu(int slot);
        void promptText(const juce::String& title, const juce::String& initial,
                        std::function<void(const juce::String&)> onOk);

        FrequencyProcessor& proc_;
        std::unique_ptr<juce::FileChooser> chooser_;
    };
}
