#pragma once
#include "Dsp/Biquad.h"
#include "Dsp/NoiseEngine.h"
#include "Dsp/Saturation.h"
#include "Dsp/Scanner.h"
#include "Dsp/TuningModel.h"
#include "Stations/StationBuffer.h"
#include <array>
#include <atomic>

// Аудиодвижок FREQUENCY. Чистый C++ (без JUCE): тестируется офлайн, вызывается из processBlock.
//
// Сигнальная цепь (на каждый сэмпл):
//   настройка (сглаженная) -> сила каждой станции (гаусс по расстоянию) -> смешивание с ограничением мощности
//   -> качество приёма Q -> [флаттер] -> HP+LP (полоса зависит от Q) -> сатурация -> + шум (статик/помехи)
//   -> ширина/моно (M/S) -> выходное усиление -> мягкий потолок (опц.) -> защита от NaN/Inf.
//
// Вход плагина НЕ используется: станции - самостоятельные источники звука (работают без входного сигнала).
// Станции «вещают» непрерывно и свободно (не привязаны к транспорту хоста): как в радиоприёмнике,
// настройка на станцию подхватывает то место, где она сейчас находится.
// Аудиопоток: без аллокаций, блокировок и файлового ввода-вывода.
namespace freq::dsp
{
    inline constexpr float kFreqMin = 87.5f;
    inline constexpr float kFreqMax = 108.0f;

    struct EngineParams
    {
        float frequency = 98.0f, reception = 0.85f, noise = 0.25f, selectivity = 0.5f;
        float tuningSpeed = 0.5f, scanSensitivity = 0.5f, saturation = 0.2f, width = 1.0f, outputGainDb = 0.0f;
        bool analog = true, mono = false, limiterBypass = false;

        EngineParams sanitized() const;        // NaN/Inf -> значения по умолчанию, остальное - в безопасные диапазоны
    };

    enum class Command : int { None = 0, Search, SearchDown, Scan, Stop, Lock, Unlock, Restart };

    class RadioEngine
    {
    public:
        void prepare(double sampleRate);
        void reset();

        // Из любого потока (последняя команда побеждает до следующего блока).
        void postCommand(Command c) noexcept { pending_.store((int) c, std::memory_order_release); }
        // Только из аудиопотока (перед process): переход на станцию по частоте.
        void tuneTo(float mhz) noexcept;

        // out: numChannels указателей (1 или 2 используются). Полностью перезаписывает буфер.
        void process(const StationSnapshot* snap, const EngineParams& params,
                     float* const* out, int numChannels, int numSamples) noexcept;

        // Для UI/таймера (потокобезопасно)
        float displayFrequency() const noexcept { return displayFreq_.load(std::memory_order_relaxed); }
        float signalQuality() const noexcept { return quality_.load(std::memory_order_relaxed); }
        float outputPeak() const noexcept { return outPeak_.load(std::memory_order_relaxed); }   // пик последнего блока
        int   scannerState() const noexcept { return scanState_.load(std::memory_order_relaxed); }
        bool  frequencyOwnedByEngine() const noexcept { return owned_.load(std::memory_order_relaxed); }
        long long slotPosition(int slot) const noexcept { return slots_[(size_t) slot].pos; }

    private:
        static constexpr int kTick = 16;               // период обновления коэффициентов (сэмплы)

        struct Slot
        {
            std::uint64_t id = 0;
            bool usable = false, active = false, done = false;
            PlaybackMode mode = PlaybackMode::Loop;
            int length = 0, period = 0, fade = 0;
            const float* l = nullptr; const float* r = nullptr;   // валидны только в пределах блока
            std::int64_t pos = 0;
            float freq = 0, strength = 0;
            Smoother gain;
            float lastL = 0, lastR = 0, holdL = 0, holdR = 0;
        };

        void setupSlots(const StationSnapshot* snap, bool& anyUsable);
        void readSlot(const Slot& s, float& l, float& r) const noexcept;
        static void advance(Slot& s) noexcept;
        void resetDsp() noexcept;
        void controlTick(const EngineParams& p, float Q, float an) noexcept;

        StationRuntime emptyRuntime_;                  // вместо статика в аудиопотоке
        double fs_ = 44100.0;
        bool started_ = false, firstSample_ = false;
        std::array<Slot, kMaxStations> slots_;
        Scanner scanner_;
        std::atomic<int> pending_ { 0 };

        // управление частотой
        float lastParam_ = 98.f, tuneTarget_ = 98.f, heldFreq_ = 98.f;
        bool scanOwned_ = false;
        double scanAccum_ = 0;

        // сглаживание
        Smoother tuneSm_, selSm_, satSm_, widthSm_, monoSm_, gainSm_, limSm_, anSm_, qSm_, gateSm_, motionSm_;
        float prevTune_ = 98.f;

        // обработка
        Biquad hpL_, hpR_, lpL_, lpR_;
        Saturator satL_, satR_;
        NoiseEngine noiseL_, noiseR_;
        float lfo1_ = 0, lfo2_ = 0, flutterDepth_ = 0;
        int tickCounter_ = 0;
        float holdDecay_ = 0.99f;

        std::atomic<float> displayFreq_ { 98.f }, quality_ { 0.f }, outPeak_ { 0.f };
        std::atomic<int> scanState_ { 0 };
        std::atomic<bool> owned_ { false };
    };
}
