#include "UI/UiModel.h"
#include <cstdio>
#include <limits>
using namespace freq::ui;
static int g_failed = 0, g_checks = 0;
#define CHECK(c) do { ++g_checks; if (!(c)) { ++g_failed; std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

int main()
{
    const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
    // шкала: границы, круговая проверка, мусор
    CHECK(freqToNorm(87.5f) == 0.f && freqToNorm(108.f) == 1.f);
    CHECK(freqToNorm(50.f) == 0.f && freqToNorm(500.f) == 1.f && freqToNorm(nan) == 0.f);
    for (float f = 87.5f; f <= 108.f; f += 0.37f) CHECK(std::fabs(normToFreq(freqToNorm(f)) - f) < 1e-3f);
    CHECK(normToFreq(-1.f) == 87.5f && normToFreq(2.f) == 108.f && normToFreq(nan) == 87.5f);
    // форматирование
    CHECK(formatFrequency(98.7f) == "98.7"); CHECK(formatFrequency(87.5f) == "87.5"); CHECK(formatFrequency(108.f) == "108.0");
    CHECK(formatFrequency(nan) == "--.-"); CHECK(formatFrequency(inf) == "--.-");
    CHECK(formatDuration(0) == "0:00"); CHECK(formatDuration(75.2) == "1:15"); CHECK(formatDuration(3599.6) == "60:00");
    CHECK(formatDuration(-1) == "--:--"); CHECK(formatDuration(std::numeric_limits<double>::infinity()) == "--:--");
    // ближайшая станция
    const float fr[3] = { 90.f, 95.f, 95.2f }; const bool act[3] = { true, true, false };
    CHECK(nearestStation(fr, act, 3, 95.1f, 0.35f) == 1);          // неактивная 95.2 не учитывается
    CHECK(nearestStation(fr, act, 3, 92.5f, 0.35f) == -1);         // далеко
    CHECK(nearestStation(fr, act, 3, 90.3f, 0.35f) == 0);
    CHECK(nearestStation(fr, act, 0, 90.f, 1.f) == -1);
    // индикатор
    CHECK(meterNorm(1.f) == 1.f); CHECK(meterNorm(0.f) == 0.f); CHECK(meterNorm(nan) == 0.f);
    CHECK(std::fabs(meterNorm(0.5f) - (1.f - 6.02f / 60.f)) < 1e-3f);
    CHECK(meterNorm(1e-4f) == 0.f); CHECK(meterNorm(10.f) == 1.f);
    std::printf("%d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
