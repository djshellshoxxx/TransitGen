// TransitGen M4 — editable energy-curve visualization.
#pragma once

#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace tgp {

class CurveEditor final : public juce::Component, public juce::SettableTooltipClient {
public:
    explicit CurveEditor(TransitGenProcessor& processor);

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

private:
    juce::Point<float> pointToPosition(const tg::CurvePoint&) const;
    juce::Point<float> positionToCurve(juce::Point<float>) const;
    juce::Rectangle<float> plotBounds() const;
    juce::Rectangle<float> curveBounds() const;
    int hitPoint(juce::Point<float>) const;
    void insertPoint(juce::Point<float>);
    void deletePoint(juce::Point<float>);

    TransitGenProcessor& processor_;
    tg::EnergyCurve dragStart_{};
    int draggedPoint_ = -1;
    int bentSegment_ = -1;
    float dragStartY_ = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CurveEditor)
};

} // namespace tgp
