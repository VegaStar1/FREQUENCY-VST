#include "RadioEngine.h"
#include <algorithm>
#include <cmath>

namespace freq::dsp
{
    namespace
    {
        constexpr float kTwoPi = 6.28318530717958647692f;

        float fin(float v, float def, float lo, float hi)
        {
            if (! std::isfinite(v)) v = def;
            return std::clamp(v, lo, hi);
        }
        inline float sq(float x) { return x * x; }
    }

    EngineParams EngineParams::sanitized() const
    {
        EngineParams p = *this;
        p.frequency       = fin(frequency, 98.f, kFreqMin, kFreqMax);
        p.reception       = fin(reception, 0.85f, 0.f, 1.f);
        p.noise           = fin(noise, 0.25f, 0.f, 1.f);
        p.selectivity     = fin(selectivity, 0.5f, 0.f, 1.f);
        p.tuningSpeed     = fin(tuningSpeed, 0.5f, 0.f, 1.f);
        p.scanSensitivity = fin(scanSensitivity, 0.5f, 0.f, 1.f);
        p.saturation      = fin(saturation, 0.f, 0.f, 1.f);
        p.width           = fin(width, 1.f, 0.f, 1.f);
        p.outputGainDb    = fin(outputGainDb, 0.f, -60.f, 24.f);
        return p;
    }

    void RadioEngine::prepare(double sampleRate)
    {
        fs_ = (sampleRate >= 1000.0 && sampleRate <= 768000.0) ? sampleRate : 44100.0;
        for (auto& s : slots_) { s = Slot{}; s.gain.prepare(fs_, 0.010); s.gain.setImmediate(0); }
        for (auto* s : { &selSm_, &satSm_, &widthSm_, &monoSm_, &gainSm_ }) s->prepare(fs_, 0.020);
        limSm_.prepare(fs_, 0.020); anSm_.prepare(fs_, 0.050); qSm_.prepare(fs_, 0.025);
        gateSm_.prepare(fs_, 0.050); motionSm_.prepare(fs_, 0.050); tuneSm_.prepare(fs_, 0.05);
        satL_.prepare(fs_); satR_.prepare(fs_);
        noiseL_.prepare(fs_, 0x1234ABCDu); noiseR_.prepare(fs_, 0x7F4A7C15u);
        holdDecay_ = (float) std::exp(-1.0 / (0.002 * fs_));
        scanner_ = Scanner{};
        scanner_.setRange(kFreqMin, kFreqMax);
        resetDsp();
        started_ = false; scanOwned_ = false; scanAccum_ = 0; tickCounter_ = 0;
        pending_.store(0);
        owned_.store(false); scanState_.store(0);
    }

    void RadioEngine::reset() { prepare(fs_); }

    void RadioEngine::resetDsp() noexcept
    {
        hpL_.reset(); hpR_.reset(); lpL_.reset(); lpR_.reset();
        satL_.reset(); satR_.reset();
        lfo1_ = lfo2_ = 0;
    }

    void RadioEngine::tuneTo(float mhz) noexcept
    {
        if (scanner_.state() == Scanner::State::Locked || ! std::isfinite(mhz)) return;
        scanner_.stop();
        heldFreq_ = tuneTarget_ = std::clamp(mhz, kFreqMin, kFreqMax);
        scanOwned_ = true;
    }

    // --- подготовка слотов из снимка (один раз за блок) ---
    void RadioEngine::setupSlots(const StationSnapshot* snap, bool& anyUsable)
    {
        anyUsable = false;
        for (int i = 0; i < kMaxStations; ++i)
        {
            Slot& s = slots_[(size_t) i];
            const StationRuntime& rt = snap ? snap->slots[(size_t) i] : emptyRuntime_;
            const StationBuffer* b = rt.buffer.get();
            const bool usable = b && b->length > 0 && b->numChannels > 0 && b->ch[0].size() == (size_t) b->length
                                && std::fabs(b->sampleRate - fs_) < 0.5;          // несовпадение частоты = станция недоступна
            const std::uint64_t id = usable ? b->id : 0;

            if (id != s.id)      // станция заменена/удалена: новый буфер - с начала, старый вклад затухает за ~2 мс
            {
                s.holdL += s.lastL; s.holdR += s.lastR;
                s.id = id; s.pos = 0; s.done = false;
                s.gain.setImmediate(0);
            }
            s.usable = usable;
            if (usable)
            {
                s.length = b->length;
                s.l = b->ch[0].data();
                s.r = b->numChannels > 1 && b->ch[1].size() == (size_t) b->length ? b->ch[1].data() : s.l;
                s.mode = rt.mode;
                int fade = (int) std::lround((double) std::max(0.f, rt.loopFadeMs) * 0.001 * fs_);
                fade = std::min(fade, s.length / 4);
                if (s.length < 256 || fade < 2) fade = 0;
                s.fade = rt.mode == PlaybackMode::Loop ? fade : 0;
                s.period = rt.mode == PlaybackMode::Loop ? s.length - s.fade : s.length;
                if (s.pos >= s.period)
                {
                    if (rt.mode == PlaybackMode::Loop) s.pos %= s.period; else { s.pos = s.period; s.done = true; }
                }
                s.freq = rt.freqMHz;
                s.strength = std::clamp(std::isfinite(rt.strength) ? rt.strength : 0.f, 0.f, 1.f);
                s.active = rt.enabled && s.freq >= kFreqMin && s.freq <= kFreqMax;
                const float g = std::isfinite(rt.gain) ? std::clamp(rt.gain, 0.f, 8.f) : 1.f;
                s.gain.setTarget(rt.enabled ? g : 0.f);
                anyUsable |= s.active;
            }
            else { s.active = false; s.gain.setTarget(0.f); s.l = s.r = nullptr; }
        }
    }

    // Чтение текущего сэмпла станции (позиция целочисленная: частоты совпадают).
    void RadioEngine::readSlot(const Slot& s, float& l, float& r) const noexcept
    {
        if (s.done) { l = r = 0.f; return; }
        const std::int64_t p = s.pos;
        if (s.mode == PlaybackMode::Loop)
        {
            if (p < s.fade)   // кроссфейд (равная мощность): начало петли плавно проявляется из хвоста
            {
                const float th = (float) (p + 0.5) * (1.5707963f / (float) s.fade);
                const float wh = std::sin(th), wt = std::cos(th);
                const std::int64_t q = p + s.period;                      // хвост: индекс период + p < длина
                l = s.l[p] * wh + s.l[q] * wt;
                r = s.r[p] * wh + s.r[q] * wt;
                return;
            }
            l = s.l[p]; r = s.r[p];
        }
        else
        {
            float g = 1.f;
            const std::int64_t tail = s.length - p;
            if (tail < 64) g = (float) tail / 64.f;                      // короткое затухание в конце one-shot
            l = s.l[p] * g; r = s.r[p] * g;
        }
    }

    void RadioEngine::advance(Slot& s) noexcept
    {
        if (s.done) return;
        if (++s.pos >= s.period)
        {
            if (s.mode == PlaybackMode::Loop) s.pos = 0;
            else { s.pos = s.period; s.done = true; }
        }
    }

    // Обновление «медленных» величин (каждые kTick сэмплов): полоса фильтров, шум, глубина флаттера.
    void RadioEngine::controlTick(const EngineParams& p, float Q, float an) noexcept
    {
        const float Qf = an * Q + (1.f - an) * (0.9f + 0.1f * Q);            // CLEAN почти не портит звук
        const double maxCut = 0.45 * fs_;
        const double lp = std::min(18000.0, maxCut) * std::pow(2500.0 / 18000.0, (double) (1.f - Qf));
        const double hp = 30.0 * std::pow(280.0 / 30.0, (double) (1.f - Qf));
        const auto cl = BiquadCoeffs::lowpass(fs_, lp, 0.707), ch = BiquadCoeffs::highpass(fs_, hp, 0.707);
        lpL_.setCoeffs(cl); lpR_.setCoeffs(cl); hpL_.setCoeffs(ch); hpR_.setCoeffs(ch);

        const float motion = motionSm_.current();
        const float Qn = Q * (1.f - 0.4f * motion * an);                      // шум «настройки» при быстром вращении
        const NoiseLevels a = mapNoiseLevels(p.noise, Qn, true), c = mapNoiseLevels(p.noise, Qn, false);
        NoiseLevels m;
        m.stat = c.stat + an * (a.stat - c.stat);
        m.burst = c.burst + an * (a.burst - c.burst);
        m.crackle = c.crackle + an * (a.crackle - c.crackle);
        noiseL_.setLevels(m); noiseR_.setLevels(m);

        flutterDepth_ = 0.35f * an * std::pow(std::max(0.f, 1.f - Q), 1.5f);   // амплитудная нестабильность, только ANALOG
    }

    void RadioEngine::process(const StationSnapshot* snap, const EngineParams& raw,
                              float* const* out, int numCh, int n) noexcept
    {
        if (out == nullptr || numCh <= 0 || n <= 0) return;
        const EngineParams p = raw.sanitized();

        bool anyUsable = false;
        setupSlots(snap, anyUsable);

        StationPoint pts[kMaxStations];
        for (int i = 0; i < kMaxStations; ++i)
            pts[i] = { slots_[(size_t) i].freq, slots_[(size_t) i].strength, slots_[(size_t) i].active };
        scanner_.setStations(pts, kMaxStations);
        scanner_.setParams(p.tuningSpeed, p.scanSensitivity, p.selectivity);

        // --- управление частотой: ручка, сканер, LOCK ---
        if (! started_)
        {
            lastParam_ = tuneTarget_ = heldFreq_ = prevTune_ = p.frequency;
            tuneSm_.setImmediate(p.frequency);
            selSm_.setImmediate(p.selectivity); satSm_.setImmediate(p.saturation);
            widthSm_.setImmediate(p.width); monoSm_.setImmediate(p.mono ? 1.f : 0.f);
            gainSm_.setImmediate(std::pow(10.f, p.outputGainDb / 20.f));
            limSm_.setImmediate(p.limiterBypass ? 0.f : 1.f); anSm_.setImmediate(p.analog ? 1.f : 0.f);
            gateSm_.setImmediate(anyUsable ? 1.f : 0.f); motionSm_.setImmediate(0.f);
            firstSample_ = true;                       // Q стартует сразу с целевого значения (без «плохого приёма» при загрузке)
            started_ = true;
        }
        else if (std::fabs(p.frequency - lastParam_) > 1e-4f)
        {
            lastParam_ = p.frequency;
            if (scanner_.state() != Scanner::State::Locked)
            {
                // Эхо нашего же результата (UI/таймер записал частоту в параметр) - не прерывание.
                if (! scanOwned_ || std::fabs(p.frequency - heldFreq_) > 0.02f) scanner_.stop();
                scanOwned_ = false;
                tuneTarget_ = p.frequency;
            }
        }

        switch ((Command) pending_.exchange(0, std::memory_order_acq_rel))
        {
            case Command::Search:     scanner_.startSearch(tuneTarget_, +1); heldFreq_ = tuneTarget_; scanOwned_ = true; break;
            case Command::SearchDown: scanner_.startSearch(tuneTarget_, -1); heldFreq_ = tuneTarget_; scanOwned_ = true; break;
            case Command::Scan:       scanner_.startScan(tuneTarget_, +1);   heldFreq_ = tuneTarget_; scanOwned_ = true; break;
            case Command::Stop:       scanner_.stop(); break;
            case Command::Lock:       heldFreq_ = tuneTarget_; scanOwned_ = true; scanner_.setLocked(true); break;
            case Command::Unlock:
                if (scanner_.state() == Scanner::State::Locked) { scanner_.setLocked(false); scanOwned_ = false; tuneTarget_ = p.frequency; lastParam_ = p.frequency; }
                break;
            case Command::Restart:
                for (auto& s : slots_) { s.pos = 0; s.done = false; s.gain.setImmediate(0); }
                break;
            default: break;
        }

        const auto st = scanner_.state();
        if (st == Scanner::State::Searching || st == Scanner::State::Scanning || st == Scanner::State::Settling)
        {
            scanAccum_ += (double) n / fs_;
            constexpr double kStep = 0.004;                                   // фиксированный шаг: результат не зависит от размера блока
            while (scanAccum_ >= kStep)
            {
                heldFreq_ = scanner_.step(heldFreq_, (float) kStep);
                scanAccum_ -= kStep;
                const auto s2 = scanner_.state();
                if (s2 == Scanner::State::Idle || s2 == Scanner::State::Locked) { scanAccum_ = 0; break; }
            }
            scanOwned_ = true;
        }
        else scanAccum_ = 0;
        if (scanOwned_) tuneTarget_ = heldFreq_;

        // --- коэффициенты сглаживания на блок ---
        tuneSm_.prepare(fs_, 0.003 + 0.25 * (double) sq(1.f - p.tuningSpeed));
        tuneSm_.setTarget(tuneTarget_);
        selSm_.setTarget(p.selectivity); satSm_.setTarget(p.saturation);
        widthSm_.setTarget(p.width); monoSm_.setTarget(p.mono ? 1.f : 0.f);
        gainSm_.setTarget(std::pow(10.f, p.outputGainDb / 20.f));
        limSm_.setTarget(p.limiterBypass ? 0.f : 1.f); anSm_.setTarget(p.analog ? 1.f : 0.f);
        gateSm_.setTarget(anyUsable ? 1.f : 0.f);

        const float lfoInc1 = (float) (kTwoPi * 0.7 / fs_), lfoInc2 = (float) (kTwoPi * 2.9 / fs_);
        const float recGain = 0.3f + 0.7f * p.reception;
        float lastQ = qSm_.current();
        float blockPeak = 0.f;

        for (int i = 0; i < n; ++i)
        {
            // 1) настройка и сила станций
            const float tune = tuneSm_.next();
            const float vel = std::fabs(tune - prevTune_) * (float) fs_;      // МГц/с
            prevTune_ = tune;
            motionSm_.setTarget(std::clamp(vel / 6.f, 0.f, 1.f)); motionSm_.next();

            const float bw = selectivityBandwidth(selSm_.next());
            const float invBw2 = 1.f / (bw * bw), reach = 6.f * bw;
            float str[kMaxStations]; float sumSq = 0, maxS = 0;
            for (int k = 0; k < kMaxStations; ++k)
            {
                const Slot& s = slots_[(size_t) k];
                float v = 0;
                if (s.usable && s.active)
                {
                    const float d = tune - s.freq;
                    if (std::fabs(d) < reach) v = s.strength * std::exp(-d * d * invBw2);
                }
                str[k] = v; sumSq += v * v; maxS = std::max(maxS, v);
            }
            // Смешивание: амплитуда = сила станции; если суммарная мощность > 1 - нормировка (равная мощность)
            const float norm = 1.f / std::max(1.f, std::sqrt(sumSq));

            float mixL = 0, mixR = 0;
            for (int k = 0; k < kMaxStations; ++k)
            {
                Slot& s = slots_[(size_t) k];
                const float g = s.gain.next();
                float cl = 0, cr = 0;
                if (s.usable)
                {
                    const float w = str[k] * norm * g;
                    if (w > 1e-7f) { float l, r; readSlot(s, l, r); cl = w * l; cr = w * r; }
                    advance(s);
                }
                s.lastL = cl; s.lastR = cr;
                s.holdL *= holdDecay_; s.holdR *= holdDecay_;
                mixL += cl + s.holdL; mixR += cr + s.holdR;
            }

            // 2) качество приёма
            qSm_.setTarget(maxS * recGain);
            if (firstSample_) { qSm_.setImmediate(maxS * recGain); firstSample_ = false; }
            const float Q = qSm_.next(); lastQ = Q;
            const float an = anSm_.next();
            if (tickCounter_ == 0) controlTick(p, Q, an);
            tickCounter_ = (tickCounter_ + 1) % kTick;

            // 3) нестабильность амплитуды (только ANALOG, глубина от Q)
            float l = mixL, r = mixR;
            if (flutterDepth_ > 0.f)
            {
                lfo1_ += lfoInc1; lfo2_ += lfoInc2;
                if (lfo1_ > kTwoPi) lfo1_ -= kTwoPi;
                if (lfo2_ > kTwoPi) lfo2_ -= kTwoPi;
                const float gf = 1.f - flutterDepth_ * (0.5f + 0.25f * std::sin(lfo1_) + 0.25f * std::sin(lfo2_ + 1.3f));
                l *= gf; r *= gf;
            }

            // 4) полоса приёма -> сатурация -> шум
            l = lpL_.process(hpL_.process(l)); r = lpR_.process(hpR_.process(r));
            const float sat = satSm_.next();
            l = satL_.process(l, sat); r = satR_.process(r, sat);
            const float gate = gateSm_.next();
            l += noiseL_.next() * gate; r += noiseR_.next() * gate;

            // 5) стереобаза: M/S; при плохом приёме сужается (ANALOG); MONO схлопывает S
            const float widthEff = widthSm_.next() * (an * (0.35f + 0.65f * Q) + (1.f - an)) * (1.f - monoSm_.next());
            const float M = 0.5f * (l + r), S = 0.5f * (l - r) * widthEff;
            l = M + S; r = M - S;

            // 6) выход: усиление, мягкий потолок (плавное включение), защита
            const float og = gainSm_.next();
            l *= og; r *= og;
            const float lim = limSm_.next();
            if (lim > 0.f)
            {
                l += lim * (softCeiling(l, 0.8f, 0.98f) - l);
                r += lim * (softCeiling(r, 0.8f, 0.98f) - r);
            }
            if (! (std::isfinite(l) && std::isfinite(r)))
            {
                resetDsp(); l = r = 0.f;                                      // самовосстановление при отравлении состояния
            }
            l = std::clamp(l, -4.f, 4.f); r = std::clamp(r, -4.f, 4.f);
            blockPeak = std::max(blockPeak, std::max(std::fabs(l), std::fabs(r)));

            if (numCh == 1) out[0][i] = 0.5f * (l + r);
            else { out[0][i] = l; out[1][i] = r; }
        }
        for (int c = 2; c < numCh; ++c) std::fill(out[c], out[c] + n, 0.f);

        displayFreq_.store(tuneTarget_, std::memory_order_relaxed);
        quality_.store(lastQ, std::memory_order_relaxed);
        outPeak_.store(blockPeak, std::memory_order_relaxed);
        scanState_.store((int) scanner_.state(), std::memory_order_relaxed);
        owned_.store(scanOwned_, std::memory_order_relaxed);
    }
}
