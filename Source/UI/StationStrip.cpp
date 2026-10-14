#include "StationStrip.h"
#include "Prompt.h"
#include "Theme.h"
#include "UiModel.h"

namespace freq::ui
{
    StationStrip::StationStrip(FrequencyProcessor& p) : proc_(p) { startTimerHz(10); }

    juce::Rectangle<int> StationStrip::slotBounds(int slot) const
    {
        const int gap = 8;
        const int w = (getWidth() - gap * (kCols - 1)) / kCols;
        const int h = (getHeight() - gap * (kRows - 1)) / kRows;
        return { (slot % kCols) * (w + gap), (slot / kCols) * (h + gap), w, h };
    }

    int StationStrip::slotAt(juce::Point<int> pos) const
    {
        for (int i = 0; i < freq::kMaxStations; ++i)
            if (slotBounds(i).contains(pos)) return i;
        return -1;
    }

    void StationStrip::paint(juce::Graphics& g)
    {
        using namespace col;
        float f[freq::kMaxStations]; bool a[freq::kMaxStations];
        for (int i = 0; i < freq::kMaxStations; ++i)
        {
            const auto& m = proc_.stations.meta(i);
            f[i] = m.frequencyMHz; a[i] = m.loaded && m.enabled;
        }
        const int selected = freq::ui::nearestStation(f, a, freq::kMaxStations, proc_.displayFrequency(), 0.35f);

        for (int i = 0; i < freq::kMaxStations; ++i)
        {
            const auto& m = proc_.stations.meta(i);
            const auto r = slotBounds(i).toFloat();
            const bool empty = ! m.loaded && ! m.missing && m.file == juce::File{};
            const bool loading = ! m.loaded && ! m.missing && m.file != juce::File{};

            g.setColour(i == dropSlot_ ? accent.withAlpha(0.18f) : (i == selected ? raised.brighter(0.08f) : panel));
            g.fillRoundedRectangle(r, 6.f);
            g.setColour((i == selected || i == dropSlot_) ? accent : (m.missing ? warn : line));
            g.drawRoundedRectangle(r.reduced(0.5f), 6.f, (i == selected || i == dropSlot_) ? 1.5f : 1.f);

            auto inner = slotBounds(i).reduced(10, 8);
            g.setColour(dim);
            setFont(g, 10.f);
            g.drawText(juce::String(i + 1).paddedLeft('0', 2), inner.removeFromTop(12), juce::Justification::centredLeft);

            if (empty)
            {
                g.setColour(line);
                setFont(g, 26.f);
                g.drawText("+", slotBounds(i), juce::Justification::centred);
                continue;
            }
            if (loading)
            {
                g.setColour(dim); setFont(g, 12.f);
                g.drawText("Loading...", inner, juce::Justification::centred);
                continue;
            }
            g.setColour(m.missing ? warn : (m.enabled ? text : dim));
            setFont(g, 13.f, true);
            g.drawText(m.missing ? (m.loadFailed ? "CAN'T READ FILE" : "MISSING FILE") : m.name, inner.removeFromTop(18), juce::Justification::centredLeft, true);
            g.setColour(m.missing ? dim : accent);
            setFont(g, 12.f);
            g.drawText(juce::String(freq::ui::formatFrequency(m.frequencyMHz)) + " MHz", inner.removeFromTop(16), juce::Justification::centredLeft);
            g.setColour(dim);
            setFont(g, 11.f);
            const juce::String info = m.missing ? m.file.getFileName()
                : juce::String(freq::ui::formatDuration(m.durationSec)) + "  " + juce::String(m.fileRate / 1000.0, 1) + "k  "
                  + (m.mode == freq::PlaybackMode::Loop ? "LOOP" : "1-SHOT");
            g.drawText(info, inner, juce::Justification::centredLeft, true);
        }
    }

    juce::String StationStrip::getTooltip()
    {
        const int i = slotAt(getMouseXYRelative());
        if (i < 0) return {};
        const auto& m = proc_.stations.meta(i);
        if (m.missing && m.loadFailed) return "This file could not be read (unsupported or damaged): " + m.file.getFullPathName() + "\nRight-click > Replace file...";
        if (m.missing) return "File not found: " + m.file.getFullPathName() + "\nRight-click > Replace file...";
        if (m.loaded)  return m.file.getFullPathName() + "\nClick to tune. Right-click for options.";
        if (m.file != juce::File{}) return "Loading...";
        return "Click to load a WAV or AIFF file into this station.";
    }

    void StationStrip::mouseDown(const juce::MouseEvent& e)
    {
        const int slot = slotAt(e.getPosition());
        if (slot < 0) return;
        const auto& m = proc_.stations.meta(slot);
        const bool occupied = m.loaded || m.missing;

        if (! occupied)                    { chooseFile(slot); return; }
        if (e.mods.isPopupMenu())          { showMenu(slot); return; }
        if (m.loaded)                      proc_.requestTuneToSlot(slot);
        else                               showMenu(slot);               // missing: сразу меню (заменить/удалить)
    }

    void StationStrip::chooseFile(int slot)
    {
        chooser_ = std::make_unique<juce::FileChooser>("Load audio into station " + juce::String(slot + 1),
                                                       juce::File::getSpecialLocation(juce::File::userMusicDirectory),
                                                       "*.wav;*.aif;*.aiff");
        auto safe = juce::Component::SafePointer<StationStrip>(this);
        chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [safe, slot](const juce::FileChooser& fc)
            {
                if (safe == nullptr) return;
                const auto file = fc.getResult();
                if (file == juce::File{}) return;                       // отмена
                safe->proc_.stations.loadReplacing(slot, file);
            });
    }

    void StationStrip::showMenu(int slot)
    {
        auto& m = proc_.stations.meta(slot);
        juce::PopupMenu menu;
        menu.addItem(1, "Replace file...");
        menu.addItem(2, "Remove station");
        menu.addSeparator();
        menu.addItem(3, "Rename...", ! m.missing);
        menu.addItem(4, "Set frequency...", true);
        menu.addItem(5, "Loop", true, m.mode == freq::PlaybackMode::Loop);
        menu.addItem(6, "One-shot", true, m.mode == freq::PlaybackMode::OneShot);
        menu.addItem(7, "Enabled", true, m.enabled);
        menu.addSeparator();
        menu.addItem(8, "Restart playback");

        auto safe = juce::Component::SafePointer<StationStrip>(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
            [safe, slot](int id)
            {
                if (safe == nullptr || id == 0) return;
                auto& proc = safe->proc_;
                auto& meta = proc.stations.meta(slot);
                switch (id)
                {
                    case 1: safe->chooseFile(slot); break;
                    case 2: proc.stations.removeStation(slot); break;
                    case 3:
                        freq::ui::promptText("Rename station", meta.name, [safe, slot](const juce::String& t)
                        {
                            if (safe == nullptr || t.trim().isEmpty()) return;
                            safe->proc_.stations.meta(slot).name = t.trim();
                        });
                        break;
                    case 4:
                        freq::ui::promptText("Frequency (MHz, 87.5 - 108.0)", juce::String(meta.frequencyMHz, 1),
                            [safe, slot](const juce::String& t)
                        {
                            if (safe == nullptr || t.trim().isEmpty()) return;
                            const float f = juce::jlimit(freq::ui::kFreqLo, freq::ui::kFreqHi, t.getFloatValue());
                            safe->proc_.stations.meta(slot).frequencyMHz = f;
                            safe->proc_.stations.applyMeta(slot);
                        });
                        break;
                    case 5: meta.mode = freq::PlaybackMode::Loop;    proc.stations.applyMeta(slot); break;
                    case 6: meta.mode = freq::PlaybackMode::OneShot; proc.stations.applyMeta(slot); break;
                    case 7: meta.enabled = ! meta.enabled;           proc.stations.applyMeta(slot); break;
                    case 8: proc.sendCommand(freq::dsp::Command::Restart); break;
                    default: break;
                }
            });
    }
}
