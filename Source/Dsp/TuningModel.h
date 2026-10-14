#pragma once
#include <cmath>
#include <algorithm>

// Чистый C++ без зависимости от JUCE: легко тестируется отдельно.
namespace freq::dsp
{
    // Сила приёма станции в точке настройки. Гауссов профиль: максимум в центре, плавно -> 0.
    // selectivity 0..1: 0 = широкий захват (~1.5 МГц), 1 = узкий (~0.15 МГц).
    inline float selectivityBandwidth(float selectivity)
    {
        return 1.5f * std::pow(0.1f, std::clamp(selectivity, 0.f, 1.f));
    }

    inline float stationStrength(float tuneMHz, float stationMHz, float selectivity, float stationStrength01)
    {
        const float bandwidth = selectivityBandwidth(selectivity);
        const float d = (tuneMHz - stationMHz) / bandwidth;
        return std::clamp(stationStrength01, 0.f, 1.f) * std::exp(-d * d);
    }

    // Равномерно-мощностные веса для двух источников.
    struct XfadeWeights { float a, b; };
    inline XfadeWeights equalPower(float strengthA, float strengthB)
    {
        const float sum = strengthA + strengthB;
        if (sum <= 1.0e-9f) return { 0.f, 0.f };
        const float t = strengthB / sum;                     // позиция между A и B
        const float level = std::min(1.f, sum);              // общий уровень падает при уходе от обеих станций
        constexpr float halfPi = 1.57079632679f;
        return { std::cos(t * halfPi) * level, std::sin(t * halfPi) * level };
    }
}
