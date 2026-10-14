#pragma once
// Без зависимостей от JUCE: движок и тесты собираются отдельно от фреймворка.
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace freq
{
    inline constexpr int kMaxStations = 16;

    enum class PlaybackMode { Loop = 0, OneShot = 1 };

    // Неизменяемые подготовленные аудиоданные станции (уже ресемплированы, нормализованы, без DC).
    // Создаются вне аудиопотока (StationPrep), после публикации не модифицируются.
    struct StationBuffer
    {
        std::uint64_t id = 0;                         // уникален для каждого буфера (защита от ABA по адресу)
        std::array<std::vector<float>, 2> ch;         // ch[1] пуст для моно
        int numChannels = 1;
        int length = 0;
        double sampleRate = 44100.0;
        float normalizeGain = 1.0f;                   // применённое усиление нормализации (информационно)
    };

    // Всё, что аудиопотоку нужно знать о слоте. Меняется только публикацией нового снимка.
    struct StationRuntime
    {
        std::shared_ptr<const StationBuffer> buffer;
        float freqMHz = 0.f;
        float strength = 1.f;                         // 0..1, «мощность передатчика»
        float gain = 1.f;                             // линейное усиление станции
        float loopFadeMs = 20.f;                      // кроссфейд на границе цикла
        PlaybackMode mode = PlaybackMode::Loop;
        bool enabled = false;
    };

    // Снимок всех станций, который видит аудиопоток. Читается без блокировок и без refcount.
    struct StationSnapshot
    {
        std::uint64_t serial = 0;
        std::array<StationRuntime, kMaxStations> slots;
    };
}
