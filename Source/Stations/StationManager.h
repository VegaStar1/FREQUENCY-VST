#pragma once
#include "StationBuffer.h"
#include "StationPrep.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>
#include <atomic>
#include <memory>
#include <vector>

namespace freq
{
    // Метаданные слота. Принадлежат ТОЛЬКО message-потоку; в аудиопоток попадают через снимок (StationRuntime).
    struct StationMeta
    {
        bool         enabled = true;
        juce::String name;
        juce::File   file;
        juce::String relativePath;      // относительно папки ассетов проекта, если есть
        float        frequencyMHz = 0.f; // 0 = назначить по умолчанию при первой загрузке
        float        signalStrength = 1.f;
        float        gainDb = 0.f;
        float        loopFadeMs = 20.f;
        PlaybackMode mode = PlaybackMode::Loop;
        bool         missing = false;   // файл не найден / не декодировался
        bool         loaded = false;
    };

    // Владение и потоки:
    //  * message-поток: метаданные, publish, reclaim, сериализация.
    //  * worker-поток (ThreadPool): декодирование и подготовка (prepareStation) -> публикация через callAsync.
    //  * аудиопоток: ТОЛЬКО acquire() в начале блока (атомарные операции, без аллокаций и блокировок).
    //
    // Освобождение: message-поток держит все снимки в retained_ и удаляет только те, чей serial строго
    // меньше serial, подтверждённого аудиопотоком (inUseSerial_). Деструкторы буферов
    // поэтому никогда не выполняются на аудиопотоке.
    class StationManager
    {
    public:
        StationManager();
        ~StationManager();

        // --- message thread ---
        void loadFileAsync(int slot, const juce::File& file);
        void removeStation(int slot);
        void applyMeta(int slot);                    // после изменения частоты/усиления/режима/enabled: републикация
        void setSampleRate(double newRate);          // можно вызывать из любого потока; пересчёт - на message-потоке
        void reclaim();
        StationMeta&       meta(int slot)       { return metas_[(size_t) slot]; }
        const StationMeta& meta(int slot) const { return metas_[(size_t) slot]; }
        static float defaultFrequency(int slot) { return 88.5f + 1.2f * (float) slot; }

        juce::ValueTree toValueTree() const;         // без аудиоданных, только метаданные
        void fromValueTree(const juce::ValueTree&);

        // --- audio thread ---
        const StationSnapshot* acquire() noexcept
        {
            auto* s = current_.load(std::memory_order_acquire);
            if (s != nullptr)
                inUseSerial_.store(s->serial, std::memory_order_release);
            return s;
        }

    private:
        void publishSlot(int slot, std::shared_ptr<const StationBuffer> buf);
        void reloadAll();

        std::array<StationMeta, kMaxStations> metas_;
        std::vector<std::shared_ptr<const StationSnapshot>> retained_; // message thread only
        std::atomic<const StationSnapshot*> current_ { nullptr };
        std::atomic<std::uint64_t> inUseSerial_ { 0 };
        std::uint64_t nextSerial_ = 1;
        std::atomic<double> engineRate_ { 44100.0 };
        juce::AudioFormatManager formats_;
        juce::ThreadPool pool_ { 1 };
        std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);
    };
}
