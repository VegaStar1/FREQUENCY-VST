#include "UI/UiModel.h"
#include <cstdio>
#include <limits>
#include <utility>
#include <vector>
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
    // перетаскивание файлов: допустимые расширения
    CHECK(isSupportedAudioExtension(".wav") && isSupportedAudioExtension("WAV") && isSupportedAudioExtension(".Aif") && isSupportedAudioExtension("aiff"));
    CHECK(! isSupportedAudioExtension(".mp3") && ! isSupportedAudioExtension("") && ! isSupportedAudioExtension(".wavx") && ! isSupportedAudioExtension("txt"));
    // раскладка: на пустой слот
    {
        bool occ[16] = {}; occ[0] = occ[2] = true; std::vector<std::pair<int,int>> plan;
        CHECK(planDrop(3, 5, occ, 16, plan) == 0);                          // 1-й файл -> слот 5, остальные -> 1, 3
        CHECK(plan.size() == 3 && plan[0] == std::make_pair(5, 0) && plan[1] == std::make_pair(1, 1) && plan[2] == std::make_pair(3, 2));
        // на занятый слот: заменяет его
        CHECK(planDrop(1, 2, occ, 16, plan) == 0 && plan.size() == 1 && plan[0] == std::make_pair(2, 0));
        // мимо слотов (-1): только свободные
        CHECK(planDrop(2, -1, occ, 16, plan) == 0 && plan[0] == std::make_pair(1, 0) && plan[1] == std::make_pair(3, 1));
        // нет места: лишние отбрасываются и считаются
        bool full[16]; for (bool& b : full) b = true;
        CHECK(planDrop(4, -1, full, 16, plan) == 4 && plan.empty());
        CHECK(planDrop(4, 7, full, 16, plan) == 3 && plan.size() == 1 && plan[0].first == 7);
        // слот вне диапазона трактуется как «мимо»; мусорные входы
        CHECK(planDrop(1, 99, occ, 16, plan) == 0 && plan[0].first == 1);
        CHECK(planDrop(0, 3, occ, 16, plan) == 0 && plan.empty());
        CHECK(planDrop(2, 3, nullptr, 16, plan) == 2 && plan.empty());
        CHECK(planDrop(2, 0, occ, 0, plan) == 2);
        // 20 файлов на 16 слотов: каждая позиция используется один раз
        bool none[16] = {}; CHECK(planDrop(20, -1, none, 16, plan) == 4 && plan.size() == 16);
        bool used[16] = {}; bool uniq = true; for (auto& pr : plan) { uniq &= ! used[pr.first]; used[pr.first] = true; } CHECK(uniq);
    }
    std::printf("%d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
