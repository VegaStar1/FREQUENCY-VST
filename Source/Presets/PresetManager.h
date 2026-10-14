#pragma once
#include "Presets/FactoryPresets.h"
#include "Presets/PresetFile.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

namespace freq
{
    // Только message-поток. Пресет меняет параметры хоста (через жесты автоматизации); плавность
    // обеспечивают сглаживатели движка, поэтому смена пресета не даёт щелчков и скачков громкости.
    // Имя текущего пресета хранится в состоянии проекта (свойство "presetName" дерева параметров).
    class PresetManager
    {
    public:
        struct Entry { juce::String name; bool factory = false; juce::File file; };

        explicit PresetManager(juce::AudioProcessorValueTreeState& s) : apvts_(s) { refresh(); }

        juce::File userDirectory() const;
        void refresh();                                         // перечитать папку пользовательских пресетов
        const std::vector<Entry>& entries() const { return entries_; }

        juce::String currentName() const { return apvts_.state.getProperty("presetName").toString(); }
        bool isUserPreset(const juce::String& name) const;
        bool isModified();                                      // параметры отличаются от сохранённых значений пресета

        bool load(const Entry& e, juce::String& error);
        bool step(int delta, juce::String& error);              // следующий/предыдущий по кругу
        bool saveUser(const juce::String& name, juce::String& error);
        bool deleteUser(const juce::String& name, juce::String& error);

        preset::PresetValues readCurrent() const;

    private:
        void apply(const preset::PresetValues& v, const juce::String& name);
        bool lookupValues(const juce::String& name, preset::PresetValues& out);
        void setPlain(const char* id, float plain);

        juce::AudioProcessorValueTreeState& apvts_;
        std::vector<Entry> entries_;
        juce::String cachedName_;
        preset::PresetValues cachedValues_;
        bool cachedValid_ = false;
    };
}
