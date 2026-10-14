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
        int slotAt(juce::Point<int> pos) const;                 // -1, если точка вне слотов
        void setDropSlot(int slot) { if (slot != dropSlot_) { dropSlot_ = slot; repaint(); } }

    private:
        static constexpr int kCols = 8, kRows = 2;
        void timerCallback() override { repaint(); }
        juce::Rectangle<int> slotBounds(int slot) const;
        void chooseFile(int slot);
        void showMenu(int slot);

        FrequencyProcessor& proc_;
        std::unique_ptr<juce::FileChooser> chooser_;
        int dropSlot_ = -1;
    };
}
