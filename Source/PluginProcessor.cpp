#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

FrequencyProcessor::FrequencyProcessor()
    : AudioProcessor(BusesProperties()
          .withInput("Input", juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMS", freq::param::createLayout())
{
    pFreq = apvts.getRawParameterValue(freq::param::frequency);
    pReception = apvts.getRawParameterValue(freq::param::reception);
    pNoise = apvts.getRawParameterValue(freq::param::noise);
    pSelectivity = apvts.getRawParameterValue(freq::param::selectivity);
    pTuningSpeed = apvts.getRawParameterValue(freq::param::tuningSpeed);
    pScanSens = apvts.getRawParameterValue(freq::param::scanSensitivity);
    pSaturation = apvts.getRawParameterValue(freq::param::saturation);
    pWidth = apvts.getRawParameterValue(freq::param::width);
    pOutGain = apvts.getRawParameterValue(freq::param::outputGain);
    pMode = apvts.getRawParameterValue(freq::param::mode);
    pMono = apvts.getRawParameterValue(freq::param::mono);
    pLimBypass = apvts.getRawParameterValue(freq::param::limiterBypass);
    pStation = apvts.getRawParameterValue(freq::param::station);

    engine_.prepare(44100.0);
    startTimerHz(10);   // освобождение старых снимков + запись результата поиска в параметр частоты
}

FrequencyProcessor::~FrequencyProcessor() { stopTimer(); }

void FrequencyProcessor::prepareToPlay(double sr, int)
{
    sampleRate_ = sr;
    stations.setSampleRate(sr);      // станции пересчитываются на новую частоту вне аудиопотока
    engine_.prepare(sr);             // выделений памяти внутри нет: всё состояние - в полях класса
}

freq::dsp::EngineParams FrequencyProcessor::readParams() const noexcept
{
    freq::dsp::EngineParams p;
    p.frequency = pFreq->load();           p.reception = pReception->load();
    p.noise = pNoise->load();              p.selectivity = pSelectivity->load();
    p.tuningSpeed = pTuningSpeed->load();  p.scanSensitivity = pScanSens->load();
    p.saturation = pSaturation->load();    p.width = pWidth->load();
    p.outputGainDb = pOutGain->load();
    p.analog = pMode->load() < 0.5f;       p.mono = pMono->load() > 0.5f;
    p.limiterBypass = pLimBypass->load() > 0.5f;
    return p;
}

// message-поток, 10 Гц: освобождение старых снимков; результат поиска/LOCK записывается в параметр частоты,
// чтобы ручка в UI и автоматизация видели ту же частоту, что слышит движок.
void FrequencyProcessor::timerCallback()
{
    stations.reclaim();
    if (engine_.frequencyOwnedByEngine() && engine_.scannerState() != (int) freq::dsp::Scanner::State::Searching
        && engine_.scannerState() != (int) freq::dsp::Scanner::State::Scanning)
    {
        if (auto* prm = apvts.getParameter(freq::param::frequency))
        {
            const float f = engine_.displayFrequency();
            if (std::fabs(pFreq->load() - f) > 0.005f)
                prm->setValueNotifyingHost(prm->convertTo0to1(f));
        }
    }
}

bool FrequencyProcessor::isBusesLayoutSupported(const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
           && out == l.getMainInputChannelSet();
}

void FrequencyProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear(ch, 0, buffer.getNumSamples());

    const int numCh = juce::jmin(buffer.getNumChannels(), 2);
    const int n = buffer.getNumSamples();
    if (numCh <= 0 || n <= 0) return;

    // Единственное обращение к данным станций из аудиопотока: без блокировок и аллокаций.
    const auto* snapshot = stations.acquire();

    // Параметр «Station» (1..16): переход на частоту станции. Меняем только при изменении значения.
    const int stationParam = (int) std::lround(pStation->load());
    if (stationParam != lastStationParam_)
    {
        lastStationParam_ = stationParam;
        if (stationParam >= 1 && stationParam <= freq::kMaxStations && snapshot != nullptr)
        {
            const auto& rt = snapshot->slots[(size_t) (stationParam - 1)];
            if (rt.buffer != nullptr && rt.enabled)
                engine_.tuneTo(rt.freqMHz);
        }
    }

    // Вход плагина не используется: станции - самостоятельные источники (звучат и без входного сигнала).
    // Буфер полностью перезаписывается движком.
    engine_.process(snapshot, readParams(), buffer.getArrayOfWritePointers(), numCh, n);
    for (int ch = numCh; ch < buffer.getNumChannels(); ++ch)
        buffer.clear(ch, 0, n);
}

juce::AudioProcessorEditor* FrequencyProcessor::createEditor() { return new FrequencyEditor(*this); }

void FrequencyProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    juce::ValueTree root("FREQUENCY_STATE");
    root.setProperty("schemaVersion", kStateSchemaVersion, nullptr);
    root.appendChild(apvts.copyState(), nullptr);
    root.appendChild(stations.toValueTree(), nullptr);
    if (auto xml = root.createXml())
        copyXmlToBinary(*xml, dest);
}

void FrequencyProcessor::setStateInformation(const void* data, int size)
{
    auto xml = getXmlFromBinary(data, size);
    if (xml == nullptr) return;
    auto root = juce::ValueTree::fromXml(*xml);
    if (! root.hasType("FREQUENCY_STATE")) return;

    const int version = root.getProperty("schemaVersion", 0);
    if (version > kStateSchemaVersion) return;   // состояние из более новой версии не трогаем
    // if (version < kStateSchemaVersion) { миграции здесь }

    auto params = root.getChildWithName(apvts.state.getType());
    if (params.isValid())
        apvts.replaceState(params);
    stations.fromValueTree(root.getChildWithName("STATIONS"));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new FrequencyProcessor(); }
