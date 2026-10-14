#pragma once
#include <cstdint>

namespace freq::dsp
{
    // Детерминированный xorshift32: воспроизводимый шум, без аллокаций и блокировок.
    class Rng
    {
    public:
        explicit Rng(std::uint32_t seed = 0x9E3779B9u) { reseed(seed); }
        void reseed(std::uint32_t seed) { state_ = seed != 0 ? seed : 0x9E3779B9u; }

        std::uint32_t nextU32() noexcept
        {
            state_ ^= state_ << 13;
            state_ ^= state_ >> 17;
            state_ ^= state_ << 5;
            return state_;
        }
        // [-1, 1)
        float nextBipolar() noexcept { return (float) nextU32() * (2.0f / 4294967296.0f) - 1.0f; }
        // [0, 1)
        float nextUnit() noexcept { return (float) (nextU32() >> 8) * (1.0f / 16777216.0f); }

    private:
        std::uint32_t state_ = 1;
    };
}
