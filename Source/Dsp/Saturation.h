#pragma once
#include <algorithm>
#include <cmath>

namespace freq::dsp
{
    // Мягкая tanh-сатурация с выравниванием громкости и DC-блокером (10 Гц).
    // DC-блокер может давать небольшой выброс на сильно насыщенных НЧ-сигналах, поэтому после
    // сатуратора ставится softCeiling.
    // amount = 0 -> точный проход (бит-в-бит).
    class Saturator
    {
    public:
        Saturator() { buildMakeupTable(); }

        void prepare(double fs) noexcept
        {
            r_ = (float) std::exp(-2.0 * 3.14159265358979 * 10.0 / fs);   // DC-блокер ~10 Гц
            x1_ = y1_ = 0;
        }
        void reset() noexcept { x1_ = y1_ = 0; }

        // y = makeup(d) * tanh(d*x) / d : единичное усиление для малых сигналов, пики сжимаются,
        // makeup выравнивает громкость с несатурированным сигналом на опорном уровне (синус 0.2).
        float process(float x, float amount) noexcept
        {
            if (amount <= 0.0f) return x;
            const float a = std::clamp(amount, 0.f, 1.f);
            const float drive = 1.0f + 7.0f * a;
            const float pos = a * (kTable - 1);
            const int i = std::min((int) pos, kTable - 2);
            const float makeup = table_[i] + (pos - (float) i) * (table_[i + 1] - table_[i]);
            const float y = makeup * std::tanh(drive * x) / drive;
            const float out = y - x1_ + r_ * y1_;
            x1_ = y; y1_ = out;
            return out;
        }
    private:
        static constexpr int kTable = 33;
        void buildMakeupTable()
        {
            constexpr int N = 512; constexpr double A = 0.2, pi = 3.14159265358979;
            for (int k = 0; k < kTable; ++k)
            {
                const double d = 1.0 + 7.0 * k / (kTable - 1);
                double accOut = 0, accIn = 0;
                for (int n = 0; n < N; ++n)
                {
                    const double x = A * std::sin(2 * pi * n / N);
                    const double y = std::tanh(d * x) / d;
                    accOut += y * y; accIn += x * x;
                }
                table_[k] = (float) std::sqrt(accIn / accOut);
            }
        }
        float table_[kTable] {};
        float r_ = 0.997f, x1_ = 0, y1_ = 0;
    };

    // Опциональный финальный каскад: мягкий потолок. Это вейвшейпер, а не lookahead-лимитер:
    // нулевая задержка, гарантированно |y| < ceiling, ценой нелинейности выше порога.
    inline float softCeiling(float x, float threshold, float ceiling) noexcept
    {
        const float a = std::fabs(x);
        if (a <= threshold) return x;
        const float range = ceiling - threshold;
        const float y = threshold + range * std::tanh((a - threshold) / range);
        return x < 0 ? -y : y;
    }
}
