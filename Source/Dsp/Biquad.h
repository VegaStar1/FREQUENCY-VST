#pragma once
#include <algorithm>
#include <cmath>

namespace freq::dsp
{
    // Фильтры по формулам RBJ Audio EQ Cookbook; форма Transposed Direct Form II (double).
    struct BiquadCoeffs
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;

        // fc ограничивается безопасным диапазоном, Q - тоже: коэффициенты всегда устойчивы.
        static BiquadCoeffs lowpass(double fs, double fc, double q)  { return make(0, fs, fc, q); }
        static BiquadCoeffs highpass(double fs, double fc, double q) { return make(1, fs, fc, q); }
        static BiquadCoeffs bandpass(double fs, double fc, double q) { return make(2, fs, fc, q); } // 0 дБ в пике

    private:
        static BiquadCoeffs make(int type, double fs, double fc, double q)
        {
            constexpr double pi = 3.14159265358979323846;
            fc = std::clamp(fc, 10.0, 0.45 * fs);
            q  = std::clamp(q, 0.2, 20.0);
            const double w0 = 2.0 * pi * fc / fs;
            const double cw = std::cos(w0), alpha = std::sin(w0) / (2.0 * q);
            const double a0 = 1.0 + alpha;
            BiquadCoeffs c;
            if (type == 0)      { c.b0 = (1 - cw) / 2; c.b1 = 1 - cw;     c.b2 = (1 - cw) / 2; }
            else if (type == 1) { c.b0 = (1 + cw) / 2; c.b1 = -(1 + cw);  c.b2 = (1 + cw) / 2; }
            else                { c.b0 = alpha;        c.b1 = 0;          c.b2 = -alpha; }
            c.a1 = -2.0 * cw; c.a2 = 1.0 - alpha;
            c.b0 /= a0; c.b1 /= a0; c.b2 /= a0; c.a1 /= a0; c.a2 /= a0;
            return c;
        }
    };

    class Biquad
    {
    public:
        void setCoeffs(const BiquadCoeffs& c) noexcept { c_ = c; }
        void reset() noexcept { z1_ = z2_ = 0; }
        float process(float xf) noexcept
        {
            const double x = xf;
            const double y = c_.b0 * x + z1_;
            z1_ = c_.b1 * x - c_.a1 * y + z2_;
            z2_ = c_.b2 * x - c_.a2 * y;
            return (float) y;
        }
    private:
        BiquadCoeffs c_;
        double z1_ = 0, z2_ = 0;
    };

    // Однополюсное сглаживание параметров (против «зиппера»).
    class Smoother
    {
    public:
        void prepare(double fs, double timeConstSec) noexcept
        {
            coeff_ = 1.0 - std::exp(-1.0 / (std::max(1.0e-4, timeConstSec) * fs));
        }
        void setImmediate(float v) noexcept { value_ = target_ = v; }
        void setTarget(float t) noexcept { target_ = t; }
        float next() noexcept { value_ += (float) coeff_ * (target_ - value_); return value_; }
        float current() const noexcept { return value_; }
    private:
        double coeff_ = 0.01;
        float value_ = 0, target_ = 0;
    };
}
