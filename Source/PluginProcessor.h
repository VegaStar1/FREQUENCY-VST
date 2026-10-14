#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Parameters.h"
#include "Engine/RadioEngine.h"
#include "Stations/StationManager.h"

class FrequencyProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    static constexpr int kStateSchemaVersion = 1;

    FrequencyProcessor();
    ~FrequencyProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "FREQUENCY"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;
    freq::StationManager stations;

    // Кнопки SEARCH/SCAN/LOCK/STOP/RESTART: из UI-потока (сами по себе не параметры хоста).
    void sendCommand(freq::dsp::Command c) noexcept { engine_.postCommand(c); }
    float displayFrequency() const noexcept { return engine_.displayFrequency(); }
    float signalQuality() const noexcept { return engine_.signalQuality(); }
    int   scannerState() const noexcept { return engine_.scannerState(); }
    float outputPeak() const noexcept { return engine_.outputPeak(); }
    // Клик по станции в UI: настроиться на её частоту (применяется в начале следующего блока).
    void requestTuneToSlot(int slot) noexcept { pendingTuneSlot_.store(slot); }

private:
    void timerCallback() override;
    freq::dsp::EngineParams readParams() const noexcept;

    freq::dsp::RadioEngine engine_;
    double sampleRate_ = 44100.0;
    int lastStationParam_ = 0;
    std::atomic<int> pendingTuneSlot_ { -1 };

    // Сырые атомарные значения параметров (чтение из аудиопотока без блокировок)
    std::atomic<float> *pFreq = nullptr, *pReception = nullptr, *pNoise = nullptr, *pSelectivity = nullptr,
                       *pTuningSpeed = nullptr, *pScanSens = nullptr, *pSaturation = nullptr, *pWidth = nullptr,
                       *pOutGain = nullptr, *pMode = nullptr, *pMono = nullptr, *pLimBypass = nullptr, *pStation = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FrequencyProcessor)
};
