#include "PluginEditor.h"
#include "UI/UiModel.h"
#include "Parameters.h"

namespace
{
    constexpr int kW = 960, kH = 700, kPad = 20;
}

FrequencyEditor::FrequencyEditor(FrequencyProcessor& p)
    : AudioProcessorEditor(&p), proc_(p), tuner_(p), strip_(p)
{
    setLookAndFeel(&laf_);
    setSize(kW, kH);
    setResizable(false, false);

    addAndMakeVisible(tuner_);
    addAndMakeVisible(strip_);

    namespace pm = freq::param;
    addKnob(knobs_[0], pm::reception,       "RECEPTION",  "Receiver quality: how clean the station sounds when tuned in.");
    addKnob(knobs_[1], pm::noise,           "NOISE",      "Amount of static, interference and crackle between and around stations.");
    addKnob(knobs_[2], pm::selectivity,     "SELECTIVITY","How narrow each station is on the dial. Higher = stations are easier to miss.");
    addKnob(knobs_[3], pm::tuningSpeed,     "TUNING",     "How fast the dial glides and how fast SEARCH/SCAN move.");
    addKnob(knobs_[4], pm::scanSensitivity, "SCAN SENS",  "How strong a signal must be for SEARCH to stop on it.");
    addKnob(knobs_[5], pm::saturation,      "SATURATION", "Warm receiver saturation (loudness-matched).");
    addKnob(knobs_[6], pm::width,           "WIDTH",      "Stereo width. Poor reception narrows the image in ANALOG mode.");
    addKnob(knobs_[7], pm::outputGain,      "OUTPUT",     "Output level in dB.");

    searchBtn_.setTooltip("Scan upward and stop on the next station.");
    scanBtn_.setTooltip("Scan the whole dial continuously until STOP.");
    stopBtn_.setTooltip("Stop searching or scanning.");
    lockBtn_.setTooltip("Hold the current frequency (the dial is ignored until unlocked).");
    searchBtn_.onClick = [this] { proc_.sendCommand(freq::dsp::Command::Search); };
    scanBtn_.onClick   = [this] { proc_.sendCommand(freq::dsp::Command::Scan); };
    stopBtn_.onClick   = [this] { proc_.sendCommand(freq::dsp::Command::Stop); };
    lockBtn_.onClick   = [this]
    {
        const bool locked = proc_.scannerState() == (int) freq::dsp::Scanner::State::Locked;
        proc_.sendCommand(locked ? freq::dsp::Command::Unlock : freq::dsp::Command::Lock);
    };
    for (auto* b : { &searchBtn_, &scanBtn_, &stopBtn_, &lockBtn_ }) addAndMakeVisible(*b);

    modeBox_.addItemList({ "ANALOG", "CLEAN" }, 1);
    modeBox_.setTooltip("ANALOG: imperfect, noisy reception. CLEAN: controlled, minimal noise.");
    addAndMakeVisible(modeBox_);
    modeAtt_ = std::make_unique<ComboAtt>(proc_.apvts, pm::mode, modeBox_);

    monoBtn_.setTooltip("Sum the output to mono.");
    limBypassBtn_.setTooltip("Bypass the optional soft output ceiling.");
    addAndMakeVisible(monoBtn_);
    addAndMakeVisible(limBypassBtn_);
    monoAtt_ = std::make_unique<ButtonAtt>(proc_.apvts, pm::mono, monoBtn_);
    limAtt_  = std::make_unique<ButtonAtt>(proc_.apvts, pm::limiterBypass, limBypassBtn_);

    startTimerHz(20);
}

FrequencyEditor::~FrequencyEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);     // L&F живёт в этом классе: снимаем до разрушения
}

void FrequencyEditor::addKnob(Knob& k, const char* paramId, const juce::String& text, const juce::String& tip)
{
    k.slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    k.slider.setRotaryParameters(juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
    k.slider.setPopupDisplayEnabled(true, true, this);
    k.slider.setTooltip(tip + " Double-click resets.");
    k.label.setText(text, juce::dontSendNotification);
    k.label.setJustificationType(juce::Justification::centred);
    k.label.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(k.slider);
    addAndMakeVisible(k.label);
    k.att = std::make_unique<SliderAtt>(proc_.apvts, paramId, k.slider);
}

void FrequencyEditor::timerCallback()
{
    const int st = proc_.scannerState();
    using S = freq::dsp::Scanner::State;
    searchBtn_.setToggleState(st == (int) S::Searching || st == (int) S::Settling, juce::dontSendNotification);
    scanBtn_.setToggleState(st == (int) S::Scanning, juce::dontSendNotification);
    lockBtn_.setToggleState(st == (int) S::Locked, juce::dontSendNotification);

    const float level = freq::ui::meterNorm(proc_.outputPeak());
    meter_ = level > meter_ ? level : meter_ * 0.88f;           // быстрая атака, плавный спад
    repaint(meterArea_);
}

void FrequencyEditor::paint(juce::Graphics& g)
{
    using namespace freq::ui::col;
    g.fillAll(bg);

    // верхняя панель
    auto top = getLocalBounds().removeFromTop(56).reduced(kPad, 0);
    g.setColour(text);
    freq::ui::setFont(g, 22.f, true);
    g.drawText("FREQUENCY", top.removeFromLeft(240), juce::Justification::centredLeft);
    int loaded = 0;
    for (int i = 0; i < freq::kMaxStations; ++i) loaded += proc_.stations.meta(i).loaded ? 1 : 0;
    g.setColour(freq::ui::col::dim);
    freq::ui::setFont(g, 12.f);
    g.drawText(juce::String(loaded) + " / 16 stations  |  v" + juce::String(JucePlugin_VersionString),
               top, juce::Justification::centredRight);
    g.setColour(line);
    g.drawLine((float) kPad, 56.f, (float) (getWidth() - kPad), 56.f, 1.f);

    // индикатор уровня
    g.setColour(line);
    g.fillRoundedRectangle(meterArea_.toFloat(), 3.f);
    const float w = (float) meterArea_.getWidth() * juce::jlimit(0.f, 1.f, meter_);
    g.setColour(meter_ > 0.97f ? warn : meter_ > 0.85f ? accent : ok);
    g.fillRoundedRectangle(meterArea_.toFloat().withWidth(w), 3.f);
    g.setColour(freq::ui::col::dim);
    freq::ui::setFont(g, 10.f);
    g.drawText("OUT", meterArea_.withY(meterArea_.getBottom() + 2).withHeight(12), juce::Justification::centredLeft);
}

void FrequencyEditor::resized()
{
    auto area = getLocalBounds().reduced(kPad, 0);
    area.removeFromTop(56 + 14);

    tuner_.setBounds(area.removeFromTop(260));
    area.removeFromTop(14);
    strip_.setBounds(area.removeFromTop(150));
    area.removeFromTop(16);

    // транспорт и переключатели
    auto row = area.removeFromTop(34);
    for (auto* b : { &searchBtn_, &scanBtn_, &stopBtn_, &lockBtn_ })
    {
        b->setBounds(row.removeFromLeft(96));
        row.removeFromLeft(8);
    }
    row.removeFromLeft(16);
    modeBox_.setBounds(row.removeFromLeft(120));
    row.removeFromLeft(14);
    monoBtn_.setBounds(row.removeFromLeft(70));
    limBypassBtn_.setBounds(row.removeFromLeft(150));
    meterArea_ = row.removeFromRight(150).withTrimmedTop(11).withHeight(8);

    area.removeFromTop(12);

    // ручки
    auto knobRow = area.removeFromTop(104);
    const int cw = knobRow.getWidth() / (int) knobs_.size();
    for (auto& k : knobs_)
    {
        auto c = knobRow.removeFromLeft(cw);
        k.label.setBounds(c.removeFromBottom(18));
        k.slider.setBounds(c.reduced(6, 0));
    }
}
