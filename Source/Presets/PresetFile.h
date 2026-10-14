#pragma once
// Текстовый формат пользовательского пресета и разбор: чистый C++, строгая валидация.
//   FREQUENCY_PRESET 1
//   name=Мой пресет
//   reception=0.8500   ... analog=1  mono=0  limiterBypass=0
#include "PresetValues.h"
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace freq::preset
{
    inline constexpr int kPresetFormatVersion = 1;
    inline constexpr size_t kMaxPresetFileBytes = 65536;
    inline constexpr size_t kMaxNameLength = 64;

    struct ParseResult { bool ok = false; std::string error; std::string name; PresetValues values; };

    inline std::string trim(const std::string& s)
    {
        size_t a = 0, b = s.size();
        while (a < b && std::isspace((unsigned char) s[a])) ++a;
        while (b > a && std::isspace((unsigned char) s[b - 1])) --b;
        return s.substr(a, b - a);
    }

    // Имя пресета: без управляющих символов и разделителей строк, обрезано по длине.
    inline std::string sanitizeName(const std::string& in)
    {
        std::string out;
        for (unsigned char c : in) if (c >= 32 && c != 127) out += (char) c;
        out = trim(out);
        if (out.size() > kMaxNameLength) out.resize(kMaxNameLength);
        return trim(out);
    }

    // Безопасное имя файла для Windows/macOS/Linux (без разделителей пути, зарезервированных имён, хвостовых точек).
    inline std::string sanitizeFileName(const std::string& in)
    {
        std::string out;
        for (unsigned char c : in)
        {
            if (c < 32 || c == 127 || std::string("<>:\"/\\|?*").find((char) c) != std::string::npos) out += '_';
            else out += (char) c;
        }
        while (! out.empty() && (out.back() == ' ' || out.back() == '.')) out.pop_back();
        size_t lead = 0; while (lead < out.size() && out[lead] == ' ') ++lead;
        out = out.substr(lead);
        if (out.size() > kMaxNameLength) out.resize(kMaxNameLength);
        while (! out.empty() && (out.back() == ' ' || out.back() == '.')) out.pop_back();
        if (out.empty()) return "Preset";

        std::string up; for (char c : out) up += (char) std::toupper((unsigned char) c);
        const std::string stem = up.substr(0, up.find('.'));
        static const char* reserved[] = { "CON", "PRN", "AUX", "NUL" };
        bool bad = false;
        for (auto* r : reserved) bad |= (stem == r);
        if ((stem.size() == 4) && (stem.rfind("COM", 0) == 0 || stem.rfind("LPT", 0) == 0) && stem[3] >= '1' && stem[3] <= '9') bad = true;
        return bad ? "_" + out : out;
    }

    inline std::string serializePreset(const std::string& name, const PresetValues& vIn)
    {
        const PresetValues v = clampValues(vIn);
        char b[1024];
        std::snprintf(b, sizeof b,
            "FREQUENCY_PRESET %d\nname=%s\nreception=%.4f\nnoise=%.4f\nselectivity=%.4f\ntuningSpeed=%.4f\n"
            "scanSensitivity=%.4f\nsaturation=%.4f\nwidth=%.4f\noutputGain=%.3f\nanalog=%d\nmono=%d\nlimiterBypass=%d\n",
            kPresetFormatVersion, sanitizeName(name).c_str(), (double) v.reception, (double) v.noise,
            (double) v.selectivity, (double) v.tuningSpeed, (double) v.scanSensitivity, (double) v.saturation,
            (double) v.width, (double) v.outputGainDb, v.analog ? 1 : 0, v.mono ? 1 : 0, v.limiterBypass ? 1 : 0);
        return b;
    }

    namespace detail
    {
        inline bool parseNumber(const std::string& s, float& out)
        {
            if (s.empty()) return false;
            char* end = nullptr; const double d = std::strtod(s.c_str(), &end);
            if (end == s.c_str() || *end != '\0' || ! std::isfinite(d)) return false;
            out = (float) d; return true;
        }
        inline bool parseBool(const std::string& s, bool& out)
        {
            if (s == "1" || s == "true")  { out = true;  return true; }
            if (s == "0" || s == "false") { out = false; return true; }
            return false;
        }
    }

    // Недостающие ключи -> значения по умолчанию; неизвестные -> игнорируются; вне диапазона -> зажимаются;
    // нечисловые/NaN/Inf, плохой заголовок, слишком новая версия или пустое имя -> ошибка.
    inline ParseResult parsePreset(const std::string& text)
    {
        ParseResult r;
        if (text.size() > kMaxPresetFileBytes) { r.error = "file too large"; return r; }

        size_t pos = 0; bool first = true;
        PresetValues v;
        while (pos <= text.size())
        {
            size_t nl = text.find('\n', pos);
            if (nl == std::string::npos) nl = text.size();
            const std::string line = trim(text.substr(pos, nl - pos));
            pos = nl + 1;
            if (line.empty() || line[0] == '#') { if (pos > text.size()) break; continue; }

            if (first)
            {
                first = false;
                const std::string tag = "FREQUENCY_PRESET";
                if (line.compare(0, tag.size(), tag) != 0) { r.error = "not a FREQUENCY preset"; return r; }
                float ver = 0;
                if (! detail::parseNumber(trim(line.substr(tag.size())), ver) || ver < 1.f) { r.error = "bad version"; return r; }
                if (ver > (float) kPresetFormatVersion) { r.error = "preset was saved by a newer version"; return r; }
                continue;
            }
            const size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            const std::string key = trim(line.substr(0, eq)), val = trim(line.substr(eq + 1));

            auto num = [&](float& dst) { if (! detail::parseNumber(val, dst)) { r.error = "bad value for " + key; return false; } return true; };
            auto flag = [&](bool& dst) { if (! detail::parseBool(val, dst)) { r.error = "bad value for " + key; return false; } return true; };

            bool ok = true;
            if      (key == "name")            r.name = sanitizeName(val);
            else if (key == "reception")       ok = num(v.reception);
            else if (key == "noise")           ok = num(v.noise);
            else if (key == "selectivity")     ok = num(v.selectivity);
            else if (key == "tuningSpeed")     ok = num(v.tuningSpeed);
            else if (key == "scanSensitivity") ok = num(v.scanSensitivity);
            else if (key == "saturation")      ok = num(v.saturation);
            else if (key == "width")           ok = num(v.width);
            else if (key == "outputGain")      ok = num(v.outputGainDb);
            else if (key == "analog")          ok = flag(v.analog);
            else if (key == "mono")            ok = flag(v.mono);
            else if (key == "limiterBypass")   ok = flag(v.limiterBypass);
            if (! ok) return r;
            if (pos > text.size()) break;
        }
        if (first)        { r.error = "empty file"; return r; }
        if (r.name.empty()) { r.error = "missing preset name"; return r; }
        r.values = clampValues(v); r.ok = true; return r;
    }
}
