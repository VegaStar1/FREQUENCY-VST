// Тесты пресетов: формат файла, валидация, фабричные пресеты на реальном аудио через RadioEngine.
#include "Presets/FactoryPresets.h"
#include "Presets/PresetFile.h"
#include "Stations/StationPrep.h"
#include <cmath>
#include <cstdio>
#include <limits>
#include <set>
#include <vector>

using namespace freq; using namespace freq::preset; using namespace freq::dsp;

static int g_failed = 0, g_checks = 0;
#define CHECK(c) do { ++g_checks; if (!(c)) { ++g_failed; std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)
#define RUN(fn) do { int b = g_failed; fn(); std::printf("[%s] %s\n", g_failed == b ? " ok " : "FAIL", #fn); } while (0)

static const double SR = 44100.0;

static std::shared_ptr<StationSnapshot> toneSnapshot()
{
    std::vector<float> v((size_t) (SR * 3)); for (size_t i = 0; i < v.size(); ++i) v[i] = 0.5f * (float) std::sin(2 * 3.14159265358979 * 440.0 * (double) i / SR);
    const float* ch[2] = { v.data(), v.data() }; PrepareOptions o; o.targetRate = SR;
    auto s = std::make_shared<StationSnapshot>(); s->serial = 1;
    auto& r = s->slots[0]; r.buffer = prepareStation(ch, 2, (long long) v.size(), SR, o); r.freqMHz = 98.f; r.enabled = true;
    return s;
}

struct Rendered { std::vector<float> L, R; };
static Rendered render(RadioEngine& e, const StationSnapshot* s, const EngineParams& p, double sec, int block = 256)
{
    Rendered o; std::vector<float> a((size_t) block), b((size_t) block); float* pp[2] = { a.data(), b.data() };
    for (int done = 0; done < (int) (sec * SR); done += block)
    { e.process(s, p, pp, 2, block); o.L.insert(o.L.end(), a.begin(), a.end()); o.R.insert(o.R.end(), b.begin(), b.end()); }
    return o;
}
static double rms(const std::vector<float>& x, size_t from) { double s = 0; for (size_t i = from; i < x.size(); ++i) s += (double) x[i] * x[i]; return std::sqrt(s / (double) (x.size() - from)); }
static double peak(const std::vector<float>& x) { double p = 0; for (float v : x) p = std::max(p, (double) std::fabs(v)); return p; }
static bool finite(const std::vector<float>& x) { for (float v : x) if (! std::isfinite(v)) return false; return true; }
static double dB(double x) { return 20 * std::log10(std::max(x, 1e-12)); }

// ---------- фабричные пресеты ----------
void test_factory_presets_valid()
{
    const char* expected[] = { "CLEAN FM", "VINTAGE FM", "DISTANT AM", "NIGHT DRIVE", "LOST SIGNAL", "TUNING BETWEEN STATIONS",
                               "BROKEN RECEIVER", "CINEMATIC SEARCH", "HIP-HOP RADIO INTRO", "MINIMAL INTERFERENCE" };
    std::set<std::string> names;
    for (int i = 0; i < kNumFactoryPresets; ++i)
    {
        const auto& p = kFactoryPresets[i];
        CHECK(std::string(p.name) == expected[i]);
        CHECK(names.insert(p.name).second);                                     // уникальные имена
        CHECK(sanitizeName(p.name) == p.name);                                  // имя проходит собственную санитизацию
        const auto c = clampValues(p.values);                                   // значения уже в диапазонах параметров
        CHECK(approxEqual(c, p.values) && c.outputGainDb == p.values.outputGainDb && c.reception == p.values.reception
              && c.noise == p.values.noise && c.selectivity == p.values.selectivity && c.tuningSpeed == p.values.tuningSpeed
              && c.scanSensitivity == p.values.scanSensitivity && c.saturation == p.values.saturation && c.width == p.values.width);
        CHECK(! p.values.limiterBypass);                                        // ограничитель включён
        CHECK(std::string(p.description).size() > 10);
        // сериализация -> разбор возвращает то же (с точностью формата)
        const auto r = parsePreset(serializePreset(p.name, p.values));
        CHECK(r.ok && r.name == p.name && approxEqual(r.values, p.values));
    }
    CHECK(names.size() == 10);
}

void test_factory_presets_on_audio()
{
    auto snap = toneSnapshot();
    double offLevel[kNumFactoryPresets] = {};
    for (int i = 0; i < kNumFactoryPresets; ++i)
    {
        const auto& fp = kFactoryPresets[i];
        // на станции
        RadioEngine e; e.prepare(SR);
        auto on = render(e, snap.get(), toEngineParams(fp.values, 98.f), 2.0);
        // между станциями
        RadioEngine e2; e2.prepare(SR);
        auto off = render(e2, snap.get(), toEngineParams(fp.values, 93.f), 3.0);
        const double onRms = rms(on.L, 30000), offRms = rms(off.L, 30000);
        const double pk = std::max({ peak(on.L), peak(on.R), peak(off.L), peak(off.R) });
        offLevel[i] = offRms;
        std::printf("    %-24s on-station %6.1f dBFS | between %6.1f dBFS | peak %.3f\n", fp.name, dB(onRms), dB(offRms), pk);
        CHECK(finite(on.L) && finite(on.R) && finite(off.L) && finite(off.R));
        CHECK(pk <= 0.9801);                                                    // нет клиппинга
        CHECK(dB(onRms) > -30.0 && dB(onRms) < -8.0);                           // разумная громкость на станции
        CHECK(dB(offRms) < -10.0);                                              // шум между станциями не чрезмерен
        if (fp.values.mono) { double d = 0; for (size_t k = 30000; k < on.L.size(); ++k) d = std::max(d, (double) std::fabs(on.L[k] - on.R[k])); CHECK(d < 1e-6); }
    }
    // характер пресетов различим: LOST SIGNAL/BROKEN шумнее CLEAN FM и MINIMAL INTERFERENCE
    CHECK(offLevel[4] > 3.0 * offLevel[0]); CHECK(offLevel[6] > 3.0 * offLevel[0]);
    CHECK(offLevel[9] < offLevel[1]);
}

void test_preset_switching_is_stable()
{
    auto snap = toneSnapshot();
    RadioEngine e; e.prepare(SR);
    std::vector<float> L, R; std::vector<float> a(128), b(128); float* pp[2] = { a.data(), b.data() };
    for (int blk = 0; blk < (int) (6.0 * SR / 128); ++blk)
    {
        const auto& fp = kFactoryPresets[(blk / 17) % kNumFactoryPresets];       // смена пресета каждые ~50 мс
        e.process(snap.get(), toEngineParams(fp.values, 96.f + 2.f * (float) ((blk / 50) % 2)), pp, 2, 128);
        L.insert(L.end(), a.begin(), a.end()); R.insert(R.end(), b.begin(), b.end());
    }
    CHECK(finite(L) && finite(R)); CHECK(peak(L) <= 0.9801 && peak(R) <= 0.9801);
}

// ---------- формат файла ----------
void test_file_roundtrip_and_validation()
{
    PresetValues v; v.reception = 0.33f; v.noise = 0.77f; v.outputGainDb = -7.5f; v.analog = false; v.mono = true;
    auto r = parsePreset(serializePreset("My Preset", v));
    CHECK(r.ok && r.name == "My Preset" && approxEqual(r.values, v) && ! r.values.analog && r.values.mono);

    // CRLF, комментарии, лишние пробелы, неизвестные ключи, отсутствующие ключи -> значения по умолчанию
    r = parsePreset("# comment\r\nFREQUENCY_PRESET 1\r\n  name = Test  \r\nfutureKey=42\r\nnoise = 0.5\r\n");
    CHECK(r.ok && r.name == "Test" && r.values.noise == 0.5f && r.values.reception == PresetValues{}.reception);

    // зажим вне диапазона
    r = parsePreset("FREQUENCY_PRESET 1\nname=X\nnoise=99\nreception=-4\noutputGain=1000\nwidth=2\n");
    CHECK(r.ok && r.values.noise == 1.f && r.values.reception == 0.f && r.values.outputGainDb == 12.f && r.values.width == 1.f);

    // ошибки
    const char* bad[] = {
        "", "\n\n", "hello", "FREQUENCY_PRESET\nname=X\n", "FREQUENCY_PRESET 0\nname=X\n", "FREQUENCY_PRESET 2\nname=X\n",
        "FREQUENCY_PRESET 1\n", "FREQUENCY_PRESET 1\nname=\n", "FREQUENCY_PRESET 1\nname=X\nnoise=abc\n",
        "FREQUENCY_PRESET 1\nname=X\nnoise=nan\n", "FREQUENCY_PRESET 1\nname=X\nnoise=inf\n", "FREQUENCY_PRESET 1\nname=X\nnoise=0.5x\n",
        "FREQUENCY_PRESET 1\nname=X\nmono=maybe\n", "FREQUENCY_PRESET 1\nname=X\nnoise=\n" };
    for (const char* t : bad) CHECK(! parsePreset(t).ok);
    CHECK(! parsePreset(std::string(kMaxPresetFileBytes + 1, 'a')).ok);          // слишком большой файл
    {   // встроенный нуль-байт: не падает, «испорченный» ключ игнорируется, значения остаются по умолчанию
        const std::string withNul = std::string("FREQUENCY_PRESET 1\nname=X\n") + '\0' + "noise=1\n";
        const auto rn = parsePreset(withNul);
        CHECK(rn.ok && rn.values.noise == PresetValues{}.noise);
    }

    // имя: управляющие символы убираются, длина ограничена, перевод строки не ломает формат
    const auto s = serializePreset("A\nnoise=1\r\nB", PresetValues{});
    r = parsePreset(s);
    CHECK(r.ok && r.name.find('\n') == std::string::npos && r.values.noise == PresetValues{}.noise);
    CHECK(sanitizeName(std::string(500, 'x')).size() == kMaxNameLength);
}

void test_file_name_sanitizing()
{
    CHECK(sanitizeFileName("Night/Drive: v2?") == "Night_Drive_ v2_");
    CHECK(sanitizeFileName("..\\..\\evil") == ".._.._evil");                  // разделители пути заменены
    CHECK(sanitizeFileName("").size() > 0 && sanitizeFileName("   ") == "Preset" && sanitizeFileName("...") == "Preset");
    CHECK(sanitizeFileName("CON") == "_CON" && sanitizeFileName("nul") == "_nul" && sanitizeFileName("COM1") == "_COM1" && sanitizeFileName("lpt9") == "_lpt9");
    CHECK(sanitizeFileName("COM0") == "COM0" && sanitizeFileName("Console") == "Console");
    CHECK(sanitizeFileName("name. ") == "name");
    CHECK(sanitizeFileName(std::string(300, 'a')).size() == kMaxNameLength);
    for (const char* n : { "a/b", "a\\b", "a:b", "a*b", "a\"b", "a<b>", "a|b" })
    { const auto f = sanitizeFileName(n); CHECK(f.find_first_of("<>:\"/\\|?*") == std::string::npos); }
}

void test_approx_equal_and_clamp()
{
    PresetValues a, b = a; CHECK(approxEqual(a, b));
    b.noise += 0.004f; CHECK(approxEqual(a, b));                                // квантование хоста не считается правкой
    b.noise += 0.05f; CHECK(! approxEqual(a, b));
    b = a; b.mono = ! b.mono; CHECK(! approxEqual(a, b));
    b = a; b.outputGainDb += 1.f; CHECK(! approxEqual(a, b));
    PresetValues n; n.noise = std::numeric_limits<float>::quiet_NaN(); n.reception = std::numeric_limits<float>::infinity();
    const auto c = clampValues(n); CHECK(c.noise == 0.25f && c.reception == 0.85f);  // NaN и Inf -> значение по умолчанию (как в EngineParams::sanitized)
}

int main()
{
    RUN(test_factory_presets_valid);
    RUN(test_factory_presets_on_audio);
    RUN(test_preset_switching_is_stable);
    RUN(test_file_roundtrip_and_validation);
    RUN(test_file_name_sanitizing);
    RUN(test_approx_equal_and_clamp);
    std::printf("\n%d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
