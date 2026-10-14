#pragma once
// Значения пресета (только параметры приёмника: НЕ частота настройки и НЕ станции). Чистый C++.
#include "ParamTable.h"
#include "Engine/RadioEngine.h"
#include <algorithm>
#include <cmath>

namespace freq::preset
{
    struct PresetValues
    {
        float reception = 0.85f, noise = 0.25f, selectivity = 0.5f, tuningSpeed = 0.5f, scanSensitivity = 0.5f;
        float saturation = 0.2f, width = 1.0f, outputGainDb = 0.0f;
        bool analog = true, mono = false, limiterBypass = false;
    };

    inline float clampTo(const char* id, float v)
    {
        const auto* s = param::findFloat(id);
        if (! std::isfinite(v)) v = s ? s->def : 0.f;
        return s ? std::clamp(v, s->min, s->max) : v;
    }

    inline PresetValues clampValues(PresetValues v)
    {
        v.reception = clampTo(param::reception, v.reception);
        v.noise = clampTo(param::noise, v.noise);
        v.selectivity = clampTo(param::selectivity, v.selectivity);
        v.tuningSpeed = clampTo(param::tuningSpeed, v.tuningSpeed);
        v.scanSensitivity = clampTo(param::scanSensitivity, v.scanSensitivity);
        v.saturation = clampTo(param::saturation, v.saturation);
        v.width = clampTo(param::width, v.width);
        v.outputGainDb = clampTo(param::outputGain, v.outputGainDb);
        return v;
    }

    // Совпадение с допуском 1% диапазона (значения хоста квантуются шагом параметра).
    inline bool approxEqual(const PresetValues& a, const PresetValues& b)
    {
        auto near = [](const char* id, float x, float y) {
            const auto* s = param::findFloat(id);
            return std::fabs(x - y) <= 0.01f * (s->max - s->min) + 1.0e-6f;
        };
        return near(param::reception, a.reception, b.reception) && near(param::noise, a.noise, b.noise)
            && near(param::selectivity, a.selectivity, b.selectivity) && near(param::tuningSpeed, a.tuningSpeed, b.tuningSpeed)
            && near(param::scanSensitivity, a.scanSensitivity, b.scanSensitivity) && near(param::saturation, a.saturation, b.saturation)
            && near(param::width, a.width, b.width) && near(param::outputGain, a.outputGainDb, b.outputGainDb)
            && a.analog == b.analog && a.mono == b.mono && a.limiterBypass == b.limiterBypass;
    }

    inline dsp::EngineParams toEngineParams(const PresetValues& v, float frequencyMHz)
    {
        dsp::EngineParams p;
        p.frequency = frequencyMHz; p.reception = v.reception; p.noise = v.noise; p.selectivity = v.selectivity;
        p.tuningSpeed = v.tuningSpeed; p.scanSensitivity = v.scanSensitivity; p.saturation = v.saturation;
        p.width = v.width; p.outputGainDb = v.outputGainDb; p.analog = v.analog; p.mono = v.mono;
        p.limiterBypass = v.limiterBypass;
        return p;
    }
}
