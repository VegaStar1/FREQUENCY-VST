// Интеграционные тесты аудиодвижка: рендер реального аудио через всю цепочку RadioEngine.
#include "Dsp/Rng.h"
#include "Engine/RadioEngine.h"
#include "Stations/StationPrep.h"
#include <cmath>
#include <cstdio>
#include <functional>
#include <limits>
#include <atomic>
#include <cstdlib>
#include <new>
#include <vector>

// Счётчик выделений памяти: проверяем, что process() не аллоцирует.
static std::atomic<long> g_allocs { 0 };
static std::atomic<bool> g_counting { false };
void* operator new(std::size_t n) { if (g_counting) ++g_allocs; if (void* p = std::malloc(n ? n : 1)) return p; throw std::bad_alloc(); }
void* operator new[](std::size_t n) { if (g_counting) ++g_allocs; if (void* p = std::malloc(n ? n : 1)) return p; throw std::bad_alloc(); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

using namespace freq;
using namespace freq::dsp;

static int g_failed = 0, g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failed; \
    std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define RUN(fn) do { int b = g_failed; fn(); std::printf("[%s] %s\n", g_failed == b ? " ok " : "FAIL", #fn); } while (0)
static constexpr double kPi = 3.14159265358979323846;

// ---------- помощники ----------
using Vec = std::vector<float>;

static Vec sineV(double f, double sr, double sec, float amp = 0.5f, double ph = 0)
{
    Vec v((size_t) (sr * sec));
    for (size_t i = 0; i < v.size(); ++i) v[i] = amp * (float) std::sin(2 * kPi * f * (double) i / sr + ph);
    return v;
}

struct TS   // описание тестовой станции
{
    int slot = 0; float freq = 98.f; Vec L, R; double sr = 44100.0;
    PlaybackMode mode = PlaybackMode::Loop; float strength = 1.f, fadeMs = 20.f; bool enabled = true;
};

static std::shared_ptr<StationSnapshot> makeSnap(const std::vector<TS>& list, double engineRate,
                                                 std::uint64_t serial = 1)
{
    auto s = std::make_shared<StationSnapshot>(); s->serial = serial;
    for (const auto& t : list)
    {
        const float* ch[2] = { t.L.data(), t.R.empty() ? nullptr : t.R.data() };
        PrepareOptions o; o.targetRate = engineRate;
        auto b = prepareStation(ch, t.R.empty() ? 1 : 2, (long long) t.L.size(), t.sr, o);
        auto& r = s->slots[(size_t) t.slot];
        r.buffer = b; r.freqMHz = t.freq; r.strength = t.strength; r.mode = t.mode;
        r.loopFadeMs = t.fadeMs; r.enabled = t.enabled;
    }
    return s;
}

struct Out { Vec L, R; };

static EngineParams baseParams(float freq)
{
    EngineParams p; p.frequency = freq; p.reception = 1.f; p.noise = 0.25f; p.selectivity = 0.5f;
    p.saturation = 0.f; p.width = 1.f; p.tuningSpeed = 1.f; return p;
}

using Mod = std::function<void(int, EngineParams&)>;
static Out render(RadioEngine& e, const StationSnapshot* snap, EngineParams p, double sec, int block,
                  double sr, int channels = 2, const Mod& mod = nullptr)
{
    Out o; const int total = (int) (sec * sr);
    Vec bl((size_t) block), br((size_t) block);
    for (int pos = 0, bi = 0; pos < total; pos += block, ++bi)
    {
        const int n = std::min(block, total - pos);
        if (mod) mod(bi, p);
        float* ptrs[2] = { bl.data(), br.data() };
        e.process(snap, p, ptrs, channels, n);
        o.L.insert(o.L.end(), bl.begin(), bl.begin() + n);
        o.R.insert(o.R.end(), (channels > 1 ? br : bl).begin(), (channels > 1 ? br : bl).begin() + n);
    }
    return o;
}

static double rms(const Vec& x, size_t a, size_t b)
{
    b = std::min(b, x.size()); if (b <= a) return 0;
    double s = 0; for (size_t i = a; i < b; ++i) s += (double) x[i] * x[i];
    return std::sqrt(s / (double) (b - a));
}
static double peakAbs(const Vec& x, size_t a = 0) { double p = 0; for (size_t i = a; i < x.size(); ++i) p = std::max(p, (double) std::fabs(x[i])); return p; }
static double maxJump(const Vec& x, size_t a = 1, size_t b = (size_t) -1)
{
    b = std::min(b, x.size()); double m = 0;
    for (size_t i = std::max<size_t>(a, 1); i < b; ++i) m = std::max(m, (double) std::fabs(x[i] - x[i - 1]));
    return m;
}
static bool allFinite(const Vec& x) { for (float v : x) if (! std::isfinite(v)) return false; return true; }
static double mean(const Vec& x, size_t a) { double s = 0; for (size_t i = a; i < x.size(); ++i) s += x[i]; return s / (double) (x.size() - a); }
static double goertzel(const Vec& x, size_t a, size_t b, double f, double sr)   // амплитуда синусоиды частоты f
{
    double re = 0, im = 0; b = std::min(b, x.size());
    for (size_t i = a; i < b; ++i) { const double w = 2 * kPi * f * (double) (i - a) / sr; re += x[i] * std::cos(w); im -= x[i] * std::sin(w); }
    return 2.0 * std::sqrt(re * re + im * im) / (double) (b - a);
}
static double dB(double x) { return 20 * std::log10(std::max(x, 1e-12)); }

static const double SR = 44100.0;

// ---------- подготовка станций ----------
void test_prepare_station()
{
    // ресемплинг сохраняет высоту тона
    const Vec t48 = sineV(1000.0, 48000.0, 1.0, 0.5f);
    const float* ch[1] = { t48.data() };
    PrepareOptions o; o.targetRate = 44100.0;
    auto b = prepareStation(ch, 1, (long long) t48.size(), 48000.0, o);
    CHECK(b != nullptr);
    CHECK(std::abs(b->length - 44100) <= 1);
    CHECK(std::fabs(goertzel(b->ch[0], 4000, 40000, 1000.0, 44100.0) - 0.0) > 0.1);   // тон на 1 кГц есть
    CHECK(goertzel(b->ch[0], 4000, 40000, 1100.0, 44100.0) < 0.02);                  // и это не 1.1 кГц

    // нормализация: RMS ~ -18 dBFS, пик <= 0.95, для тихого и громкого файла
    for (float amp : { 0.03f, 0.5f, 1.0f })
    {
        Vec v = sineV(440, 44100, 1.0, amp); const float* c[1] = { v.data() };
        auto nb = prepareStation(c, 1, (long long) v.size(), 44100.0, o);
        double r = rms(nb->ch[0], 0, nb->ch[0].size());
        CHECK(std::fabs(dB(r) - (-18.0)) < 0.3);
        CHECK(peakAbs(nb->ch[0]) <= 0.951);
    }
    {   // очень тихий файл: усиление ограничено maxGainDb (не раздуваем почти тишину)
        Vec q = sineV(440, 44100, 1.0, 0.005f); const float* c[1] = { q.data() };
        auto nb = prepareStation(c, 1, (long long) q.size(), 44100.0, o);
        CHECK(nb->normalizeGain <= 15.9f);                                  // 24 дБ
    }
    // понижение частоты: алиасинг подавлен (96 кГц тон 30 кГц -> 44.1 кГц)
    Vec hf = sineV(30000.0, 96000.0, 0.5, 0.5f); const float* ch2[1] = { hf.data() };
    auto bh = prepareStation(ch2, 1, (long long) hf.size(), 96000.0, o);
    CHECK(bh != nullptr);
    {   // тон 30 кГц выше Найквиста 22 кГц: после ресемплинга почти ничего не должно остаться (до нормализации)
        Vec raw = resampleSinc(hf, 96000.0, 44100.0);
        CHECK(rms(raw, 2000, raw.size() - 2000) < 0.01);
    }
    // мусор и NaN
    Vec bad = sineV(440, 44100, 0.2, 0.5f);
    bad[100] = std::numeric_limits<float>::quiet_NaN(); bad[200] = std::numeric_limits<float>::infinity();
    const float* cb[1] = { bad.data() };
    auto nb = prepareStation(cb, 1, (long long) bad.size(), 44100.0, o);
    CHECK(nb != nullptr && allFinite(nb->ch[0]));
    CHECK(prepareStation(cb, 1, 0, 44100.0, o) == nullptr);                 // пустой
    CHECK(prepareStation(nullptr, 1, 100, 44100.0, o) == nullptr);
    CHECK(prepareStation(cb, 0, 100, 44100.0, o) == nullptr);
    CHECK(prepareStation(cb, 1, 100, 0.0, o) == nullptr);                   // плохая частота
    CHECK(prepareStation(cb, 1, 100, std::numeric_limits<double>::quiet_NaN(), o) == nullptr);
    // очень короткий файл (1 сэмпл) и тишина
    float one = 0.3f; const float* c1[1] = { &one };
    auto sb = prepareStation(c1, 1, 1, 44100.0, o); CHECK(sb != nullptr && sb->length == 1 && allFinite(sb->ch[0]));
    Vec silent(1000, 0.f); const float* cs[1] = { silent.data() };
    auto zb = prepareStation(cs, 1, 1000, 44100.0, o); CHECK(zb != nullptr && peakAbs(zb->ch[0]) == 0.0);
    // DC удалён
    Vec dc = sineV(440, 44100, 1.0, 0.2f); for (auto& x : dc) x += 0.3f;
    const float* cd[1] = { dc.data() };
    auto db = prepareStation(cd, 1, (long long) dc.size(), 44100.0, o);
    CHECK(std::fabs(mean(db->ch[0], 0)) < 1e-3);
    // обрезка по длине
    PrepareOptions lim = o; lim.maxSeconds = 0.5; Vec lng = sineV(440, 44100, 2.0, 0.5f);
    const float* cl[1] = { lng.data() };
    auto lb = prepareStation(cl, 1, (long long) lng.size(), 44100.0, lim); CHECK(lb && lb->length == 22050);
    // уникальные id
    CHECK(sb->id != zb->id);
}

// ---------- воспроизведение ----------
void test_station_plays_without_input()
{
    TS t; t.freq = 98.f; t.L = t.R = sineV(440, SR, 2.0, 0.5f);
    auto snap = makeSnap({ t }, SR);
    RadioEngine e; e.prepare(SR);
    auto o = render(e, snap.get(), baseParams(98.f), 2.0, 256, SR);
    const size_t a = (size_t) (0.5 * SR);
    const double amp = goertzel(o.L, a, o.L.size(), 440.0, SR);
    const double total = rms(o.L, a, o.L.size());
    std::printf("    station tone: amp=%.3f rms=%.3f (%.1f dBFS)\n", amp, total, dB(total));
    CHECK(amp > 0.15 && amp < 0.21);                       // ~ -18 dBFS RMS -> пик 0.178
    CHECK(std::fabs(dB(total) - (-18.0)) < 1.5);
    CHECK((amp / 1.41421356) / total > 0.97);              // преимущественно чистый тон
    CHECK(allFinite(o.L) && allFinite(o.R));
    // первые миллисекунды: плавное нарастание, без щелчка
    CHECK(maxJump(o.L, 1, 2000) < 0.02);
}

void test_looping_is_seamless()
{
    // 0.5 с, 440.3 Гц: на границе фаза не сходится (220.15 периода) -> без кроссфейда был бы скачок
    TS t; t.freq = 98.f; t.L = t.R = sineV(440.3, SR, 0.5, 0.5f);
    EngineParams p = baseParams(98.f); p.noise = 0.f;
    RadioEngine e1; e1.prepare(SR);
    auto s1 = makeSnap({ t }, SR);
    auto o1 = render(e1, s1.get(), p, 4.0, 512, SR);
    t.fadeMs = 0.f;
    RadioEngine e0; e0.prepare(SR);
    auto s0 = makeSnap({ t }, SR);
    auto o0 = render(e0, s0.get(), p, 4.0, 512, SR);
    const size_t a = (size_t) (0.2 * SR);
    const double jFade = maxJump(o1.L, a), jNo = maxJump(o0.L, a);
    const double natural = 0.178 * 2 * kPi * 440.3 / SR;
    std::printf("    loop: maxJump with fade=%.4f without fade=%.4f natural=%.4f\n", jFade, jNo, natural);
    CHECK(jFade < 1.5 * natural);
    CHECK(jNo > 3.0 * natural);                            // тест чувствителен: без fade скачок виден
    // уровень стабилен на протяжении нескольких циклов (провал в зоне кроссфейда ограничен)
    double lo = 1e9, hi = 0;
    for (size_t w = a; w + 2205 < o1.L.size(); w += 2205) { const double r = rms(o1.L, w, w + 2205); lo = std::min(lo, r); hi = std::max(hi, r); }
    std::printf("    loop: window rms min=%.3f max=%.3f\n", lo, hi);
    CHECK(lo > 0.6 * hi);
    CHECK(allFinite(o1.L));
}

void test_one_shot_and_restart()
{
    TS t; t.freq = 98.f; t.mode = PlaybackMode::OneShot; t.L = t.R = sineV(440, SR, 0.3, 0.5f);
    auto snap = makeSnap({ t }, SR);
    EngineParams p = baseParams(98.f); p.noise = 0.f;
    RadioEngine e; e.prepare(SR);
    auto o = render(e, snap.get(), p, 1.0, 256, SR);
    CHECK(rms(o.L, 2000, 10000) > 0.08);                              // играет
    CHECK(rms(o.L, (size_t) (0.4 * SR), o.L.size()) < 1e-4);          // после конца - тишина (нет шума: noise=0)
    CHECK(maxJump(o.L, 1) < 0.03);                                    // конец без щелчка
    e.postCommand(Command::Restart);
    auto o2 = render(e, snap.get(), p, 0.2, 256, SR);
    CHECK(rms(o2.L, 2000, 8000) > 0.08);                              // Restart: снова играет
    CHECK(e.slotPosition(0) > 0 && e.slotPosition(0) < (long long) (0.25 * SR));
}

// ---------- настройка и переключение ----------
void test_switching_between_stations()
{
    TS a, b; a.slot = 0; a.freq = 95.f; a.L = a.R = sineV(440, SR, 3.0, 0.5f);
    b.slot = 1; b.freq = 97.f; b.L = b.R = sineV(1000, SR, 3.0, 0.5f);
    auto snap = makeSnap({ a, b }, SR);
    EngineParams p = baseParams(95.f); p.noise = 0.f;
    RadioEngine e; e.prepare(SR);
    const int blk = 64; const int blocks = (int) (1.5 * SR / blk);
    auto o = render(e, snap.get(), p, 1.5, blk, SR, 2, [&](int bi, EngineParams& q) { q.frequency = 95.f + 2.f * (float) bi / (float) blocks; });
    const size_t from = (size_t) (0.2 * SR);
    const double j = maxJump(o.L, from);
    const double pk = peakAbs(o.L, from);
    std::printf("    sweep A->B: maxJump=%.4f peak=%.3f\n", j, pk);
    CHECK(j < 0.06);                                                  // 1 кГц * 0.178: естественный наклон ~0.025
    CHECK(pk < 0.40);                                                 // нет всплесков громкости при смешивании
    CHECK(allFinite(o.L));
    // в начале слышна A (440), в конце B (1000)
    CHECK(goertzel(o.L, from, from + 4410, 440.0, SR) > 5 * goertzel(o.L, from, from + 4410, 1000.0, SR));
    CHECK(goertzel(o.L, o.L.size() - 4410, o.L.size(), 1000.0, SR) > 5 * goertzel(o.L, o.L.size() - 4410, o.L.size(), 440.0, SR));
    // громкость внутри развёртки не превышает громкость отдельной станции
    double mx = 0; for (size_t w = from; w + 2205 < o.L.size(); w += 2205) mx = std::max(mx, rms(o.L, w, w + 2205));
    CHECK(mx < 0.126 * 1.2);
}

void test_correlated_stations_level()
{
    // Два одинаковых (коррелированных) источника рядом: пик громкости не должен превышать ~+3.2 дБ
    TS a, b; a.slot = 0; a.freq = 98.0f; b.slot = 1; b.freq = 98.4f;
    a.L = a.R = b.L = b.R = sineV(440, SR, 2.0, 0.5f);
    auto two = makeSnap({ a, b }, SR), one = makeSnap({ a }, SR);
    EngineParams p = baseParams(98.f); p.noise = 0.f; p.selectivity = 0.3f;
    double worst = 0;
    for (float f = 97.8f; f <= 98.6f; f += 0.1f)
    {
        RadioEngine e; e.prepare(SR); p.frequency = f;
        auto o = render(e, two.get(), p, 1.0, 256, SR);
        worst = std::max(worst, rms(o.L, 20000, o.L.size()));
    }
    const double ref = 0.126;
    std::printf("    correlated stations: worst %.1f dB relative to single station\n", dB(worst / ref));
    CHECK(dB(worst / ref) < 3.3);
}

// ---------- шум ----------
static Vec noiseAt(float tune, float noise, float reception, double sec = 3.0, bool analog = true, double sr = SR)
{
    TS t; t.freq = 98.f; t.L = t.R = Vec((size_t) (sr * 1.0), 0.f);          // тишина: слышен только шум приёмника
    auto snap = makeSnap({ t }, sr);
    EngineParams p = baseParams(tune); p.noise = noise; p.reception = reception; p.analog = analog;
    RadioEngine e; e.prepare(sr);
    return render(e, snap.get(), p, sec, 256, sr).L;
}

void test_noise_vs_tuning()
{
    const double on  = rms(noiseAt(98.f, 0.5f, 0.85f), 20000, 1 << 30);
    const double off = rms(noiseAt(93.f, 0.5f, 0.85f), 20000, 1 << 30);
    std::printf("    noise rms: on-station=%.5f  between=%.5f\n", on, off);
    CHECK(on < 0.2 * off);                                            // точная настройка -> шум заметно меньше
    CHECK(off > 0.01 && off < 0.5);                                   // вне станций шум слышен, но не чрезмерен
    // монотонность по расстоянию от станции (с допуском на случайные всплески)
    double prev = 0; bool mono = true;
    for (float d : { 0.0f, 0.3f, 0.6f, 1.0f, 2.0f })
    {
        const double r = rms(noiseAt(98.f + d, 0.5f, 0.85f, 4.0), 30000, 1 << 30);
        std::printf("      offset %.1f MHz: rms=%.5f\n", d, r);
        mono &= (r >= prev * 0.9); prev = r;
    }
    CHECK(mono);
    CHECK(rms(noiseAt(93.f, 0.f, 0.f), 20000, 1 << 30) < 1e-6);        // ручка шума = 0 -> тишина
    CHECK(rms(noiseAt(93.f, 1.f, 0.f, 3.0, false), 20000, 1 << 30) < 0.4 * rms(noiseAt(93.f, 1.f, 0.f, 3.0, true), 20000, 1 << 30)); // CLEAN тише
    // шум растёт с ручкой
    CHECK(rms(noiseAt(93.f, 0.2f, 0.85f), 20000, 1 << 30) < rms(noiseAt(93.f, 0.8f, 0.85f), 20000, 1 << 30));
}

void test_reception_controls_bandwidth()
{
    // станция с тонами 440 Гц и 10 кГц; noise=0 -> чистая оценка полосы
    TS t; t.freq = 98.f;
    Vec a = sineV(440, SR, 2.0, 0.3f), b = sineV(10000, SR, 2.0, 0.3f);
    for (size_t i = 0; i < a.size(); ++i) a[i] += b[i];
    t.L = t.R = a; auto snap = makeSnap({ t }, SR);
    auto hf = [&](float rec, bool analog) {
        EngineParams p = baseParams(98.f); p.noise = 0.f; p.reception = rec; p.analog = analog;
        RadioEngine e; e.prepare(SR); auto o = render(e, snap.get(), p, 2.0, 256, SR);
        return goertzel(o.L, 30000, 80000, 10000.0, SR) / std::max(1e-9, goertzel(o.L, 30000, 80000, 440.0, SR));
    };
    const double good = hf(1.f, true), poor = hf(0.f, true), cleanPoor = hf(0.f, false);
    std::printf("    10k/440 ratio: reception 1=%.3f, reception 0=%.3f, CLEAN reception 0=%.3f\n", good, poor, cleanPoor);
    CHECK(poor < 0.4 * good);                                         // хуже приём -> ВЧ режется
    CHECK(cleanPoor > 0.7 * good);                                    // CLEAN почти не портит звук
}

// ---------- стабильность ----------
void test_no_stations_is_silent_and_safe()
{
    RadioEngine e; e.prepare(SR);
    EngineParams p = baseParams(93.f); p.noise = 1.f; p.reception = 0.f; p.saturation = 1.f;
    auto o1 = render(e, nullptr, p, 1.0, 256, SR);
    CHECK(peakAbs(o1.L) < 1e-9 && peakAbs(o1.R) < 1e-9);              // нет станций -> ровная тишина
    StationSnapshot empty; empty.serial = 5;
    auto o2 = render(e, &empty, p, 0.5, 100, SR);
    CHECK(peakAbs(o2.L) < 1e-9);
    // станция выключена или с неподходящей частотой тоже не даёт мусора
    TS t; t.L = t.R = sineV(440, SR, 1.0, 0.5f); t.enabled = false;
    auto sOff = makeSnap({ t }, SR); auto o3 = render(e, sOff.get(), p, 0.5, 256, SR);
    CHECK(peakAbs(o3.L) < 1e-9);
    // пустой снимок -> станция добавляется позже: нет щелчка
    t.enabled = true; auto sOn = makeSnap({ t }, SR, 7);
    EngineParams pq = baseParams(98.f); pq.noise = 0.f;                  // щелчок включения измеряем без шума
    { StationSnapshot none2; none2.serial = 6; render(e, &none2, pq, 0.3, 256, SR); }   // сглаживатели шума успокаиваются
    auto o4 = render(e, sOn.get(), pq, 1.0, 256, SR);
    {   size_t at = 0; double mj = 0; for (size_t i = 1; i < o4.L.size(); ++i) { const double j = std::fabs(o4.L[i] - o4.L[i-1]); if (j > mj) { mj = j; at = i; } }
        std::printf("    station added later: maxJump=%.4f at sample %zu\n", mj, at); }
    CHECK(allFinite(o4.L) && maxJump(o4.L) < 0.05);
}

void test_invalid_parameters()
{
    TS t; t.L = t.R = sineV(440, SR, 1.0, 0.5f); auto snap = makeSnap({ t }, SR);
    const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
    EngineParams p;
    p.frequency = nan; p.reception = inf; p.noise = -inf; p.selectivity = 1e30f; p.tuningSpeed = nan;
    p.scanSensitivity = -5.f; p.saturation = 1e9f; p.width = -3.f; p.outputGainDb = inf;
    RadioEngine e; e.prepare(SR);
    auto o = render(e, snap.get(), p, 1.0, 256, SR);
    CHECK(allFinite(o.L) && allFinite(o.R));
    CHECK(peakAbs(o.L) <= 4.0001);
    const EngineParams s = p.sanitized();
    CHECK(s.frequency >= 87.5f && s.frequency <= 108.f && s.reception <= 1.f && s.noise >= 0.f && s.outputGainDb <= 24.f);
    // нулевой/отрицательный размер блока и нулевые каналы не падают
    float dummy[4] = {}; float* ptrs[2] = { dummy, dummy };
    e.process(snap.get(), p, ptrs, 2, 0); e.process(snap.get(), p, ptrs, 0, 4); e.process(snap.get(), p, nullptr, 2, 4);
    CHECK(true);
    // неверная частота engine.prepare
    RadioEngine bad; bad.prepare(-5.0); auto ob = render(bad, snap.get(), baseParams(98.f), 0.2, 128, 44100.0);
    CHECK(allFinite(ob.L));
}

void test_stress_rapid_param_changes()
{
    struct Case { double sr; int block; };
    for (int noiseOff = 0; noiseOff < 2; ++noiseOff)
    for (Case c : { Case{ 22050.0, 1 }, Case{ 44100.0, 13 }, Case{ 48000.0, 64 }, Case{ 96000.0, 512 }, Case{ 44100.0, 4096 } })
    {
        TS a, b; a.slot = 0; a.freq = 92.f; a.L = a.R = sineV(300, c.sr, 1.0, 0.8f);
        b.slot = 5; b.freq = 101.f; b.L = sineV(2000, c.sr, 1.3, 0.9f); b.R = sineV(2100, c.sr, 1.3, 0.9f);
        auto snap = makeSnap({ a, b }, c.sr);
        RadioEngine e; e.prepare(c.sr);
        freq::dsp::Rng rng(99);
        EngineParams p = baseParams(95.f); p.limiterBypass = false;
        const double sec = c.block == 1 ? 0.3 : 2.0;
        auto o = render(e, snap.get(), p, sec, c.block, c.sr, 2, [&](int bi, EngineParams& q) {
            if (c.block <= 13 && bi % 64) return;
            q.frequency = 87.5f + 20.5f * rng.nextUnit(); q.reception = rng.nextUnit(); q.noise = rng.nextUnit();
            q.selectivity = rng.nextUnit(); q.tuningSpeed = rng.nextUnit(); q.saturation = rng.nextUnit();
            q.width = rng.nextUnit(); q.outputGainDb = -24.f + 36.f * rng.nextUnit();
            q.analog = rng.nextUnit() > 0.5f; q.mono = rng.nextUnit() > 0.8f; q.limiterBypass = false;
            if (noiseOff) q.noise = 0.f;
        });
        const double pk = std::max(peakAbs(o.L), peakAbs(o.R));
        const double jmp = std::max(maxJump(o.L), maxJump(o.R));
        std::printf("    stress%s sr=%.0f block=%d: peak=%.4f maxJump=%.4f mean=%.5f\n", noiseOff ? "(no noise)" : "", c.sr, c.block, pk, jmp, mean(o.L, o.L.size() / 4));
        CHECK(allFinite(o.L) && allFinite(o.R));
        CHECK(pk <= 0.9801);                                           // ограничитель держит потолок
        CHECK(jmp < (noiseOff ? 0.45 : 0.7));                          // без шума порог жёстче: это щелчки от смены параметров
        CHECK(std::fabs(mean(o.L, o.L.size() / 4)) < 0.02);            // нет DC
    }
}

void test_block_size_invariance()
{
    TS a, b; a.slot = 0; a.freq = 95.f; a.L = a.R = sineV(440, SR, 1.0, 0.5f);
    b.slot = 1; b.freq = 97.f; b.L = sineV(777, SR, 0.9, 0.5f); b.R = sineV(555, SR, 0.9, 0.5f);
    auto snap = makeSnap({ a, b }, SR);
    EngineParams p = baseParams(96.2f); p.noise = 0.6f; p.reception = 0.5f; p.saturation = 0.4f; p.width = 0.8f;
    RadioEngine e0; e0.prepare(SR); auto ref = render(e0, snap.get(), p, 1.0, 64, SR);
    for (int blk : { 1, 7, 100, 513, 4096 })
    {
        RadioEngine e; e.prepare(SR); auto o = render(e, snap.get(), p, 1.0, blk, SR);
        double md = 0; for (size_t i = 0; i < ref.L.size(); ++i) md = std::max({ md, (double) std::fabs(o.L[i] - ref.L[i]), (double) std::fabs(o.R[i] - ref.R[i]) });
        std::printf("    block %d vs 64: max diff=%.2e\n", blk, md);
        CHECK(md < 1e-5);
    }
}

// ---------- mono/stereo ----------
void test_mono_stereo()
{
    TS t; t.freq = 98.f; t.L = sineV(440, SR, 2.0, 0.5f); t.R = sineV(660, SR, 2.0, 0.5f);
    auto snap = makeSnap({ t }, SR);
    EngineParams p = baseParams(98.f); p.noise = 0.f;
    RadioEngine e1; e1.prepare(SR); auto st = render(e1, snap.get(), p, 1.5, 256, SR);
    CHECK(rms(st.L, 20000, st.L.size()) > 0.05 && rms(st.R, 20000, st.R.size()) > 0.05);
    CHECK(goertzel(st.L, 20000, 60000, 440, SR) > 3 * goertzel(st.L, 20000, 60000, 660, SR));     // каналы разные
    // MONO: L == R, а моно-сумма сохраняет энергию M (совместимость)
    p.mono = true; RadioEngine e2; e2.prepare(SR); auto mo = render(e2, snap.get(), p, 1.5, 256, SR);
    double md = 0; for (size_t i = 30000; i < mo.L.size(); ++i) md = std::max(md, (double) std::fabs(mo.L[i] - mo.R[i]));
    CHECK(md < 1e-6);
    Vec mSt(st.L.size()), mMo(mo.L.size());
    for (size_t i = 0; i < mSt.size(); ++i) { mSt[i] = 0.5f * (st.L[i] + st.R[i]); mMo[i] = 0.5f * (mo.L[i] + mo.R[i]); }
    const double d = dB(rms(mMo, 30000, 1 << 30) / rms(mSt, 30000, 1 << 30));
    std::printf("    mono-sum level stereo vs MONO: %.3f dB\n", d);
    CHECK(std::fabs(d) < 0.3);
    // width = 0 даёт то же M без потери уровня моно-суммы
    p.mono = false; p.width = 0.f; RadioEngine e3; e3.prepare(SR); auto w0 = render(e3, snap.get(), p, 1.5, 256, SR);
    double m2 = 0; for (size_t i = 30000; i < w0.L.size(); ++i) m2 = std::max(m2, (double) std::fabs(w0.L[i] - w0.R[i]));
    CHECK(m2 < 1e-6);
    // моно-шина: выход = M
    RadioEngine e4; e4.prepare(SR); p.width = 1.f; auto m1 = render(e4, snap.get(), p, 1.5, 256, SR, 1);
    CHECK(allFinite(m1.L) && rms(m1.L, 30000, 1 << 30) > 0.05);
    // моно-режим при коррелированном (L=R) материале не меняет уровень
    TS c; c.freq = 98.f; c.L = c.R = sineV(440, SR, 2.0, 0.5f); auto cs = makeSnap({ c }, SR);
    p.noise = 0.f; p.width = 1.f; RadioEngine e5, e6; e5.prepare(SR); e6.prepare(SR);
    auto s1 = render(e5, cs.get(), p, 1.5, 256, SR); p.mono = true; auto s2 = render(e6, cs.get(), p, 1.5, 256, SR);
    CHECK(std::fabs(dB(rms(s2.L, 30000, 1 << 30) / rms(s1.L, 30000, 1 << 30))) < 0.1);
}

// ---------- частота дискретизации ----------
void test_sample_rate_change()
{
    TS t; t.freq = 98.f; t.L = t.R = sineV(1000, 44100.0, 2.0, 0.5f);
    auto snap44 = makeSnap({ t }, 44100.0, 1);
    RadioEngine e; e.prepare(44100.0);
    EngineParams p = baseParams(98.f); p.noise = 0.f;
    auto a = render(e, snap44.get(), p, 1.0, 256, 44100.0);
    CHECK(std::fabs(goertzel(a.L, 10000, 40000, 1000.0, 44100.0) - 0.178) < 0.03);

    // хост сменил частоту; менеджер ещё не пересчитал станции: буфер 44.1 кГц недоступен -> тишина, без мусора
    e.prepare(48000.0);
    auto mid = render(e, snap44.get(), p, 0.3, 256, 48000.0);
    CHECK(allFinite(mid.L) && peakAbs(mid.L) < 1e-9);

    // станции пересчитаны под 48 кГц (из исходного файла 44.1 кГц): высота тона сохранена
    auto snap48 = makeSnap({ t }, 48000.0, 2);
    auto b = render(e, snap48.get(), p, 1.0, 256, 48000.0);
    const double a1000 = goertzel(b.L, 10000, 40000, 1000.0, 48000.0), a1089 = goertzel(b.L, 10000, 40000, 1088.0, 48000.0);
    std::printf("    48k after 44.1k file: amp@1000=%.3f amp@1088=%.3f\n", a1000, a1089);
    CHECK(a1000 > 0.15 && a1089 < 0.02);
    CHECK(allFinite(b.L));

    // файл 96 кГц на движке 44.1 кГц и 22.05 кГц
    for (double sr : { 22050.0, 44100.0, 192000.0 })
    {
        TS h; h.freq = 98.f; h.sr = 96000.0; h.L = h.R = sineV(1000, 96000.0, 1.0, 0.5f);
        auto sn = makeSnap({ h }, sr); RadioEngine en; en.prepare(sr);
        auto o = render(en, sn.get(), p, 1.0, 256, sr);
        CHECK(allFinite(o.L) && goertzel(o.L, (size_t) (0.3 * sr), (size_t) (0.9 * sr), 1000.0, sr) > 0.15);
    }
}

// ---------- замена станции, сканер ----------
void test_station_replacement_during_playback()
{
    TS t; t.freq = 98.f; t.L = t.R = sineV(440, SR, 2.0, 0.5f);
    auto s1 = makeSnap({ t }, SR, 1);
    TS u = t; u.L = u.R = sineV(1000, SR, 2.0, 0.5f); auto s2 = makeSnap({ u }, SR, 2);
    EngineParams p = baseParams(98.f); p.noise = 0.f;
    RadioEngine e; e.prepare(SR);
    auto a = render(e, s1.get(), p, 0.7, 256, SR);
    auto b = render(e, s2.get(), p, 0.7, 256, SR);
    Vec all = a.L; all.insert(all.end(), b.L.begin(), b.L.end());
    const size_t sw = a.L.size();
    const double j = maxJump(all, sw - 50, sw + 800);
    std::printf("    replacement: maxJump around swap=%.4f\n", j);
    CHECK(allFinite(all));
    CHECK(j < 0.05);
    CHECK(goertzel(all, sw + 10000, sw + 25000, 1000, SR) > 0.15);          // играет новая станция
    // удаление станции (пустой слот) тоже без щелчка
    StationSnapshot none; none.serial = 3;
    auto c = render(e, &none, p, 0.4, 256, SR);
    Vec all2 = b.L; all2.insert(all2.end(), c.L.begin(), c.L.end());
    CHECK(maxJump(all2, b.L.size() - 50, b.L.size() + 800) < 0.05);
    CHECK(peakAbs(c.L, 4000) < 1e-9);
}

void test_scanner_integration()
{
    TS a, b; a.slot = 0; a.freq = 90.f; a.L = a.R = sineV(440, SR, 3.0, 0.5f);
    b.slot = 1; b.freq = 100.f; b.L = b.R = sineV(1000, SR, 3.0, 0.5f);
    auto snap = makeSnap({ a, b }, SR);
    EngineParams p = baseParams(88.f); p.noise = 0.f; p.tuningSpeed = 0.5f;
    RadioEngine e; e.prepare(SR);
    render(e, snap.get(), p, 0.1, 256, SR);
    e.postCommand(Command::Search);
    int guard = 0; Vec acc;
    while (guard++ < 2000) { auto o = render(e, snap.get(), p, 0.01, 256, SR); acc.insert(acc.end(), o.L.begin(), o.L.end()); if (e.scannerState() == (int) Scanner::State::Idle && guard > 3) break; }
    std::printf("    SEARCH stopped at %.3f MHz after %.2f s\n", e.displayFrequency(), (double) acc.size() / SR);
    CHECK(std::fabs(e.displayFrequency() - 90.f) < 0.01f);
    CHECK(maxJump(acc, 2000) < 0.12);                                      // поиск без щелчков (шум выключен)
    CHECK(e.frequencyOwnedByEngine());
    auto settled = render(e, snap.get(), p, 0.8, 256, SR);                 // даём настроиться (скольжение)
    CHECK(goertzel(settled.L, 20000, 35000, 440, SR) > 0.12);
    // тот же параметр частоты (эхо) не прерывает и не сбивает настройку
    EngineParams echo = p; echo.frequency = e.displayFrequency();
    render(e, snap.get(), echo, 0.2, 256, SR);
    CHECK(std::fabs(e.displayFrequency() - 90.f) < 0.02f);
    // следующий SEARCH находит 100
    e.postCommand(Command::Search); guard = 0;
    while (guard++ < 3000) { render(e, snap.get(), echo, 0.01, 256, SR); if (e.scannerState() == (int) Scanner::State::Idle && guard > 3) break; }
    CHECK(std::fabs(e.displayFrequency() - 100.f) < 0.01f);

    // SCAN: непрерывно, STOP останавливает
    e.postCommand(Command::Scan); render(e, snap.get(), echo, 3.0, 256, SR);
    CHECK(e.scannerState() == (int) Scanner::State::Scanning);
    e.postCommand(Command::Stop); render(e, snap.get(), echo, 0.05, 256, SR);
    CHECK(e.scannerState() == (int) Scanner::State::Idle);
    const float fStop = e.displayFrequency(); render(e, snap.get(), echo, 0.3, 256, SR);
    CHECK(std::fabs(e.displayFrequency() - fStop) < 1e-6f);

    // ручное движение ручки прерывает поиск
    e.postCommand(Command::Search); render(e, snap.get(), echo, 0.05, 256, SR);
    EngineParams manual = echo; manual.frequency = 105.f; render(e, snap.get(), manual, 0.2, 256, SR);
    CHECK(e.scannerState() == (int) Scanner::State::Idle);
    CHECK(std::fabs(e.displayFrequency() - 105.f) < 0.01f);
    CHECK(! e.frequencyOwnedByEngine());

    // LOCK удерживает станцию независимо от ручки
    EngineParams atB = manual; atB.frequency = 100.f; render(e, snap.get(), atB, 0.3, 256, SR);
    e.postCommand(Command::Lock); render(e, snap.get(), atB, 0.1, 256, SR);
    EngineParams moved = atB; moved.frequency = 91.f; render(e, snap.get(), moved, 0.3, 256, SR);
    CHECK(std::fabs(e.displayFrequency() - 100.f) < 0.01f);
    e.postCommand(Command::Unlock); render(e, snap.get(), moved, 0.2, 256, SR);   // возврат управления ручке
    CHECK(std::fabs(e.displayFrequency() - 91.f) < 0.01f);

    // tuneTo (выбор станции параметром)
    e.tuneTo(90.f); render(e, snap.get(), moved, 0.2, 256, SR);
    CHECK(std::fabs(e.displayFrequency() - 90.f) < 0.01f);

    // без станций поиск не останавливается и ничего не ломает
    RadioEngine e2; e2.prepare(SR); render(e2, nullptr, p, 0.05, 256, SR);
    e2.postCommand(Command::Search); render(e2, nullptr, p, 5.0, 256, SR);
    CHECK(e2.scannerState() == (int) Scanner::State::Searching);
}

void test_gain_and_saturation_levels()
{
    TS t; t.freq = 98.f; t.L = t.R = sineV(440, SR, 2.0, 0.5f); auto snap = makeSnap({ t }, SR);
    auto lvl = [&](EngineParams p) { RadioEngine e; e.prepare(SR); auto o = render(e, snap.get(), p, 1.5, 256, SR); return rms(o.L, 30000, 1 << 30); };
    EngineParams p = baseParams(98.f); p.noise = 0.f; p.limiterBypass = true;
    const double r0 = lvl(p); p.outputGainDb = 6.f; const double r6 = lvl(p);
    CHECK(std::fabs(dB(r6 / r0) - 6.0) < 0.2);                              // выходной gain работает
    p.outputGainDb = 0.f; p.saturation = 1.f; const double rs = lvl(p);
    std::printf("    saturation=1 level change: %.2f dB\n", dB(rs / r0));
    CHECK(std::fabs(dB(rs / r0)) < 3.0);                                    // сатурация не взрывает громкость
    p.saturation = 0.f; p.outputGainDb = -60.f; CHECK(lvl(p) < 0.001);
}

void test_no_allocations_in_process()
{
    TS a, b; a.slot = 0; a.freq = 90.f; a.L = a.R = sineV(440, SR, 1.0, 0.5f);
    b.slot = 1; b.freq = 100.f; b.L = b.R = sineV(1000, SR, 1.0, 0.5f);
    auto s1 = makeSnap({ a, b }, SR, 1);
    TS c = a; c.L = c.R = sineV(300, SR, 1.0, 0.5f); auto s2 = makeSnap({ c, b }, SR, 2);
    RadioEngine e; e.prepare(SR);
    Vec L(512), R(512); float* pp[2] = { L.data(), R.data() };
    EngineParams p = baseParams(92.f); p.saturation = 0.5f;
    e.process(s1.get(), p, pp, 2, 512);
    g_allocs = 0; g_counting = true;
    e.postCommand(Command::Search);
    for (int i = 0; i < 400; ++i)
    {
        if (i == 100) e.postCommand(Command::Scan);
        if (i == 200) e.postCommand(Command::Restart);
        if (i == 250) p.frequency = 97.f;
        e.process(i < 150 ? s1.get() : s2.get(), p, pp, 2, 512);     // включая смену снимка
        e.process(nullptr, p, pp, 1, 64);                              // и пустой/моно путь
    }
    g_counting = false;
    std::printf("    allocations inside process(): %ld\n", g_allocs.load());
    CHECK(g_allocs.load() == 0);
}

int main()
{
    RUN(test_prepare_station);
    RUN(test_station_plays_without_input);
    RUN(test_looping_is_seamless);
    RUN(test_one_shot_and_restart);
    RUN(test_switching_between_stations);
    RUN(test_correlated_stations_level);
    RUN(test_noise_vs_tuning);
    RUN(test_reception_controls_bandwidth);
    RUN(test_no_stations_is_silent_and_safe);
    RUN(test_invalid_parameters);
    RUN(test_stress_rapid_param_changes);
    RUN(test_block_size_invariance);
    RUN(test_mono_stereo);
    RUN(test_sample_rate_change);
    RUN(test_station_replacement_during_playback);
    RUN(test_scanner_integration);
    RUN(test_gain_and_saturation_levels);
    RUN(test_no_allocations_in_process);
    std::printf("\n%d checks, %d failed\n", g_checks, g_failed);
    return g_failed == 0 ? 0 : 1;
}
