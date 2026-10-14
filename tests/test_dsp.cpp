// Юнит-тесты DSP-ядра. Без внешних зависимостей: компилируется любым C++17-компилятором.
#include "Dsp/Biquad.h"
#include "Dsp/NoiseEngine.h"
#include "Dsp/Rng.h"
#include "Dsp/Saturation.h"
#include "Dsp/Scanner.h"
#include "Dsp/TuningModel.h"
#include <cmath>
#include <cstdio>
#include <vector>

using namespace freq::dsp;

static int g_failed = 0, g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failed; \
    std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define RUN(fn) do { int before = g_failed; fn(); std::printf("[%s] %s\n", g_failed == before ? " ok " : "FAIL", #fn); } while (0)

static constexpr double kPi = 3.14159265358979323846;

// Усиление фильтра на частоте f (амплитуда на установившемся режиме).
static double gainAt(Biquad& bq, double fs, double f)
{
    bq.reset();
    const int n = (int) fs;
    double peak = 0;
    for (int i = 0; i < n; ++i)
    {
        const float y = bq.process((float) std::sin(2 * kPi * f * i / fs));
        if (i > n / 2) peak = std::max(peak, (double) std::fabs(y));
    }
    return peak;
}
static double db(double x) { return 20 * std::log10(std::max(x, 1e-12)); }

void test_rng()
{
    Rng a(123), b(123);
    bool same = true, inRange = true;
    for (int i = 0; i < 10000; ++i)
    {
        const float x = a.nextBipolar(), y = b.nextBipolar();
        same &= (x == y);
        inRange &= (x >= -1.f && x < 1.f);
    }
    CHECK(same); CHECK(inRange);
    Rng z(0);  // нулевой seed не должен «залипать»
    CHECK(z.nextU32() != 0);
}

void test_biquad_response()
{
    for (double fs : { 8000.0, 44100.0, 96000.0 })
    {
        Biquad lp, hp, bp;
        lp.setCoeffs(BiquadCoeffs::lowpass(fs, 1000, 0.707));
        hp.setCoeffs(BiquadCoeffs::highpass(fs, 1000, 0.707));
        bp.setCoeffs(BiquadCoeffs::bandpass(fs, 1000, 2.0));
        CHECK(std::fabs(db(gainAt(lp, fs, 100))) < 0.5);
        CHECK(db(gainAt(lp, fs, std::min(10000.0, 0.4 * fs))) < -20);
        CHECK(std::fabs(db(gainAt(hp, fs, std::min(8000.0, 0.4 * fs)))) < 1.0);
        CHECK(db(gainAt(hp, fs, 100)) < -15);
        CHECK(std::fabs(db(gainAt(bp, fs, 1000))) < 0.5);
        CHECK(db(gainAt(bp, fs, 100)) < -10);
    }
}

void test_biquad_stability_extremes()
{
    const double fs = 44100;
    bool finite = true;
    for (double fc : { -5.0, 0.0, 1.0, 20.0, 20000.0, 22050.0, 1e9 })
        for (double q : { -1.0, 0.0, 0.5, 50.0 })
        {
            Biquad b; Rng r(7);
            b.setCoeffs(BiquadCoeffs::lowpass(fs, fc, q));
            for (int i = 0; i < 20000; ++i)
                finite &= std::isfinite(b.process(r.nextBipolar()));
        }
    CHECK(finite);
}

void test_smoother()
{
    Smoother s; s.prepare(48000, 0.01); s.setImmediate(0); s.setTarget(1);
    float prev = 0; bool monotone = true; float maxStep = 0;
    for (int i = 0; i < 48000; ++i)
    {
        const float v = s.next();
        monotone &= (v >= prev); maxStep = std::max(maxStep, v - prev); prev = v;
    }
    CHECK(monotone); CHECK(maxStep < 0.01f); CHECK(std::fabs(prev - 1.f) < 1e-3f);
}

void test_saturator()
{
    Saturator s; s.prepare(44100);
    CHECK(s.process(0.37f, 0.f) == 0.37f);                     // 0 = точный проход

    bool bounded = true; double sum = 0; int n = 44100 * 2;
    for (int i = 0; i < n; ++i)
    {
        const float y = s.process(0.9f * (float) std::sin(2 * kPi * 220 * i / 44100.0), 1.f);
        bounded &= std::fabs(y) <= 1.2f; sum += y;
    }
    CHECK(bounded);
    CHECK(std::fabs(sum / n) < 0.01);                          // нет DC-смещения

    // Постоянное смещение на входе должно срезаться блокировщиком
    Saturator d; d.prepare(44100); float last = 0;
    for (int i = 0; i < 44100 * 3; ++i) last = d.process(0.5f, 0.5f);
    CHECK(std::fabs(last) < 0.01f);
}

void test_soft_ceiling()
{
    bool below = true, ident = true, bounded = true;
    for (float x = -20; x <= 20; x += 0.01f)
    {
        const float y = softCeiling(x, 0.8f, 0.98f);
        bounded &= std::fabs(y) <= 0.98f;
        if (std::fabs(x) <= 0.8f) ident &= (y == x);
        below &= std::isfinite(y);
    }
    CHECK(bounded); CHECK(ident); CHECK(below);
    // Монотонность и непрерывность на пороге
    float prev = softCeiling(-5, 0.8f, 0.98f); bool mono = true, smooth = true;
    for (float x = -5; x <= 5; x += 0.001f)
    {
        const float y = softCeiling(x, 0.8f, 0.98f);
        mono &= (y >= prev); smooth &= (std::fabs(y - prev) < 0.002f); prev = y;
    }
    CHECK(mono); CHECK(smooth);
}

static double noiseRms(float knob, float reception, bool analog, float* peakOut = nullptr, bool* finiteOut = nullptr)
{
    NoiseEngine ne; ne.prepare(44100, 42);
    ne.setLevels(mapNoiseLevels(knob, reception, analog));
    double acc = 0; float peak = 0; bool fin = true; int n = 44100 * 10, cnt = 0;
    for (int i = 0; i < n; ++i)
    {
        const float v = ne.next(); fin &= std::isfinite(v);
        if (i > 44100 / 2) { acc += (double) v * v; ++cnt; peak = std::max(peak, std::fabs(v)); }
    }
    if (peakOut) *peakOut = peak;
    if (finiteOut) *finiteOut = fin;
    return std::sqrt(acc / cnt);
}

void test_noise_levels()
{
    float peak = 0; bool fin = false;
    CHECK(noiseRms(0.f, 0.f, true) < 1e-6);                    // knob = 0 -> тишина
    const double good = noiseRms(0.5f, 1.0f, true);
    const double mid  = noiseRms(0.5f, 0.5f, true);
    const double bad  = noiseRms(0.5f, 0.0f, true, &peak, &fin);
    std::printf("    rms: good=%.5f mid=%.5f bad=%.5f peak=%.3f\n", good, mid, bad, peak);
    CHECK(good < mid); CHECK(mid < bad);                       // хуже приём -> больше шума
    CHECK(fin); CHECK(peak <= NoiseEngine::kMaxOut);
    CHECK(noiseRms(1.f, 1.0f, true) < 0.01);                   // сильный приём: шум почти неслышен
    CHECK(noiseRms(1.f, 0.f, false) < 0.3 * noiseRms(1.f, 0.f, true)); // CLEAN тише ANALOG
    CHECK(noiseRms(1.f, 0.f, true) < 0.5);
}

void test_noise_deterministic_and_smooth_changes()
{
    NoiseEngine a, b; a.prepare(48000, 5); b.prepare(48000, 5);
    a.setLevels(mapNoiseLevels(0.7f, 0.2f, true)); b.setLevels(mapNoiseLevels(0.7f, 0.2f, true));
    bool same = true;
    for (int i = 0; i < 50000; ++i) same &= (a.next() == b.next());
    CHECK(same);

    // Резкая смена уровня: нет мгновенного скачка выше потолка и всё конечно.
    NoiseEngine n; n.prepare(48000, 9); n.setLevels(mapNoiseLevels(0.f, 1.f, true));
    bool fin = true; float peak = 0;
    for (int i = 0; i < 96000; ++i)
    {
        if (i % 4800 == 0) n.setLevels(mapNoiseLevels((i / 4800) % 2 ? 1.f : 0.f, (i / 4800) % 3 ? 0.f : 1.f, true));
        const float v = n.next(); fin &= std::isfinite(v); peak = std::max(peak, std::fabs(v));
    }
    CHECK(fin); CHECK(peak <= NoiseEngine::kMaxOut);
}

void test_tuning_strength()
{
    CHECK(std::fabs(stationStrength(98.f, 98.f, 0.5f, 0.8f) - 0.8f) < 1e-6f);
    float prev = 1.f; bool mono = true;
    for (float d = 0; d < 3.f; d += 0.01f)
    { const float s = stationStrength(98.f + d, 98.f, 0.5f, 1.f); mono &= (s <= prev + 1e-7f); prev = s; }
    CHECK(mono);
    CHECK(stationStrength(98.f, 99.f, 1.f, 1.f) < stationStrength(98.f, 99.f, 0.f, 1.f)); // выше селективность -> уже
    CHECK(stationStrength(98.f, 98.f, 0.5f, 0.f) == 0.f);
}

void test_equal_power_and_continuity()
{
    // При сумме сил = 1 суммарная мощность постоянна (a^2 + b^2 = 1).
    bool constPower = true;
    for (float t = 0; t <= 1.f; t += 0.01f)
    { const auto w = equalPower(1.f - t, t); constPower &= std::fabs(w.a * w.a + w.b * w.b - 1.f) < 1e-4f; }
    CHECK(constPower);
    const auto zero = equalPower(0.f, 0.f);
    CHECK(zero.a == 0.f && zero.b == 0.f);

    // Развёртка настройки между двумя станциями: веса непрерывны, общий уровень не превышает 1.
    const float fa = 95.f, fb = 97.f; float maxJump = 0, maxLevel = 0;
    auto w0 = equalPower(stationStrength(94.f, fa, 0.3f, 1.f), stationStrength(94.f, fb, 0.3f, 1.f));
    float pa = w0.a, pb = w0.b;
    for (float f = 94.f; f <= 98.f; f += 0.002f)
    {
        const auto w = equalPower(stationStrength(f, fa, 0.3f, 1.f), stationStrength(f, fb, 0.3f, 1.f));
        maxJump = std::max({ maxJump, std::fabs(w.a - pa), std::fabs(w.b - pb) });
        maxLevel = std::max(maxLevel, std::sqrt(w.a * w.a + w.b * w.b));
        pa = w.a; pb = w.b;
    }
    std::printf("    sweep: maxJump=%.4f maxLevel=%.4f\n", maxJump, maxLevel);
    CHECK(maxLevel <= 1.0001f);
    CHECK(maxJump < 0.05f);
}

static Scanner makeScanner()
{
    Scanner sc; StationPoint pts[2] = { { 90.f, 1.f, true }, { 100.f, 1.f, true } };
    sc.setStations(pts, 2); sc.setRange(87.5f, 108.f); sc.setParams(0.5f, 0.5f, 0.5f);
    return sc;
}

static float runUntilIdle(Scanner& sc, float f, float dt, float maxSec, float* time = nullptr)
{
    float t = 0;
    while (t < maxSec && sc.state() != Scanner::State::Idle) { f = sc.step(f, dt); t += dt; }
    if (time) *time = t;
    return f;
}

void test_scanner_finds_station()
{
    auto sc = makeScanner(); sc.startSearch(88.f);
    const float f = runUntilIdle(sc, 88.f, 0.005f, 30.f);
    CHECK(sc.state() == Scanner::State::Idle);
    CHECK(std::fabs(f - 90.f) < 0.002f);                        // остановился точно на станции
    CHECK(sc.lastStationIndex() == 0);

    // следующий SEARCH со станции не останавливается на ней же, а находит следующую
    sc.startSearch(f);
    const float f2 = runUntilIdle(sc, f, 0.005f, 30.f);
    CHECK(std::fabs(f2 - 100.f) < 0.002f);
    CHECK(sc.lastStationIndex() == 1);

    // продолжение вверх: оборачивается и находит первую снова
    sc.startSearch(f2);
    const float f3 = runUntilIdle(sc, f2, 0.005f, 60.f);
    CHECK(std::fabs(f3 - 90.f) < 0.002f);
}

void test_scanner_deterministic_and_block_sizes()
{
    auto a = makeScanner(), b = makeScanner(); a.startSearch(88.f); b.startSearch(88.f);
    float fa = 88.f, fb = 88.f; bool same = true;
    for (int i = 0; i < 4000; ++i) { fa = a.step(fa, 0.005f); fb = b.step(fb, 0.005f); same &= (fa == fb); }
    CHECK(same);

    // Крупные блоки (~11 мс при 512/44.1k) тоже не перепрыгивают станцию
    auto c = makeScanner(); c.setParams(1.0f, 0.5f, 1.0f); c.startSearch(88.f);   // макс. скорость, макс. селективность
    const float fc = runUntilIdle(c, 88.f, 512.f / 44100.f, 30.f);
    CHECK(std::fabs(fc - 90.f) < 0.002f);
}

void test_scanner_modes()
{
    // SCAN: непрерывный, не останавливается, остаётся в диапазоне, обходит всю шкалу
    auto sc = makeScanner(); sc.startScan(88.f);
    float f = 88.f; bool inRange = true, wrapped = false, pass90 = false, pass100 = false;
    for (int i = 0; i < 20000; ++i)
    {
        const float prev = f; f = sc.step(f, 0.005f);
        inRange &= (f >= 87.5f && f <= 108.f); wrapped |= (f < prev - 5.f);
        pass90 |= std::fabs(f - 90.f) < 0.05f; pass100 |= std::fabs(f - 100.f) < 0.05f;
    }
    CHECK(sc.state() == Scanner::State::Scanning); CHECK(inRange); CHECK(wrapped); CHECK(pass90 && pass100);

    // Без станций SEARCH не останавливается и оборачивается
    Scanner empty; empty.setRange(87.5f, 108.f); empty.setParams(0.5f, 0.5f, 0.5f); empty.startSearch(100.f);
    f = 100.f; wrapped = false; inRange = true;
    for (int i = 0; i < 6000; ++i)
    { const float p = f; f = empty.step(f, 0.005f); wrapped |= f < p - 5.f; inRange &= (f >= 87.5f && f <= 108.f); }
    CHECK(empty.state() == Scanner::State::Searching); CHECK(wrapped); CHECK(inRange);

    // LOCK удерживает частоту; STOP прерывает поиск; отключённая станция игнорируется
    auto lk = makeScanner(); lk.startSearch(88.f); lk.setLocked(true);
    CHECK(lk.step(88.f, 0.01f) == 88.f); CHECK(lk.state() == Scanner::State::Locked);
    lk.stop(); CHECK(lk.state() == Scanner::State::Locked);   // STOP не снимает LOCK
    lk.setLocked(false); CHECK(lk.state() == Scanner::State::Idle);

    auto st = makeScanner(); st.startSearch(88.f); st.step(88.f, 0.01f); st.stop();
    CHECK(st.state() == Scanner::State::Idle);

    Scanner dis; StationPoint p[2] = { { 90.f, 1.f, false }, { 100.f, 1.f, true } };
    dis.setStations(p, 2); dis.setRange(87.5f, 108.f); dis.setParams(0.5f, 0.5f, 0.5f); dis.startSearch(88.f);
    const float fd = runUntilIdle(dis, 88.f, 0.005f, 30.f);
    CHECK(std::fabs(fd - 100.f) < 0.002f);

    // Чувствительность влияет на порог остановки
    Scanner lo = makeScanner(), hi = makeScanner();
    lo.setParams(0.5f, 0.0f, 0.5f); hi.setParams(0.5f, 1.0f, 0.5f);
    CHECK(hi.stopThr() < lo.stopThr());
}

int main()
{
    RUN(test_rng);
    RUN(test_biquad_response);
    RUN(test_biquad_stability_extremes);
    RUN(test_smoother);
    RUN(test_saturator);
    RUN(test_soft_ceiling);
    RUN(test_noise_levels);
    RUN(test_noise_deterministic_and_smooth_changes);
    RUN(test_tuning_strength);
    RUN(test_equal_power_and_continuity);
    RUN(test_scanner_finds_station);
    RUN(test_scanner_deterministic_and_block_sizes);
    RUN(test_scanner_modes);
    std::printf("\n%d checks, %d failed\n", g_checks, g_failed);
    return g_failed == 0 ? 0 : 1;
}
