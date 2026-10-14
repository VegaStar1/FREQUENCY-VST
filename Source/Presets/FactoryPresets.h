#pragma once
#include "PresetValues.h"
#include <iterator>

namespace freq::preset
{
    struct FactoryPreset { const char* name; const char* description; PresetValues values; };

    // Порядок полей: reception, noise, selectivity, tuningSpeed, scanSensitivity, saturation, width, outputGainDb,
    //                analog, mono, limiterBypass.  Ни один пресет не отключает ограничитель.
    inline const FactoryPreset kFactoryPresets[] = {
        { "CLEAN FM",
          "Чистый современный переход между станциями, минимум шума.",
          { 0.95f, 0.10f, 0.45f, 0.60f, 0.50f, 0.05f, 1.00f,  0.0f, false, false, false } },
        { "VINTAGE FM",
          "Тёплый FM-приёмник: лёгкая сатурация, слегка суженная база.",
          { 0.75f, 0.30f, 0.50f, 0.50f, 0.50f, 0.30f, 0.80f,  0.0f, true,  false, false } },
        { "DISTANT AM",
          "Узкий далёкий AM-звук: моно, срезанные верха, шорох.",
          { 0.35f, 0.45f, 0.60f, 0.40f, 0.40f, 0.35f, 0.00f,  2.0f, true,  true,  false } },
        { "NIGHT DRIVE",
          "Ночное шоссе: медленная настройка, мягкая нестабильность, тепло.",
          { 0.60f, 0.35f, 0.40f, 0.35f, 0.35f, 0.45f, 0.70f,  0.0f, true,  false, false } },
        { "LOST SIGNAL",
          "Слабый, тонущий в шуме сигнал; станции ловятся с трудом.",
          { 0.20f, 0.75f, 0.80f, 0.25f, 0.80f, 0.50f, 0.40f,  0.0f, true,  false, false } },
        { "TUNING BETWEEN STATIONS",
          "Широкий захват и мягкое перетекание станций друг в друга.",
          { 0.85f, 0.55f, 0.35f, 0.30f, 0.50f, 0.15f, 1.00f,  0.0f, true,  false, false } },
        { "BROKEN RECEIVER",
          "Рассыпающийся приёмник: сильный шум, искажения, узкая база.",
          { 0.10f, 0.85f, 0.70f, 0.80f, 0.60f, 0.85f, 0.20f, -3.0f, true,  false, false } },
        { "CINEMATIC SEARCH",
          "Кинематографичный поиск: заметный шум, выразительные переходы.",
          { 0.70f, 0.50f, 0.65f, 0.45f, 0.70f, 0.20f, 1.00f,  0.0f, true,  false, false } },
        { "HIP-HOP RADIO INTRO",
          "Быстрые переключения станций, зернистость, плотный звук для интро.",
          { 0.50f, 0.35f, 0.55f, 0.90f, 0.50f, 0.55f, 0.60f,  1.0f, true,  false, false } },
        { "MINIMAL INTERFERENCE",
          "Почти прозрачный приём с едва заметным характером приёмника.",
          { 0.98f, 0.06f, 0.40f, 0.70f, 0.40f, 0.08f, 1.00f,  0.0f, true,  false, false } },
    };
    inline constexpr int kNumFactoryPresets = 10;
    static_assert(std::size(kFactoryPresets) == (size_t) kNumFactoryPresets, "factory preset count");
}
