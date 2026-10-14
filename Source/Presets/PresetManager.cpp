#include "PresetManager.h"
#include <algorithm>

namespace freq
{
    juce::File PresetManager::userDirectory() const
    {
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                   .getChildFile("FrequencyAudio").getChildFile("FREQUENCY").getChildFile("Presets");
    }

    void PresetManager::refresh()
    {
        entries_.clear();
        for (const auto& p : preset::kFactoryPresets)
            entries_.push_back({ juce::String(p.name), true, {} });

        auto files = userDirectory().findChildFiles(juce::File::findFiles, false, "*.freqpreset");
        std::sort(files.begin(), files.end(), [](const juce::File& a, const juce::File& b)
        { return a.getFileNameWithoutExtension().compareIgnoreCase(b.getFileNameWithoutExtension()) < 0; });

        for (const auto& f : files)
        {
            const auto name = f.getFileNameWithoutExtension();
            const bool clash = std::any_of(entries_.begin(), entries_.end(), [&](const Entry& e)
                                           { return e.factory && e.name.equalsIgnoreCase(name); });
            if (! clash) entries_.push_back({ name, false, f });   // имя совпало с фабричным: файл не показываем
        }
    }

    bool PresetManager::isUserPreset(const juce::String& name) const
    {
        return std::any_of(entries_.begin(), entries_.end(), [&](const Entry& e) { return ! e.factory && e.name == name; });
    }

    void PresetManager::setPlain(const char* id, float plain)
    {
        if (auto* p = apvts_.getParameter(id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost(p->convertTo0to1(plain));
            p->endChangeGesture();
        }
    }

    void PresetManager::apply(const preset::PresetValues& vIn, const juce::String& name)
    {
        const auto v = preset::clampValues(vIn);
        setPlain(param::reception, v.reception);       setPlain(param::noise, v.noise);
        setPlain(param::selectivity, v.selectivity);   setPlain(param::tuningSpeed, v.tuningSpeed);
        setPlain(param::scanSensitivity, v.scanSensitivity);
        setPlain(param::saturation, v.saturation);     setPlain(param::width, v.width);
        setPlain(param::outputGain, v.outputGainDb);
        setPlain(param::mode, v.analog ? 0.f : 1.f);
        setPlain(param::mono, v.mono ? 1.f : 0.f);
        setPlain(param::limiterBypass, v.limiterBypass ? 1.f : 0.f);
        apvts_.state.setProperty("presetName", name, nullptr);
        cachedName_ = name; cachedValues_ = v; cachedValid_ = true;
    }

    preset::PresetValues PresetManager::readCurrent() const
    {
        auto get = [this](const char* id) { auto* a = apvts_.getRawParameterValue(id); return a ? a->load() : 0.f; };
        preset::PresetValues v;
        v.reception = get(param::reception);   v.noise = get(param::noise);
        v.selectivity = get(param::selectivity); v.tuningSpeed = get(param::tuningSpeed);
        v.scanSensitivity = get(param::scanSensitivity); v.saturation = get(param::saturation);
        v.width = get(param::width);           v.outputGainDb = get(param::outputGain);
        v.analog = get(param::mode) < 0.5f;    v.mono = get(param::mono) > 0.5f;
        v.limiterBypass = get(param::limiterBypass) > 0.5f;
        return v;
    }

    bool PresetManager::lookupValues(const juce::String& name, preset::PresetValues& out)
    {
        for (const auto& p : preset::kFactoryPresets)
            if (name == p.name) { out = p.values; return true; }
        for (const auto& e : entries_)
            if (! e.factory && e.name == name)
            {
                if (e.file.getSize() > (juce::int64) preset::kMaxPresetFileBytes) return false;
                const auto r = preset::parsePreset(e.file.loadFileAsString().toStdString());
                if (! r.ok) return false;
                out = r.values; return true;
            }
        return false;
    }

    bool PresetManager::isModified()
    {
        const auto name = currentName();
        if (name.isEmpty()) return false;
        if (name != cachedName_) { cachedName_ = name; cachedValid_ = lookupValues(name, cachedValues_); }
        return cachedValid_ && ! preset::approxEqual(readCurrent(), cachedValues_);
    }

    bool PresetManager::load(const Entry& e, juce::String& error)
    {
        if (e.factory)
        {
            for (const auto& p : preset::kFactoryPresets)
                if (e.name == p.name) { apply(p.values, e.name); return true; }
            error = "Preset not found."; return false;
        }
        if (! e.file.existsAsFile()) { error = "Preset file not found."; return false; }
        if (e.file.getSize() > (juce::int64) preset::kMaxPresetFileBytes) { error = "Preset file is too large."; return false; }
        const auto r = preset::parsePreset(e.file.loadFileAsString().toStdString());
        if (! r.ok) { error = "Cannot read preset: " + juce::String(r.error); return false; }
        apply(r.values, e.name);
        return true;
    }

    bool PresetManager::step(int delta, juce::String& error)
    {
        refresh();
        if (entries_.empty()) return false;
        const auto cur = currentName();
        int idx = -1;
        for (size_t i = 0; i < entries_.size(); ++i) if (entries_[i].name == cur) { idx = (int) i; break; }
        const int n = (int) entries_.size();
        const int next = idx < 0 ? (delta > 0 ? 0 : n - 1) : ((idx + delta) % n + n) % n;
        return load(entries_[(size_t) next], error);
    }

    bool PresetManager::saveUser(const juce::String& name, juce::String& error)
    {
        const std::string fileName = preset::sanitizeFileName(name.toStdString());
        for (const auto& p : preset::kFactoryPresets)
            if (juce::String(p.name).equalsIgnoreCase(fileName)) { error = "This name is used by a factory preset."; return false; }

        const auto dir = userDirectory();
        const auto made = dir.createDirectory();
        if (made.failed()) { error = "Cannot create preset folder: " + made.getErrorMessage(); return false; }

        const auto file = dir.getChildFile(juce::String(fileName) + ".freqpreset");
        const auto text = preset::serializePreset(fileName, readCurrent());
        if (! file.replaceWithText(juce::String(text), false, false, "\n")) { error = "Cannot write " + file.getFullPathName(); return false; }

        apvts_.state.setProperty("presetName", juce::String(fileName), nullptr);
        cachedName_ = {}; cachedValid_ = false;
        refresh();
        return true;
    }

    bool PresetManager::deleteUser(const juce::String& name, juce::String& error)
    {
        for (const auto& e : entries_)
            if (! e.factory && e.name == name)
            {
                if (! e.file.deleteFile()) { error = "Cannot delete " + e.file.getFullPathName(); return false; }
                if (currentName() == name) apvts_.state.setProperty("presetName", juce::String(), nullptr);
                cachedName_ = {}; cachedValid_ = false;
                refresh();
                return true;
            }
        error = "Only user presets can be deleted."; return false;
    }
}
