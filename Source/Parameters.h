#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

// Стабильные ID параметров. НИКОГДА не переименовывать после релиза: на них держится автоматизация в DAW.
// Версия в ParameterID - для будущей миграции (меняется только при несовместимом изменении смысла).
namespace freq::param
{
    inline constexpr const char* frequency       = "frequency";
    inline constexpr const char* reception       = "reception";
    inline constexpr const char* noise           = "noise";
    inline constexpr const char* selectivity     = "selectivity";
    inline constexpr const char* tuningSpeed     = "tuningSpeed";
    inline constexpr const char* scanSensitivity = "scanSensitivity";
    inline constexpr const char* saturation      = "saturation";
    inline constexpr const char* width           = "width";
    inline constexpr const char* outputGain      = "outputGain";
    inline constexpr const char* mode            = "mode";          // 0 = ANALOG, 1 = CLEAN
    inline constexpr const char* mono            = "mono";
    inline constexpr const char* limiterBypass   = "limiterBypass";
    inline constexpr const char* station         = "station";       // 0 = нет, 1..16

    inline constexpr float kFreqMin = 87.5f;
    inline constexpr float kFreqMax = 108.0f;
    inline constexpr int   kMaxStations = 16;

    inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        using APF = juce::AudioParameterFloat;
        using APB = juce::AudioParameterBool;
        using APC = juce::AudioParameterChoice;
        using API = juce::AudioParameterInt;
        using juce::NormalisableRange;
        using juce::ParameterID;

        juce::AudioProcessorValueTreeState::ParameterLayout l;

        l.add(std::make_unique<APF>(ParameterID{ frequency, 1 }, "Frequency",
                                    NormalisableRange<float>(kFreqMin, kFreqMax, 0.01f), 98.0f));
        l.add(std::make_unique<APF>(ParameterID{ reception, 1 }, "Reception",
                                    NormalisableRange<float>(0.f, 1.f, 0.001f), 0.85f));
        l.add(std::make_unique<APF>(ParameterID{ noise, 1 }, "Noise",
                                    NormalisableRange<float>(0.f, 1.f, 0.001f), 0.25f));
        l.add(std::make_unique<APF>(ParameterID{ selectivity, 1 }, "Selectivity",
                                    NormalisableRange<float>(0.f, 1.f, 0.001f), 0.5f));
        l.add(std::make_unique<APF>(ParameterID{ tuningSpeed, 1 }, "Tuning Speed",
                                    NormalisableRange<float>(0.f, 1.f, 0.001f), 0.5f));
        l.add(std::make_unique<APF>(ParameterID{ scanSensitivity, 1 }, "Scan Sensitivity",
                                    NormalisableRange<float>(0.f, 1.f, 0.001f), 0.5f));
        l.add(std::make_unique<APF>(ParameterID{ saturation, 1 }, "Saturation",
                                    NormalisableRange<float>(0.f, 1.f, 0.001f), 0.2f));
        l.add(std::make_unique<APF>(ParameterID{ width, 1 }, "Stereo Width",
                                    NormalisableRange<float>(0.f, 1.f, 0.001f), 1.0f));
        l.add(std::make_unique<APF>(ParameterID{ outputGain, 1 }, "Output Gain",
                                    NormalisableRange<float>(-24.f, 12.f, 0.1f), 0.0f,
                                    juce::AudioParameterFloatAttributes().withLabel("dB")));
        l.add(std::make_unique<APC>(ParameterID{ mode, 1 }, "Mode",
                                    juce::StringArray{ "ANALOG", "CLEAN" }, 0));
        l.add(std::make_unique<APB>(ParameterID{ mono, 1 }, "Mono", false));
        l.add(std::make_unique<APB>(ParameterID{ limiterBypass, 1 }, "Limiter Bypass", false));
        l.add(std::make_unique<API>(ParameterID{ station, 1 }, "Station", 0, kMaxStations, 0));
        return l;
    }
}
