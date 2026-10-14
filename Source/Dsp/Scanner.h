#pragma once
#include "TuningModel.h"
#include <algorithm>
#include <cmath>

namespace freq::dsp
{
    struct StationPoint { float freqMHz = 0; float strength = 1; bool enabled = false; };

    // Детерминированный автомат поиска станций. Без случайности и без блокировок: вызывается из аудиопотока
    // один раз за блок (dt = длительность блока); при очень больших блоках станция может быть пропущена -
    // шаг ограничен так, чтобы не перепрыгивать самую узкую полосу (см. maxStepMHz).
    //
    // Гистерезис: остановка при силе >= stopThr; новый поиск «взводится» только когда сила упала ниже
    // releaseThr = stopThr/2, поэтому SEARCH со станции не останавливается на ней же сразу.
    class Scanner
    {
    public:
        enum class State { Idle, Searching, Scanning, Settling, Locked };

        void setRange(float minMHz, float maxMHz) { min_ = minMHz; max_ = maxMHz; }
        void setStations(const StationPoint* pts, int n)
        {
            n_ = std::min(n, kMax);
            for (int i = 0; i < n_; ++i) st_[i] = pts[i];
        }
        void setParams(float speed01, float sensitivity01, float selectivity01)
        {
            speed_ = std::clamp(speed01, 0.f, 1.f);
            sens_ = std::clamp(sensitivity01, 0.f, 1.f);
            selectivity_ = std::clamp(selectivity01, 0.f, 1.f);
        }

        void startSearch(float cur, int direction = +1) { begin(State::Searching, cur, direction); }
        void startScan(float cur, int direction = +1)   { begin(State::Scanning, cur, direction); }
        void stop() { if (state_ != State::Locked) state_ = State::Idle; }
        void setLocked(bool on) { state_ = on ? State::Locked : State::Idle; }
        State state() const { return state_; }
        int lastStationIndex() const { return found_; }

        float strengthAt(float f) const
        {
            float best = 0;
            for (int i = 0; i < n_; ++i)
                if (st_[i].enabled)
                    best = std::max(best, stationStrength(f, st_[i].freqMHz, selectivity_, st_[i].strength));
            return best;
        }

        // Возвращает новую частоту настройки.
        float step(float cur, float dt)
        {
            switch (state_)
            {
                case State::Idle:
                case State::Locked: return cur;
                case State::Settling: return settle(cur, dt);
                default: break;
            }

            const float s = strengthAt(cur);
            if (! armed_ && s < 0.5f * stopThr()) armed_ = true;

            if (state_ == State::Searching && armed_ && s >= stopThr())
            {
                found_ = bestStation(cur);
                if (found_ >= 0) { state_ = State::Settling; return settle(cur, dt); }
            }

            float next = cur + (float) dir_ * std::min(speedMHzPerSec() * dt, maxStepMHz());
            const float span = max_ - min_;
            if (next > max_) next -= span;
            if (next < min_) next += span;
            return next;
        }

        float speedMHzPerSec() const { return 0.5f + 9.5f * speed_ * speed_; }
        float stopThr() const { return 0.9f - 0.6f * sens_; }   // высокая чувствительность -> ранняя остановка

    private:
        static constexpr int kMax = 16;

        float maxStepMHz() const { return 0.05f; }
        void begin(State s, float cur, int dir)
        {
            state_ = s; dir_ = dir >= 0 ? 1 : -1; found_ = -1;
            armed_ = strengthAt(cur) < 0.5f * stopThr();
        }
        int bestStation(float f) const
        {
            int best = -1; float bs = 0;
            for (int i = 0; i < n_; ++i)
                if (st_[i].enabled)
                {
                    const float v = stationStrength(f, st_[i].freqMHz, selectivity_, st_[i].strength);
                    if (v > bs) { bs = v; best = i; }
                }
            return best;
        }
        float settle(float cur, float dt)
        {
            const float target = st_[found_].freqMHz;
            const float d = target - cur;
            const float stepMax = std::min(2.0f * dt, maxStepMHz());
            if (std::fabs(d) <= std::max(stepMax, 1.0e-3f)) { state_ = State::Idle; return target; }
            return cur + (d > 0 ? stepMax : -stepMax);
        }

        StationPoint st_[kMax];
        int n_ = 0, found_ = -1, dir_ = 1;
        float min_ = 87.5f, max_ = 108.f, speed_ = 0.5f, sens_ = 0.5f, selectivity_ = 0.5f;
        bool armed_ = true;
        State state_ = State::Idle;
    };
}
