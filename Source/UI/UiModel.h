#pragma once
// Чистая логика интерфейса (без JUCE): форматирование, шкала, выбор станции, индикатор уровня.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cctype>
#include <string>
#include <utility>
#include <vector>

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

    // Допустимые для перетаскивания расширения (в соответствии с ТЗ: WAV и AIFF).
    inline bool isSupportedAudioExtension(const std::string& ext)
    {
        std::string e;
        for (unsigned char c : ext) if (c != '.') e += (char) std::tolower(c);
        return e == "wav" || e == "aif" || e == "aiff";
    }

    // Раскладка брошенных файлов по слотам. Файл, брошенный на слот (даже занятый), заменяет его;
    // остальные заполняют свободные слоты по возрастанию номера. Возвращает число файлов, которым не хватило места.
    inline int planDrop(int numFiles, int targetSlot, const bool* occupied, int numSlots,
                        std::vector<std::pair<int, int>>& plan)   // (слот, индекс файла)
    {
        plan.clear();
        if (numFiles <= 0) return 0;
        if (numSlots <= 0 || occupied == nullptr) return numFiles;
        int next = 0;
        if (targetSlot >= 0 && targetSlot < numSlots) plan.emplace_back(targetSlot, next++);
        for (int s = 0; s < numSlots && next < numFiles; ++s)
        {
            if (s == targetSlot || occupied[s]) continue;
            plan.emplace_back(s, next++);
        }
        return numFiles - next;
    }
}
