#pragma once
// Чистая логика интерфейса (без JUCE): форматирование, шкала, выбор станции, индикатор уровня.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace freq::ui
{
    inline constexpr float kFreqLo = 87.5f, kFreqHi = 108.0f;

    inline float freqToNorm(float f)
    {
        if (! std::isfinite(f)) return 0.f;
        return std::clamp((f - kFreqLo) / (kFreqHi - kFreqLo), 0.f, 1.f);
    }
    inline float normToFreq(float n)
    {
        if (! std::isfinite(n)) n = 0.f;
        return kFreqLo + std::clamp(n, 0.f, 1.f) * (kFreqHi - kFreqLo);
    }

    inline std::string formatFrequency(float mhz)            // "98.7"
    {
        if (! std::isfinite(mhz)) return "--.-";
        char b[32]; std::snprintf(b, sizeof b, "%.1f", (double) std::clamp(mhz, 0.f, 999.f));
        return b;
    }

    inline std::string formatDuration(double sec)            // "1:15", "--:--" для некорректных
    {
        if (! std::isfinite(sec) || sec < 0) return "--:--";
        const long long s = (long long) std::floor(sec + 0.5);
        char b[32]; std::snprintf(b, sizeof b, "%lld:%02lld", s / 60, s % 60);
        return b;
    }

    // Индекс ближайшей активной станции в пределах maxDist (МГц) или -1.
    inline int nearestStation(const float* freqs, const bool* active, int n, float tune, float maxDist)
    {
        int best = -1; float bd = maxDist;
        for (int i = 0; i < n; ++i)
        {
            if (! active[i]) continue;
            const float d = std::fabs(freqs[i] - tune);
            if (d <= bd) { bd = d; best = i; }
        }
        return best;
    }

    // Пик (линейный) -> 0..1 по шкале -60..0 dBFS (для индикатора).
    inline float meterNorm(float peak)
    {
        if (! std::isfinite(peak) || peak <= 1.0e-6f) return 0.f;
        const float db = 20.f * std::log10(peak);
        return std::clamp((db + 60.f) / 60.f, 0.f, 1.f);
    }
}
