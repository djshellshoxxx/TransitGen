#include "PluginEditor.h"

#include <cmath>

namespace tgp {

namespace {
constexpr int kRowH = 26, kMargin = 10, kTopH = 70, kColumns = 2, kWidth = 760;
}

TransitGenEditor::TransitGenEditor(TransitGenProcessor& p) : AudioProcessorEditor(p), proc_(p)
{
    for (juce::AudioProcessorParameter* base : p.getParameters()) {
        auto* param = dynamic_cast<juce::RangedAudioParameter*>(base);
        if (param == nullptr) continue;
        auto row = std::make_unique<ParamRow>();
        row->label.setText(param->getName(64), juce::dontSendNotification);
        row->label.setJustificationType(juce::Justification::centredRight);
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(param)) {
            auto box = std::make_unique<juce::ComboBox>();
            box->addItemList(choice->choices, 1);
            row->combo = std::make_unique<juce::ComboBoxParameterAttachment>(*param, *box);
            row->control = std::move(box);
        } else if (dynamic_cast<juce::AudioParameterBool*>(param) != nullptr) {
            auto button = std::make_unique<juce::ToggleButton>();
            row->button = std::make_unique<juce::ButtonParameterAttachment>(*param, *button);
            row->control = std::move(button);
        } else {
            auto slider = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
            slider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 70, kRowH - 6);
            slider->setTextValueSuffix(param->getLabel().isEmpty() ? juce::String() : " " + param->getLabel());
            row->slider = std::make_unique<juce::SliderParameterAttachment>(*param, *slider);
            row->control = std::move(slider);
        }
        addAndMakeVisible(row->label);
        addAndMakeVisible(*row->control);
        rows_.push_back(std::move(row));
    }

    for (int i = 0; i < static_cast<int>(tg::CurvePreset::kCount); ++i) curveBox_.addItem(tg::kCurvePresetNames[i], i + 1);
    curveBox_.setTextWhenNothingSelected("Custom curve");
    curveBox_.onChange = [this] {
        if (const int id = curveBox_.getSelectedId(); id > 0) proc_.setCurvePreset(id - 1);
    };
    rerollButton_.onClick = [this] { proc_.rerollSeed(); };
    lockButton_.onClick = [this] { proc_.setLocked(lockButton_.getToggleState()); };
    previewButton_.setEnabled(false);   // the engine has no preview hook yet (05: one-shot at the next quantize point)
    previewButton_.setTooltip("Preview arrives with the M4 UI");
    status_.setFont(juce::FontOptions(15.0f, juce::Font::bold));
    for (juce::Component* c : std::initializer_list<juce::Component*>{ &curveBox_, &rerollButton_, &lockButton_, &previewButton_, &status_ })
        addAndMakeVisible(*c);

    const int rowsPerCol = (static_cast<int>(rows_.size()) + kColumns - 1) / kColumns;
    setSize(kWidth, kTopH + rowsPerCol * kRowH + 2 * kMargin);
    timerCallback();
    startTimerHz(30);
}

TransitGenEditor::~TransitGenEditor() { stopTimer(); }

void TransitGenEditor::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
    g.setColour(juce::Colours::white.withAlpha(0.15f));
    g.drawHorizontalLine(kTopH, static_cast<float>(kMargin), static_cast<float>(getWidth() - kMargin));
}

void TransitGenEditor::resized()
{
    juce::Rectangle<int> area = getLocalBounds().reduced(kMargin);
    juce::Rectangle<int> top = area.removeFromTop(kTopH - kMargin);
    juce::Rectangle<int> buttons = top.removeFromTop(28);
    curveBox_.setBounds(buttons.removeFromLeft(180));
    buttons.removeFromLeft(8);
    rerollButton_.setBounds(buttons.removeFromLeft(90));
    buttons.removeFromLeft(8);
    lockButton_.setBounds(buttons.removeFromLeft(70));
    buttons.removeFromLeft(8);
    previewButton_.setBounds(buttons.removeFromLeft(90));
    status_.setBounds(top.reduced(0, 4));

    area.removeFromTop(kMargin);
    const int rowsPerCol = (static_cast<int>(rows_.size()) + kColumns - 1) / kColumns;
    const int colW = area.getWidth() / kColumns;
    for (size_t i = 0; i < rows_.size(); ++i) {
        const int col = static_cast<int>(i) / rowsPerCol, r = static_cast<int>(i) % rowsPerCol;
        juce::Rectangle<int> cell(area.getX() + col * colW, area.getY() + r * kRowH, colW - kMargin, kRowH);
        rows_[i]->label.setBounds(cell.removeFromLeft(120));
        rows_[i]->control->setBounds(cell.reduced(4, 2));
    }
}

void TransitGenEditor::timerCallback()
{
    proc_.collectRetired();
    status_.setText(statusText(proc_), juce::dontSendNotification);

    const bool locked = proc_.isLocked();
    lockButton_.setToggleState(locked, juce::dontSendNotification);
    rerollButton_.setEnabled(!locked);
    const juce::String preset = proc_.curvePresetName();
    int id = 0;
    for (int i = 0; i < static_cast<int>(tg::CurvePreset::kCount); ++i)
        if (preset == tg::kCurvePresetNames[i]) id = i + 1;
    if (curveBox_.getSelectedId() != id) curveBox_.setSelectedId(id, juce::dontSendNotification);
}

juce::String TransitGenEditor::statusText(const TransitGenProcessor& p)
{
    const tg::EngineTelemetry& t = p.telemetry();
    tg::EngineSettings s;
    p.currentSettings(s);
    const bool fixed = p.isLocked() || s.variation == tg::Variation::Fixed;

    juce::String text = p.loadedNewerState() ? "Saved with a newer version - some settings were not loaded.  " : "";
    if (t.fillActive.load(std::memory_order_relaxed)) {
        return text + "FILL  " + juce::String(juce::roundToInt(t.fillProgress.load(std::memory_order_relaxed) * 100.0f))
             + "%   seed " + juce::String(t.fillSeed.load(std::memory_order_relaxed));
    }
    const juce::String seed = "   seed " + juce::String(s.seed) + (fixed ? "" : " (per phrase)");
    if (s.mode == tg::TriggerMode::Automation) return text + "waiting for trigger" + seed;
    if (s.mode == tg::TriggerMode::Midi) return text + "waiting for MIDI" + seed;

    // Auto-Phrase countdown, mirroring FillScheduler::autoPhrase (02 §2).
    const double barLen = p.lastBarLength(), beat = t.beat.load(std::memory_order_relaxed);
    const double P = s.phraseBars * barLen;
    const double o = p.lastGridOrigin() + s.phraseOffset * barLen;
    const double Lf = std::min(tg::fillLengthBeats(s.fillLen, barLen), P);
    double start = o + (std::floor((beat - o) / P) + 1.0) * P - Lf;
    if (start <= beat) start += P;
    const double until = start - beat;
    const int bars = static_cast<int>(std::floor(until / barLen));
    const juce::String when = (bars > 0 ? juce::String(bars) + (bars == 1 ? " bar " : " bars ") : juce::String())
                            + juce::String(until - bars * barLen, 1) + " beats";
    const juce::String transport = t.playing.load(std::memory_order_relaxed) ? "" : "  (stopped)";
    return text + "next fill in " + when + transport + seed;
}

} // namespace tgp
