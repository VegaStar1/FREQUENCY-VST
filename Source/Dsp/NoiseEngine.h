#pragma once
#include "Biquad.h"
#include "Rng.h"
#include <algorithm>
#include <cmath>

namespace freq::dsp
{
    // Слоистый шум: статик (широкополосный + фильтрованный), всплески помех, потрескивания.
    // Все уровни сглаживаются; выход жёстко ограничен +-kMaxOut (по умолчанию нет резких пиков).
    //
    // Карта уровней: чем хуже приём, тем больше шум. Режим CLEAN даёт лишь следовое количество шума.
    struct NoiseLevels { float stat = 0, burst = 0, crackle = 0; };

    inline NoiseLevels mapNoiseLevels(float noiseKnob, float reception, bool analog)
    {
        const float n = std::clamp(noiseKnob, 0.f, 1.f);
        const float bad = 1.0f - std::clamp(reception, 0.f, 1.f);
        const float k = analog ? 1.0f : 0.08f;
        const float curve = bad * bad * (3.0f - 2.0f * bad);                  // smoothstep: мягкий вход
        NoiseLevels l;
        l.stat    = k * n * (0.02f + 0.98f * curve) * 0.25f;                  // макс. ~ -12 дБFS
        l.burst   = k * n * curve * curve;
        l.crackle = k * n * curve;
        return l;
    }

    class NoiseEngine
    {
    public:
        static constexpr float kMaxOut = 0.5f;

        void prepare(double fs, std::uint32_t seed)
        {
            fs_ = fs;
            rng_.reseed(seed);
            for (auto* s : { &sStat_, &sBurst_, &sCrackle_ }) { s->prepare(fs, 0.05); s->setImmediate(0); }
            lpA_.setCoeffs(BiquadCoeffs::lowpass(fs, 3000.0, 0.7));
            hpA_.setCoeffs(BiquadCoeffs::highpass(fs, 300.0, 0.7));
            burstLp_.setCoeffs(BiquadCoeffs::lowpass(fs, 1800.0, 0.7));
            lpA_.reset(); hpA_.reset(); burstLp_.reset();
            burstPos_ = burstLen_ = 0; crackleEnv_ = 0; crackleSign_ = 1; crackleOut_ = 0;
            crackleRise_ = (float) (1.0 - std::exp(-1.0 / (0.0002 * fs)));
        }

        void setLevels(const NoiseLevels& l) noexcept
        {
            sStat_.setTarget(l.stat); sBurst_.setTarget(l.burst); sCrackle_.setTarget(l.crackle);
        }

        float next() noexcept
        {
            const float stat = sStat_.next(), burst = sBurst_.next(), crackle = sCrackle_.next();
            const float w = rng_.nextBipolar();

            // 1) широкополосный статик + «окрашенный» (300 Гц-3 кГц) для тела шума
            float out = stat * (0.6f * w + 0.9f * hpA_.process(lpA_.process(rng_.nextBipolar())));

            // 2) всплески помех: ~0..3 в секунду, 20-80 мс, полусинусная огибающая
            if (burstPos_ >= burstLen_ && burst > 1.0e-4f
                && rng_.nextUnit() < (float) (burst * 3.0 / fs_))
            {
                burstLen_ = (int) (fs_ * (0.02 + 0.06 * rng_.nextUnit()));
                burstPos_ = 0;
            }
            if (burstPos_ < burstLen_)
            {
                const float env = std::sin(3.14159265f * (float) burstPos_ / (float) burstLen_);
                out += burstLp_.process(rng_.nextBipolar()) * env * burst * 0.35f;
                ++burstPos_;
            }

            // 3) потрескивания: редкие мягкие импульсы с экспоненциальным спадом (~1 мс)
            if (crackle > 1.0e-4f && rng_.nextUnit() < (float) (crackle * 40.0 / fs_))
            {
                crackleEnv_ = 0.07f + 0.06f * rng_.nextUnit();            // мягкие, не резкие
                crackleSign_ = rng_.nextBipolar() < 0 ? -1.f : 1.f;
            }
            // фронт ~0.2 мс (однополюсный сглаживатель): нет мгновенного ступенчатого скачка
            crackleOut_ += crackleRise_ * (crackleSign_ * crackleEnv_ * crackle - crackleOut_);
            out += crackleOut_;
            crackleEnv_ *= (float) std::exp(-1.0 / (0.001 * fs_));

            return std::clamp(out, -kMaxOut, kMaxOut);
        }

    private:
        double fs_ = 44100.0;
        Rng rng_;
        Smoother sStat_, sBurst_, sCrackle_;
        Biquad lpA_, hpA_, burstLp_;
        int burstPos_ = 0, burstLen_ = 0;
        float crackleEnv_ = 0, crackleSign_ = 1, crackleOut_ = 0, crackleRise_ = 0.1f;
    };
}
