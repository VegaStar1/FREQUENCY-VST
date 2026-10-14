#pragma once
#include "StationBuffer.h"

namespace freq
{
    struct PrepareOptions
    {
        double targetRate   = 44100.0;   // частота дискретизации движка
        float  targetRmsDb  = -18.0f;    // целевой RMS после нормализации
        float  maxPeak      = 0.95f;     // потолок пика после нормализации
        float  maxGainDb    = 24.0f;     // не усиливать тихие файлы больше чем на это
        double maxSeconds   = 300.0;     // длиннее - обрезается (ограничение памяти)
    };

    // Подготовка файла: НЕ для аудиопотока (аллокации, долгие циклы).
    //  1) NaN/Inf -> 0, обрезка до maxSeconds
    //  2) удаление DC по каналам
    //  3) ресемплинг оконным sinc (Blackman, 16 лепестков; антиалиасинг при понижении частоты)
    //  4) нормализация по RMS с ограничением по пику (одно постоянное усиление: без скачков громкости)
    // Возвращает nullptr для некорректных данных (нет каналов/сэмплов, плохая частота).
    std::shared_ptr<StationBuffer> prepareStation(const float* const* channels, int numChannels,
                                                  long long numSamples, double srcRate,
                                                  const PrepareOptions& opt);

    // Экспортировано для тестов
    std::vector<float> resampleSinc(const std::vector<float>& in, double srcRate, double dstRate);
}
