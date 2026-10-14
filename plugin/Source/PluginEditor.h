// TransitGen plugin — functional M3 editor: generic parameter controls, curve preset, seed and
// telemetry. The real UI (05) replaces it in M4.
#pragma once

#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

namespace tgp {

class TransitGenEditor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit TransitGenEditor(TransitGenProcessor&);
    ~TransitGenEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    /// Telemetry line shown in the status label ("next fill in …" / "FILL" / seed).
    static juce::String statusText(const TransitGenProcessor&);

private:
    void timerCallback() override;

    /// One generic control row: a label plus a slider, combo box or toggle bound to the parameter.
    struct ParamRow {
        juce::Label                                   label;
        std::unique_ptr<juce::Component>              control;
        std::unique_ptr<juce::SliderParameterAttachment>   slider;
        std::unique_ptr<juce::ComboBoxParameterAttachment> combo;
        std::unique_ptr<juce::ButtonParameterAttachment>   button;
    };

    TransitGenProcessor&                   proc_;
    std::vector<std::unique_ptr<ParamRow>> rows_;
    juce::ComboBox                         curveBox_;
    juce::TextButton                       rerollButton_{ "Re-roll" };
    juce::ToggleButton                     lockButton_{ "Lock" };
    juce::TextButton                       previewButton_{ "Preview" };
    juce::Label                            status_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TransitGenEditor)
};

} // namespace tgp
