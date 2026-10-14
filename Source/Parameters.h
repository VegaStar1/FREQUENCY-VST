#pragma once
#include "ParamTable.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace freq::param
{
    inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        using juce::NormalisableRange;
        using juce::ParameterID;

        juce::AudioProcessorValueTreeState::ParameterLayout l;

        for (const auto& s : kFloatSpecs)     // порядок и ID - как в ParamTable (стабильны)
            l.add(std::make_unique<juce::AudioParameterFloat>(
                ParameterID{ s.id, 1 }, s.name, NormalisableRange<float>(s.min, s.max, s.step), s.def,
                juce::AudioParameterFloatAttributes().withLabel(s.label)));

        l.add(std::make_unique<juce::AudioParameterChoice>(ParameterID{ mode, 1 }, "Mode",
                                                           juce::StringArray{ "ANALOG", "CLEAN" }, 0));
        l.add(std::make_unique<juce::AudioParameterBool>(ParameterID{ mono, 1 }, "Mono", false));
        l.add(std::make_unique<juce::AudioParameterBool>(ParameterID{ limiterBypass, 1 }, "Limiter Bypass", false));
        l.add(std::make_unique<juce::AudioParameterInt>(ParameterID{ station, 1 }, "Station", 0, kMaxStations, 0));
        return l;
    }
}
