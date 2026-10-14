#include "PluginEditor.h"
#include "UI/UiModel.h"
#include "Parameters.h"
#include "UI/Prompt.h"

namespace
{
    constexpr int kW = 960, kH = 700, kPad = 20;
    constexpr int kSaveId = 100000, kDeleteId = 100001, kRevealId = 100002;

    bool isSupportedAudioFile(const juce::File& f)
    {
        return f.existsAsFile() && freq::ui::isSupportedAudioExtension(f.getFileExtension().toStdString());
    }
}

FrequencyEditor::FrequencyEditor(FrequencyProcessor& p)
    : AudioProcessorEditor(&p), proc_(p), presets_(p.apvts), tuner_(p), strip_(p)
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

    // пресеты
    presetBtn_.setTooltip("Choose a preset. Presets change receiver settings only: not the dial and not your stations.");
    prevPresetBtn_.setTooltip("Previous preset");
    nextPresetBtn_.setTooltip("Next preset");
    savePresetBtn_.setTooltip("Save the current settings as a user preset.");
    presetBtn_.onClick = [this] { showPresetMenu(); };
    prevPresetBtn_.onClick = [this] { stepPreset(-1); };
    nextPresetBtn_.onClick = [this] { stepPreset(+1); };
    savePresetBtn_.onClick = [this] { promptSavePreset(); };
    for (auto* b : { &presetBtn_, &prevPresetBtn_, &nextPresetBtn_, &savePresetBtn_ }) addAndMakeVisible(*b);

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

    const auto name = presets_.currentName();
    const juce::String label = (name.isEmpty() ? juce::String("Init") : name) + (presets_.isModified() ? " *" : "");
    if (label != shownPreset_) { shownPreset_ = label; presetBtn_.setButtonText(label); }

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

    // подсказка при перетаскивании файлов
    if (dragging_)
    {
        const auto r = strip_.getBounds().toFloat().expanded(5.f);
        g.setColour(accent);
        g.drawRoundedRectangle(r, 8.f, 2.f);
        freq::ui::setFont(g, 12.f, true);
        g.drawText("Drop WAV / AIFF files here: onto a slot to replace it, elsewhere to fill empty slots",
                   getLocalBounds().withY(strip_.getBottom() + 6).withHeight(16), juce::Justification::centred);
    }

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
    {
        const int groupW = 28 + 6 + 240 + 6 + 28 + 10 + 60;
        auto g = juce::Rectangle<int>((getWidth() - groupW) / 2, 11, groupW, 34);
        prevPresetBtn_.setBounds(g.removeFromLeft(28)); g.removeFromLeft(6);
        presetBtn_.setBounds(g.removeFromLeft(240));    g.removeFromLeft(6);
        nextPresetBtn_.setBounds(g.removeFromLeft(28)); g.removeFromLeft(10);
        savePresetBtn_.setBounds(g.removeFromLeft(60));
    }

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

// ---------------- пресеты ----------------
void FrequencyEditor::showError(const juce::String& message)
{
    juce::NativeMessageBox::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "FREQUENCY", message);
}

void FrequencyEditor::stepPreset(int delta)
{
    juce::String err;
    if (! presets_.step(delta, err) && err.isNotEmpty()) showError(err);
}

void FrequencyEditor::showPresetMenu()
{
    presets_.refresh();
    const auto entries = presets_.entries();                       // копия: меню асинхронное
    const auto current = presets_.currentName();

    juce::PopupMenu menu, factory, user;
    for (size_t i = 0; i < entries.size(); ++i)
        (entries[i].factory ? factory : user).addItem((int) i + 1, entries[i].name, true, entries[i].name == current);
    menu.addSubMenu("Factory", factory);
    if (user.getNumItems() > 0) menu.addSubMenu("User", user);
    menu.addSeparator();
    menu.addItem(kSaveId, "Save preset as...");
    menu.addItem(kDeleteId, "Delete user preset", presets_.isUserPreset(current));
    menu.addItem(kRevealId, "Show preset folder");

    auto safe = juce::Component::SafePointer<FrequencyEditor>(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&presetBtn_),
        [safe, entries, current](int id)
        {
            if (safe == nullptr || id == 0) return;
            if (id == kSaveId)   { safe->promptSavePreset(); return; }
            if (id == kRevealId)
            {
                const auto dir = safe->presets_.userDirectory();
                dir.createDirectory();
                dir.revealToUser();
                return;
            }
            if (id == kDeleteId)
            {
                juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::QuestionIcon, "Delete preset",
                    "Delete user preset \"" + current + "\"? This cannot be undone.", "Delete", "Cancel", nullptr,
                    juce::ModalCallbackFunction::create([safe, current](int r)
                    {
                        if (safe == nullptr || r != 1) return;
                        juce::String err;
                        if (! safe->presets_.deleteUser(current, err)) showError(err);
                    }));
                return;
            }
            if (id >= 1 && id <= (int) entries.size())
            {
                juce::String err;
                if (! safe->presets_.load(entries[(size_t) id - 1], err)) showError(err);
            }
        });
}

void FrequencyEditor::promptSavePreset()
{
    auto safe = juce::Component::SafePointer<FrequencyEditor>(this);
    const auto current = presets_.currentName();
    freq::ui::promptText("Save preset as", presets_.isUserPreset(current) ? current : juce::String("My Preset"),
        [safe](const juce::String& name)
        {
            if (safe == nullptr || name.trim().isEmpty()) return;
            juce::String err;
            if (! safe->presets_.saveUser(name.trim(), err)) showError(err);
        });
}

// ---------------- перетаскивание файлов ----------------
bool FrequencyEditor::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& f : files)
        if (isSupportedAudioFile(juce::File(f))) return true;
    return false;
}

void FrequencyEditor::updateDropHighlight(int x, int y)
{
    strip_.setDropSlot(strip_.slotAt(strip_.getLocalPoint(this, juce::Point<int>(x, y))));
}

void FrequencyEditor::fileDragEnter(const juce::StringArray&, int x, int y)
{
    dragging_ = true; updateDropHighlight(x, y); repaint();
}
void FrequencyEditor::fileDragMove(const juce::StringArray&, int x, int y) { updateDropHighlight(x, y); }
void FrequencyEditor::fileDragExit(const juce::StringArray&)
{
    dragging_ = false; strip_.setDropSlot(-1); repaint();
}

void FrequencyEditor::filesDropped(const juce::StringArray& files, int x, int y)
{
    const int target = strip_.slotAt(strip_.getLocalPoint(this, juce::Point<int>(x, y)));
    dragging_ = false; strip_.setDropSlot(-1); repaint();

    juce::Array<juce::File> valid;
    for (const auto& f : files)
    {
        const juce::File file(f);
        if (isSupportedAudioFile(file)) valid.add(file);
    }
    const int unsupported = files.size() - valid.size();

    bool occupied[freq::kMaxStations];
    for (int i = 0; i < freq::kMaxStations; ++i)
    {
        const auto& m = proc_.stations.meta(i);
        occupied[i] = m.loaded || m.missing || m.file != juce::File{};
    }
    std::vector<std::pair<int, int>> plan;
    const int noRoom = freq::ui::planDrop(valid.size(), target, occupied, freq::kMaxStations, plan);

    for (const auto& [slot, fileIndex] : plan)
        proc_.stations.loadReplacing(slot, valid[fileIndex]);

    if (unsupported > 0 || noRoom > 0)
    {
        juce::String msg = "Loaded " + juce::String((int) plan.size()) + " file(s).";
        if (unsupported > 0) msg += "\n" + juce::String(unsupported) + " skipped: only WAV and AIFF files are supported.";
        if (noRoom > 0)      msg += "\n" + juce::String(noRoom) + " skipped: all 16 stations are in use (drop onto a slot to replace it).";
        showError(msg);
    }
}
