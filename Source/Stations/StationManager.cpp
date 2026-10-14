#include "StationManager.h"
#include <cmath>

namespace freq
{
    StationManager::StationManager()
    {
        formats_.registerBasicFormats();   // WAV, AIFF и др. базовые форматы JUCE
        auto empty = std::make_shared<StationSnapshot>();
        empty->serial = nextSerial_++;
        current_.store(empty.get(), std::memory_order_release);
        retained_.push_back(std::move(empty));
    }

    StationManager::~StationManager()
    {
        alive_->store(false);                 // отложенные callAsync после уничтожения станут no-op
        pool_.removeAllJobs(true, 5000);
    }

    // Строит новый снимок (копия последнего + изменённый слот) и публикует его атомарно.
    void StationManager::publishSlot(int slot, std::shared_ptr<const StationBuffer> buf)
    {
        jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
        auto next = std::make_shared<StationSnapshot>(*retained_.back());
        next->serial = nextSerial_++;

        const auto& m = metas_[(size_t) slot];
        auto& rt = next->slots[(size_t) slot];
        rt.buffer     = std::move(buf);
        rt.freqMHz    = m.frequencyMHz;
        rt.strength   = juce::jlimit(0.f, 1.f, m.signalStrength);
        rt.gain       = juce::Decibels::decibelsToGain(juce::jlimit(-60.f, 24.f, m.gainDb));
        rt.loopFadeMs = juce::jlimit(0.f, 500.f, m.loopFadeMs);
        rt.mode       = m.mode;
        rt.enabled    = m.enabled && rt.buffer != nullptr;

        const auto* raw = next.get();
        retained_.push_back(std::move(next));
        current_.store(raw, std::memory_order_release);
    }

    void StationManager::applyMeta(int slot)
    {
        if (slot < 0 || slot >= kMaxStations) return;
        publishSlot(slot, retained_.back()->slots[(size_t) slot].buffer);
    }

    void StationManager::reclaim()
    {
        const auto inUse = inUseSerial_.load(std::memory_order_acquire);
        while (retained_.size() > 1 && retained_.front()->serial < inUse
               && retained_.front().get() != current_.load(std::memory_order_acquire))
            retained_.erase(retained_.begin());
    }

    void StationManager::loadFileAsync(int slot, const juce::File& file)
    {
        if (slot < 0 || slot >= kMaxStations) return;
        meta(slot).file = file;
        if (meta(slot).name.isEmpty())
            meta(slot).name = file.getFileNameWithoutExtension();

        auto alive = alive_;
        pool_.addJob([this, alive, slot, file]
        {
            std::shared_ptr<StationBuffer> buf;
            double durationSec = 0.0, fileRate = 0.0;
            std::unique_ptr<juce::AudioFormatReader> reader(formats_.createReaderFor(file));

            if (reader != nullptr && reader->lengthInSamples > 0 && reader->numChannels > 0)
            {
                fileRate = reader->sampleRate;
                durationSec = reader->sampleRate > 0 ? (double) reader->lengthInSamples / reader->sampleRate : 0.0;
                PrepareOptions opt;
                opt.targetRate = engineRate_.load();
                const auto maxLen = (juce::int64) (opt.maxSeconds * reader->sampleRate);
                const int len = (int) juce::jmin(reader->lengthInSamples, maxLen);
                const int chans = (int) juce::jmin(2u, reader->numChannels);

                juce::AudioBuffer<float> tmp(chans, len);
                if (reader->read(&tmp, 0, len, 0, true, chans > 1))
                {
                    const float* ptrs[2] = { tmp.getReadPointer(0), chans > 1 ? tmp.getReadPointer(1) : nullptr };
                    buf = prepareStation(ptrs, chans, len, reader->sampleRate, opt);   // ресемплинг + нормализация, вне аудиопотока
                }
            }

            juce::MessageManager::callAsync([this, alive, slot, buf, durationSec, fileRate]
            {
                if (! alive->load()) return;
                auto& m = meta(slot);
                m.loaded  = (buf != nullptr);
                m.missing = (buf == nullptr);
                m.durationSec = durationSec; m.fileRate = fileRate;
                if (m.frequencyMHz <= 0.f) m.frequencyMHz = defaultFrequency(slot);
                publishSlot(slot, buf);
            });
        });
    }

    void StationManager::removeStation(int slot)
    {
        if (slot < 0 || slot >= kMaxStations) return;
        meta(slot) = StationMeta{};
        publishSlot(slot, nullptr);
    }

    // prepareToPlay может прийти не с message-потока: пересчёт станций отправляем туда.
    void StationManager::setSampleRate(double newRate)
    {
        if (! (newRate > 0.0)) return;
        const double old = engineRate_.exchange(newRate);
        if (std::fabs(old - newRate) < 0.5) return;
        auto alive = alive_;
        juce::MessageManager::callAsync([this, alive] { if (alive->load()) reloadAll(); });
    }

    void StationManager::reloadAll()
    {
        for (int i = 0; i < kMaxStations; ++i)
        {
            auto& m = meta(i);
            if (m.file != juce::File{} && m.file.existsAsFile()) loadFileAsync(i, m.file);
        }
    }

    juce::ValueTree StationManager::toValueTree() const
    {
        juce::ValueTree v("STATIONS");
        for (int i = 0; i < kMaxStations; ++i)
        {
            const auto& m = metas_[(size_t) i];
            if (m.file == juce::File{} && ! m.loaded) continue;
            juce::ValueTree s("STATION");
            s.setProperty("slot", i, nullptr);
            s.setProperty("name", m.name, nullptr);
            s.setProperty("path", m.file.getFullPathName(), nullptr);
            s.setProperty("relPath", m.relativePath, nullptr);
            s.setProperty("freq", m.frequencyMHz, nullptr);
            s.setProperty("strength", m.signalStrength, nullptr);
            s.setProperty("gainDb", m.gainDb, nullptr);
            s.setProperty("fadeMs", m.loopFadeMs, nullptr);
            s.setProperty("mode", (int) m.mode, nullptr);
            s.setProperty("enabled", m.enabled, nullptr);
            v.appendChild(s, nullptr);
        }
        return v;
    }

    void StationManager::fromValueTree(const juce::ValueTree& v)
    {
        for (auto s : v)
        {
            const int slot = s.getProperty("slot", -1);
            if (slot < 0 || slot >= kMaxStations) continue;
            auto& m = meta(slot);
            m.name = s.getProperty("name").toString();
            m.file = juce::File(s.getProperty("path").toString());
            m.relativePath = s.getProperty("relPath").toString();
            m.frequencyMHz = s.getProperty("freq", 0.f);
            m.signalStrength = s.getProperty("strength", 1.f);
            m.gainDb = s.getProperty("gainDb", 0.f);
            m.loopFadeMs = s.getProperty("fadeMs", 20.f);
            m.mode = (PlaybackMode) (int) s.getProperty("mode", 0);
            m.enabled = s.getProperty("enabled", true);
            if (m.file.existsAsFile()) loadFileAsync(slot, m.file);
            else                       m.missing = true;     // UI покажет «файл не найден»
        }
    }
}
