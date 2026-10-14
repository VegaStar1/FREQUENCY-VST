#include "StationPrep.h"
#include <algorithm>
#include <atomic>
#include <cmath>

namespace freq
{
    namespace
    {
        constexpr double kPi = 3.14159265358979323846;
        std::atomic<std::uint64_t> g_nextId { 1 };

        double sinc(double x) { return std::fabs(x) < 1e-9 ? 1.0 : std::sin(kPi * x) / (kPi * x); }
    }

    std::vector<float> resampleSinc(const std::vector<float>& in, double srcRate, double dstRate)
    {
        if (in.empty() || srcRate <= 0 || dstRate <= 0) return {};
        if (std::fabs(srcRate - dstRate) < 1e-6) return in;

        const double ratio = srcRate / dstRate;                       // шаг по входу на один выходной сэмпл
        const double fc = std::min(1.0, 1.0 / ratio);                 // срез относительно Найквиста источника
        const double halfWidth = 16.0 / fc;                           // ширина ядра во входных сэмплах
        const long long n = (long long) in.size();
        const long long m = (long long) std::floor((double) n / ratio);
        std::vector<float> out((size_t) std::max(0LL, m));

        for (long long j = 0; j < m; ++j)
        {
            const double t = (double) j * ratio;
            const long long i0 = (long long) std::floor(t);
            const long long lo = std::max(0LL, i0 - (long long) std::ceil(halfWidth) + 1);
            const long long hi = std::min(n - 1, i0 + (long long) std::ceil(halfWidth));
            double acc = 0, wsum = 0;
            for (long long i = lo; i <= hi; ++i)
            {
                const double x = t - (double) i;
                const double u = x / halfWidth;
                if (std::fabs(u) >= 1.0) continue;
                const double win = 0.42 + 0.5 * std::cos(kPi * u) + 0.08 * std::cos(2.0 * kPi * u);
                const double w = fc * sinc(fc * x) * win;
                acc += w * in[(size_t) i]; wsum += w;
            }
            out[(size_t) j] = wsum != 0 ? (float) (acc / wsum) : 0.f;  // нормировка: единичное усиление на DC
        }
        return out;
    }

    std::shared_ptr<StationBuffer> prepareStation(const float* const* channels, int numChannels,
                                                  long long numSamples, double srcRate,
                                                  const PrepareOptions& opt)
    {
        if (channels == nullptr || numChannels <= 0 || numSamples <= 0) return nullptr;
        if (! (srcRate >= 1000.0 && srcRate <= 768000.0) || ! (opt.targetRate >= 1000.0 && opt.targetRate <= 768000.0))
            return nullptr;
        const int nch = std::min(2, numChannels);
        for (int c = 0; c < nch; ++c) if (channels[c] == nullptr) return nullptr;

        const long long maxIn = (long long) (opt.maxSeconds * srcRate);
        const long long n = std::min(numSamples, std::max(1LL, maxIn));

        std::array<std::vector<float>, 2> data;
        for (int c = 0; c < nch; ++c)
        {
            auto& v = data[(size_t) c];
            v.resize((size_t) n);
            double sum = 0;
            for (long long i = 0; i < n; ++i)
            {
                float x = channels[c][i];
                if (! std::isfinite(x)) x = 0.f;
                v[(size_t) i] = x; sum += x;
            }
            const float mean = (float) (sum / (double) n);              // убираем DC
            for (auto& x : v) x -= mean;
            v = resampleSinc(v, srcRate, opt.targetRate);
            if (v.empty()) return nullptr;
        }

        auto buf = std::make_shared<StationBuffer>();
        buf->id = g_nextId.fetch_add(1);
        buf->numChannels = nch;
        buf->length = (int) data[0].size();
        buf->sampleRate = opt.targetRate;

        double sq = 0; float peak = 0; long long cnt = 0;
        for (int c = 0; c < nch; ++c)
            for (float x : data[(size_t) c]) { sq += (double) x * x; peak = std::max(peak, std::fabs(x)); ++cnt; }
        const double rms = std::sqrt(sq / (double) std::max(1LL, cnt));

        float gain = 1.0f;
        if (rms > 1e-9 && peak > 1e-9f)
        {
            const double byRms  = std::pow(10.0, opt.targetRmsDb / 20.0) / rms;
            const double byPeak = (double) opt.maxPeak / peak;
            const double byMax  = std::pow(10.0, opt.maxGainDb / 20.0);
            gain = (float) std::min({ byRms, byPeak, byMax });
        }
        for (int c = 0; c < nch; ++c)
        {
            for (auto& x : data[(size_t) c]) x *= gain;
            buf->ch[(size_t) c] = std::move(data[(size_t) c]);
        }
        buf->normalizeGain = gain;
        return buf;
    }
}
