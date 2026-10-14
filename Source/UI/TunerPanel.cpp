#include "TunerPanel.h"
#include "Theme.h"
#include "UiModel.h"
#include "Parameters.h"

namespace freq::ui
{
    TunerPanel::TunerPanel(FrequencyProcessor& p) : proc_(p)
    {
        knob_.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        knob_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        knob_.setRotaryParameters(juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
        knob_.setPopupDisplayEnabled(true, true, this);
        knob_.setTooltip("Tuning. Drag to tune; double-click resets. Mouse wheel works too.");
        addAndMakeVisible(knob_);
        knobAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            proc_.apvts, freq::param::frequency, knob_);
        setTooltip("Drag across the frequency scale to tune (mouse wheel: 0.1 MHz, Shift+wheel: 0.01 MHz).");
        startTimerHz(30);
    }

    void TunerPanel::resized()
    {
        auto r = getLocalBounds().reduced(16);
        const int knobSize = juce::jmin(r.getHeight(), 230);
        knob_.setBounds(r.removeFromLeft(knobSize));
        r.removeFromLeft(20);
        scale_ = r.removeFromBottom(64).toFloat();
        readout_ = r;
    }

    juce::String TunerPanel::currentStationName() const
    {
        float f[freq::kMaxStations]; bool a[freq::kMaxStations];
        for (int i = 0; i < freq::kMaxStations; ++i)
        {
            const auto& m = proc_.stations.meta(i);
            f[i] = m.frequencyMHz; a[i] = m.loaded && m.enabled;
        }
        const int idx = freq::ui::nearestStation(f, a, freq::kMaxStations, shown_, 0.35f);
        return idx >= 0 ? proc_.stations.meta(idx).name : juce::String();
    }

    void TunerPanel::timerCallback()
    {
        shown_ = proc_.displayFrequency();
        quality_ += 0.25f * (proc_.signalQuality() - quality_);
        const int st = proc_.scannerState();                    // 1..3: поиск идёт - ручка следует за сканером
        if ((st == 1 || st == 2 || st == 3) && ! knob_.isMouseButtonDown())
            knob_.setValue(shown_, juce::dontSendNotification);
        repaint();
    }

    void TunerPanel::paint(juce::Graphics& g)
    {
        using namespace col;
        g.setColour(panel);
        g.fillRoundedRectangle(getLocalBounds().toFloat(), 8.f);

        // крупная частота
        auto ro = readout_;
        g.setColour(text);
        setFont(g, 64.f, true);
        g.drawText(juce::String(freq::ui::formatFrequency(shown_)), ro.removeFromTop(72), juce::Justification::centredLeft);
        g.setColour(dim);
        setFont(g, 16.f);
        g.drawText("MHz", ro.removeFromTop(20), juce::Justification::centredLeft);

        // имя станции + статус
        const auto name = currentStationName();
        const bool anyLoaded = [&] { for (int i = 0; i < freq::kMaxStations; ++i) if (proc_.stations.meta(i).loaded) return true; return false; }();
        g.setColour(name.isNotEmpty() ? accent : dim);
        setFont(g, 18.f);
        const juce::String status = ! anyLoaded ? "No stations loaded - click an empty slot below to add a WAV/AIFF file"
                                  : name.isNotEmpty() ? name : "Between stations";
        g.drawText(status, ro.removeFromTop(28), juce::Justification::centredLeft, true);

        // качество приёма
        auto qr = ro.removeFromTop(24).removeFromLeft(220).toFloat();
        g.setColour(dim); setFont(g, 12.f);
        g.drawText("SIGNAL", qr.removeFromLeft(54.f).toNearestInt(), juce::Justification::centredLeft);
        g.setColour(line); g.fillRoundedRectangle(qr.withSizeKeepingCentre(qr.getWidth(), 6.f), 3.f);
        g.setColour(quality_ > 0.6f ? ok : quality_ > 0.25f ? accent : warn);
        g.fillRoundedRectangle(qr.withSizeKeepingCentre(qr.getWidth() * juce::jlimit(0.f, 1.f, quality_), 6.f)
                                 .withX(qr.getX()), 3.f);

        // шкала
        const float L = scale_.getX(), W = scale_.getWidth(), base = scale_.getY() + 36.f;
        g.setColour(line); g.drawLine(L, base, L + W, base, 1.5f);
        setFont(g, 11.f);
        for (int mhz = 88; mhz <= 108; ++mhz)
        {
            const float x = L + W * freq::ui::freqToNorm((float) mhz);
            const bool major = (mhz % 5) == 0;
            g.setColour(major ? dim : line);
            g.drawLine(x, base, x, base - (major ? 12.f : 6.f), 1.f);
            if (major)
            {
                g.setColour(dim);
                g.drawText(juce::String(mhz), juce::Rectangle<float>(x - 20.f, base + 4.f, 40.f, 14.f).toNearestInt(), juce::Justification::centred);
            }
        }
        // маркеры станций
        for (int i = 0; i < freq::kMaxStations; ++i)
        {
            const auto& m = proc_.stations.meta(i);
            if (! (m.loaded || m.missing) || m.frequencyMHz <= 0.f) continue;
            const float x = L + W * freq::ui::freqToNorm(m.frequencyMHz);
            juce::Path tri; tri.addTriangle(x - 5.f, base - 18.f, x + 5.f, base - 18.f, x, base - 8.f);
            if (m.loaded) { g.setColour(m.enabled ? ok : dim); g.fillPath(tri); }
            else          { g.setColour(warn); g.strokePath(tri, juce::PathStrokeType(1.5f)); }
        }
        // индикатор настройки
        const float ix = L + W * freq::ui::freqToNorm(shown_);
        g.setColour(accent);
        g.drawLine(ix, base - 30.f, ix, base + 3.f, 2.f);
    }

    void TunerPanel::setFrequencyFromX(float x)
    {
        const float n = (x - scale_.getX()) / juce::jmax(1.f, scale_.getWidth());
        if (auto* prm = proc_.apvts.getParameter(freq::param::frequency))
            prm->setValueNotifyingHost(prm->convertTo0to1(freq::ui::normToFreq(n)));
    }

    void TunerPanel::mouseDown(const juce::MouseEvent& e)
    {
        if (! scale_.expanded(0.f, 10.f).contains(e.position)) return;
        draggingScale_ = true;
        if (auto* prm = proc_.apvts.getParameter(freq::param::frequency)) prm->beginChangeGesture();
        setFrequencyFromX(e.position.x);
    }

    void TunerPanel::mouseDrag(const juce::MouseEvent& e) { if (draggingScale_) setFrequencyFromX(e.position.x); }

    void TunerPanel::mouseUp(const juce::MouseEvent&)
    {
        if (! draggingScale_) return;
        draggingScale_ = false;
        if (auto* prm = proc_.apvts.getParameter(freq::param::frequency)) prm->endChangeGesture();
    }

    void TunerPanel::nudge(float delta)
    {
        if (auto* prm = proc_.apvts.getParameter(freq::param::frequency))
        {
            const float f = prm->convertFrom0to1(prm->getValue()) + delta;
            prm->beginChangeGesture();
            prm->setValueNotifyingHost(prm->convertTo0to1(juce::jlimit(freq::ui::kFreqLo, freq::ui::kFreqHi, f)));
            prm->endChangeGesture();
        }
    }

    void TunerPanel::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
    {
        if (w.deltaY == 0.f) return;
        nudge((w.deltaY > 0.f ? 1.f : -1.f) * (e.mods.isShiftDown() ? 0.01f : 0.1f));
    }
}
