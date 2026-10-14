#pragma once
// Единый источник правды по параметрам (чистый C++, без JUCE): ID, диапазоны, значения по умолчанию.
// По этой таблице строится layout хоста (Parameters.h), валидируются пресеты и тесты.
#include <cstring>

namespace freq::param
{
    // Стабильные ID. НИКОГДА не переименовывать после релиза: на них держится автоматизация в DAW.
    inline constexpr const char* frequency       = "frequency";
    inline constexpr const char* reception       = "reception";
    inline constexpr const char* noise           = "noise";
    inline constexpr const char* selectivity     = "selectivity";
    inline constexpr const char* tuningSpeed     = "tuningSpeed";
    inline constexpr const char* scanSensitivity = "scanSensitivity";
    inline constexpr const char* saturation      = "saturation";
    inline constexpr const char* width           = "width";
    inline constexpr const char* outputGain      = "outputGain";
    inline constexpr const char* mode            = "mode";          // 0 = ANALOG, 1 = CLEAN
    inline constexpr const char* mono            = "mono";
    inline constexpr const char* limiterBypass   = "limiterBypass";
    inline constexpr const char* station         = "station";       // 0 = нет, 1..16

    inline constexpr float kFreqMin = 87.5f;
    inline constexpr float kFreqMax = 108.0f;
    inline constexpr int   kMaxStations = 16;

    struct FloatSpec { const char* id; const char* name; float min, max, step, def; const char* label; };

    inline constexpr FloatSpec kFloatSpecs[] = {
        { frequency,       "Frequency",        kFreqMin, kFreqMax, 0.01f,  98.0f, ""   },
        { reception,       "Reception",        0.f,      1.f,      0.001f, 0.85f, ""   },
        { noise,           "Noise",            0.f,      1.f,      0.001f, 0.25f, ""   },
        { selectivity,     "Selectivity",      0.f,      1.f,      0.001f, 0.5f,  ""   },
        { tuningSpeed,     "Tuning Speed",     0.f,      1.f,      0.001f, 0.5f,  ""   },
        { scanSensitivity, "Scan Sensitivity", 0.f,      1.f,      0.001f, 0.5f,  ""   },
        { saturation,      "Saturation",       0.f,      1.f,      0.001f, 0.2f,  ""   },
        { width,           "Stereo Width",     0.f,      1.f,      0.001f, 1.0f,  ""   },
        { outputGain,      "Output Gain",      -24.f,    12.f,     0.1f,   0.0f,  "dB" },
    };

    inline const FloatSpec* findFloat(const char* id)
    {
        for (const auto& s : kFloatSpecs)
            if (std::strcmp(s.id, id) == 0) return &s;
        return nullptr;
    }
}
