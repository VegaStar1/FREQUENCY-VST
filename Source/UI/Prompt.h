#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace freq::ui
{
    // Небольшое модальное окно с одним текстовым полем (имя станции, частота, имя пресета).
    inline void promptText(const juce::String& title, const juce::String& initial,
                           std::function<void(const juce::String&)> onOk)
    {
        auto* w = new juce::AlertWindow(title, {}, juce::MessageBoxIconType::NoIcon);
        w->addTextEditor("t", initial);
        w->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
        w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        w->enterModalState(true,
            juce::ModalCallbackFunction::create([w, onOk](int result)
            {
                if (result == 1) onOk(w->getTextEditorContents("t"));
            }),
            true);
    }
}
