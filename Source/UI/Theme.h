#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// Дизайн-система FREQUENCY: графит, тёплый off-white, сдержанный янтарный акцент.
namespace freq::ui::col
{
    inline const juce::Colour bg     { 0xff16171a };
    inline const juce::Colour panel  { 0xff1e2024 };
    inline const juce::Colour raised { 0xff272a2f };
    inline const juce::Colour line   { 0xff34373d };
    inline const juce::Colour text   { 0xffeae5d9 };
    inline const juce::Colour dim    { 0xff8d9096 };
    inline const juce::Colour accent { 0xffd9a441 };
    inline const juce::Colour ok     { 0xff9cc27a };
    inline const juce::Colour warn   { 0xffd9635a };
}

namespace freq::ui
{
    // Явный размер и начертание (withHeight наследует стиль текущего шрифта, поэтому стиль задаём всегда).
    inline void setFont(juce::Graphics& g, float height, bool bold = false)
    {
        g.setFont(g.getCurrentFont().withHeight(height).withStyle(bold ? juce::Font::bold : juce::Font::plain));
    }

    class FrequencyLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        FrequencyLookAndFeel()
        {
            using namespace col;
            setColour(juce::ResizableWindow::backgroundColourId, bg);
            setColour(juce::Label::textColourId, dim);
            setColour(juce::Slider::thumbColourId, accent);
            setColour(juce::TextButton::buttonColourId, raised);
            setColour(juce::TextButton::buttonOnColourId, accent);
            setColour(juce::TextButton::textColourOffId, text);
            setColour(juce::TextButton::textColourOnId, bg);
            setColour(juce::ComboBox::backgroundColourId, raised);
            setColour(juce::ComboBox::textColourId, text);
            setColour(juce::ComboBox::outlineColourId, line);
            setColour(juce::ComboBox::arrowColourId, dim);
            setColour(juce::PopupMenu::backgroundColourId, panel);
            setColour(juce::PopupMenu::textColourId, text);
            setColour(juce::PopupMenu::highlightedBackgroundColourId, accent);
            setColour(juce::PopupMenu::highlightedTextColourId, bg);
            setColour(juce::ToggleButton::textColourId, text);
            setColour(juce::ToggleButton::tickColourId, accent);
            setColour(juce::ToggleButton::tickDisabledColourId, dim);
            setColour(juce::TooltipWindow::backgroundColourId, panel);
            setColour(juce::TooltipWindow::textColourId, text);
            setColour(juce::TooltipWindow::outlineColourId, line);
            setColour(juce::AlertWindow::backgroundColourId, panel);
            setColour(juce::AlertWindow::textColourId, text);
            setColour(juce::AlertWindow::outlineColourId, line);
            setColour(juce::TextEditor::backgroundColourId, bg);
            setColour(juce::TextEditor::textColourId, text);
            setColour(juce::TextEditor::outlineColourId, line);
        }

        void drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos,
                              float a0, float a1, juce::Slider& s) override
        {
            using namespace col;
            const auto b = juce::Rectangle<float>((float) x, (float) y, (float) w, (float) h).reduced(4.f);
            const float r = juce::jmin(b.getWidth(), b.getHeight()) * 0.5f;
            const auto c = b.getCentre();
            const bool big = r > 60.f;
            const float arcW = big ? 6.f : 3.f;
            const float ar = r - arcW;
            const float ang = a0 + pos * (a1 - a0);
            const bool enabled = s.isEnabled();

            juce::Path track; track.addCentredArc(c.x, c.y, ar, ar, 0.f, a0, a1, true);
            g.setColour(line);
            g.strokePath(track, juce::PathStrokeType(arcW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            juce::Path val; val.addCentredArc(c.x, c.y, ar, ar, 0.f, a0, ang, true);
            g.setColour(enabled ? accent : dim);
            g.strokePath(val, juce::PathStrokeType(arcW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            const float br = ar - arcW * 2.2f;
            g.setColour(raised);
            g.fillEllipse(c.x - br, c.y - br, br * 2.f, br * 2.f);
            g.setColour(line);
            g.drawEllipse(c.x - br, c.y - br, br * 2.f, br * 2.f, 1.5f);

            juce::Path p;
            p.addRoundedRectangle(-1.5f, -br + 4.f, 3.f, br * (big ? 0.30f : 0.42f), 1.5f);
            g.setColour(enabled ? text : dim);
            g.fillPath(p, juce::AffineTransform::rotation(ang).translated(c.x, c.y));
        }

        void drawButtonBackground(juce::Graphics& g, juce::Button& bt, const juce::Colour&,
                                  bool highlighted, bool down) override
        {
            using namespace col;
            auto r = bt.getLocalBounds().toFloat().reduced(0.5f);
            const bool on = bt.getToggleState();
            auto fill = on ? accent : raised;
            if (! bt.isEnabled()) fill = panel;
            else if (down) fill = fill.darker(0.2f);
            else if (highlighted) fill = fill.brighter(0.08f);
            g.setColour(fill);
            g.fillRoundedRectangle(r, 5.f);
            g.setColour(on ? accent : line);
            g.drawRoundedRectangle(r, 5.f, 1.f);
        }
    };
}
