#include "CurveEditor.h"

#include "../ui/Theme.h"

#include <algorithm>
#include <cmath>

namespace tgp {

namespace {
constexpr float kInset = 16.0f;
constexpr float kPointRadius = 6.0f;
constexpr float kMinimumPointGap = 0.005f;
}

CurveEditor::CurveEditor(TransitGenProcessor& processor) : processor_(processor)
{
    setName("Energy curve editor");
    setTooltip("Drag points to shape fill energy. Double-click a point to remove it; Alt-drag a segment to bend it; click to add a point.");
}

juce::Point<float> CurveEditor::pointToPosition(const tg::CurvePoint& p) const
{
    const auto bounds = curveBounds();
    return { bounds.getX() + p.t * bounds.getWidth(), bounds.getBottom() - p.value * bounds.getHeight() };
}

juce::Rectangle<float> CurveEditor::plotBounds() const
{
    auto bounds = getLocalBounds().toFloat().reduced(kInset);
    bounds.removeFromTop(14.0f);
    return bounds;
}

juce::Rectangle<float> CurveEditor::curveBounds() const
{
    auto bounds = plotBounds();
    bounds.setHeight(bounds.getHeight() * 0.58f);
    return bounds;
}

juce::Point<float> CurveEditor::positionToCurve(juce::Point<float> position) const
{
    const auto bounds = curveBounds();
    if (bounds.getWidth() <= 0.0f || bounds.getHeight() <= 0.0f) return {};
    const float t = std::clamp((position.x - bounds.getX()) / bounds.getWidth(), 0.0f, 1.0f);
    const float value = std::clamp((bounds.getBottom() - position.y) / bounds.getHeight(), 0.0f, 1.0f);
    return { t, value };
}

int CurveEditor::hitPoint(juce::Point<float> position) const
{
    const tg::EnergyCurve curve = processor_.energyCurve();
    for (int i = 0; i < curve.numPoints; ++i)
        if (pointToPosition(curve.points[i]).getDistanceFrom(position) <= kPointRadius + 5.0f) return i;
    return -1;
}

void CurveEditor::paint(juce::Graphics& g)
{
    using namespace ui;
    const auto outer = getLocalBounds().toFloat();
    g.setColour(panel);
    g.fillRoundedRectangle(outer, 6.0f);
    g.setColour(border);
    g.drawRoundedRectangle(outer.reduced(0.5f), 6.0f, 1.0f);

    auto plot = plotBounds();
    if (plot.getWidth() <= 0.0f || plot.getHeight() <= 0.0f) return;
    g.setColour(text.withAlpha(0.8f));
    g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    g.drawText("ENERGY CURVE", getLocalBounds().reduced(8, 3).removeFromTop(16), juce::Justification::centredLeft);

    const auto curveArea = curveBounds();
    for (int i = 0; i <= 4; ++i) {
        const float y = curveArea.getY() + curveArea.getHeight() * static_cast<float>(i) / 4.0f;
        const float x = curveArea.getX() + curveArea.getWidth() * static_cast<float>(i) / 4.0f;
        g.setColour(grid);
        g.drawHorizontalLine(juce::roundToInt(y), curveArea.getX(), curveArea.getRight());
        g.drawVerticalLine(juce::roundToInt(x), curveArea.getY(), plot.getBottom());
    }

    const tg::EnergyCurve curve = processor_.energyCurve();
    if (!tg::isValidCurve(curve)) return;
    juce::Path path;
    constexpr int kSamples = 128;
    for (int i = 0; i <= kSamples; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(kSamples);
        const float value = static_cast<float>(curve.evaluate(t));
        const juce::Point<float> p{ curveArea.getX() + t * curveArea.getWidth(), curveArea.getBottom() - value * curveArea.getHeight() };
        if (i == 0) path.startNewSubPath(p);
        else path.lineTo(p);
    }
    g.setColour(accent);
    g.strokePath(path, juce::PathStrokeType(2.0f));
    for (int i = 0; i < curve.numPoints; ++i) {
        const auto p = pointToPosition(curve.points[i]);
        g.setColour(i == 0 || i == curve.numPoints - 1 ? secondary : text);
        g.fillEllipse(p.x - kPointRadius, p.y - kPointRadius, kPointRadius * 2.0f, kPointRadius * 2.0f);
        g.setColour(background);
        g.drawEllipse(p.x - kPointRadius, p.y - kPointRadius, kPointRadius * 2.0f, kPointRadius * 2.0f, 1.0f);
    }

    tg::FillPlan plan{};
    if (!processor_.previewPlan(plan) || plan.lengthBeats <= 0.0) return;
    auto lanes = plot.withY(curveArea.getBottom()).withHeight(plot.getBottom() - curveArea.getBottom());
    constexpr float labelWidth = 42.0f;
    auto laneContent = lanes.withTrimmedLeft(labelWidth);
    const float laneHeight = lanes.getHeight() / 4.0f;
    const char* laneNames[] = { "SOURCE", "GATE", "CRUSH", "FILTER" };
    for (int lane = 0; lane < 4; ++lane) {
        const float y = lanes.getY() + laneHeight * static_cast<float>(lane);
        g.setColour(grid);
        g.drawHorizontalLine(juce::roundToInt(y), lanes.getX(), lanes.getRight());
        g.setColour(muted);
        g.setFont(juce::FontOptions(8.0f, juce::Font::bold));
        g.drawText(laneNames[lane], juce::Rectangle<int>(juce::roundToInt(lanes.getX()), juce::roundToInt(y),
                                                          juce::roundToInt(labelWidth - 4.0f), juce::roundToInt(laneHeight)),
                   juce::Justification::centredLeft, false);
    }
    const float scaleX = laneContent.getWidth() / static_cast<float>(plan.lengthBeats);
    for (int i = 0; i < plan.numEvents; ++i) {
        const tg::FillEvent& event = plan.events[i];
        const int lane = static_cast<int>(event.lane);
        if (lane < 0 || lane > 2) continue;
        const float y = lanes.getY() + laneHeight * static_cast<float>(lane);
        const float x = laneContent.getX() + static_cast<float>(event.startBeat) * scaleX;
        const float width = std::max(1.0f, static_cast<float>(event.lengthBeats) * scaleX - 1.0f);
        juce::Colour colour = muted;
        if (event.lane == tg::Lane::Source) {
            const int savedType = static_cast<int>(event.type);
            const tg::EventType type = savedType >= 0 && savedType <= static_cast<int>(tg::EventType::Silence)
                                            ? event.type : tg::EventType::Pass;
            switch (type) {
                case tg::EventType::Stutter: colour = accent; break;
                case tg::EventType::Reverse: colour = laneReverse; break;
                case tg::EventType::TapeStop:
                case tg::EventType::TapeStart: colour = laneTape; break;
                case tg::EventType::Silence: colour = laneSilence; break;
                default: colour = muted.withAlpha(0.35f); break;
            }
        } else if (event.lane == tg::Lane::Gate) colour = laneGate;
        else colour = laneCrush;
        g.setColour(colour);
        g.fillRoundedRectangle(x, y + 3.0f, width, std::max(1.0f, laneHeight - 6.0f), 2.0f);
    }

    const tg::AutomationLane& cutoff = plan.lanes[static_cast<int>(tg::AutoTarget::FilterCutoff)];
    if (cutoff.numPoints >= 2) {
        juce::Path filterPath;
        for (int i = 0; i < cutoff.numPoints; ++i) {
            const tg::LanePoint& point = cutoff.points[i];
            const float x = laneContent.getX() + static_cast<float>(point.beat) * scaleX;
            const float y = lanes.getY() + laneHeight * 3.0f + laneHeight * (1.0f - point.value);
            if (i == 0) filterPath.startNewSubPath(x, y);
            else filterPath.lineTo(x, y);
        }
        g.setColour(text);
        g.strokePath(filterPath, juce::PathStrokeType(1.5f));
    }
}

void CurveEditor::insertPoint(juce::Point<float> position)
{
    tg::EnergyCurve curve = processor_.energyCurve();
    if (curve.numPoints >= tg::kMaxCurvePoints) return;
    const auto normalized = positionToCurve(position);
    int index = 1;
    while (index < curve.numPoints && curve.points[index].t < normalized.x) ++index;
    if (index <= 0 || index >= curve.numPoints) return;
    const float lo = curve.points[index - 1].t + kMinimumPointGap;
    const float hi = curve.points[index].t - kMinimumPointGap;
    if (hi < lo) return;
    const float t = std::clamp(normalized.x, lo, hi);
    for (int i = curve.numPoints; i > index; --i) curve.points[i] = curve.points[i - 1];
    curve.points[index] = { t, normalized.y, 0.0f };
    curve.points[index - 1].curve = 0.0f;
    ++curve.numPoints;
    processor_.setEnergyCurve(curve);
    repaint();
}

void CurveEditor::deletePoint(juce::Point<float> position)
{
    tg::EnergyCurve curve = processor_.energyCurve();
    const int index = hitPoint(position);
    if (index <= 0 || index >= curve.numPoints - 1) return;
    for (int i = index; i + 1 < curve.numPoints; ++i) curve.points[i] = curve.points[i + 1];
    --curve.numPoints;
    processor_.setEnergyCurve(curve);
    repaint();
}

void CurveEditor::mouseDown(const juce::MouseEvent& event)
{
    dragStart_ = processor_.energyCurve();
    draggedPoint_ = hitPoint(event.position);
    bentSegment_ = -1;
    dragStartY_ = event.position.y;
    if (draggedPoint_ >= 0 && event.mods.isAltDown()) draggedPoint_ = -1;
    if (draggedPoint_ < 0 && event.mods.isAltDown()) {
        const auto normalized = positionToCurve(event.position);
        for (int i = 0; i + 1 < dragStart_.numPoints; ++i)
            if (normalized.x >= dragStart_.points[i].t && normalized.x <= dragStart_.points[i + 1].t) {
                bentSegment_ = i;
                break;
            }
    } else if (draggedPoint_ < 0 && !event.mods.isPopupMenu()) {
        insertPoint(event.position);
        dragStart_ = processor_.energyCurve();
        draggedPoint_ = hitPoint(event.position);
    }
}

void CurveEditor::mouseDrag(const juce::MouseEvent& event)
{
    tg::EnergyCurve curve = dragStart_;
    if (draggedPoint_ >= 0) {
        const auto normalized = positionToCurve(event.position);
        tg::CurvePoint& point = curve.points[draggedPoint_];
        point.value = normalized.y;
        if (draggedPoint_ > 0 && draggedPoint_ < curve.numPoints - 1) {
            float t = normalized.x;
            if (event.mods.isShiftDown()) t = std::round(t * 16.0f) / 16.0f;
            point.t = std::clamp(t, curve.points[draggedPoint_ - 1].t + kMinimumPointGap,
                                 curve.points[draggedPoint_ + 1].t - kMinimumPointGap);
        }
        processor_.setEnergyCurve(curve);
        repaint();
    } else if (bentSegment_ >= 0) {
        const auto plot = plotBounds();
        const float height = std::max(1.0f, plot.getHeight());
        curve.points[bentSegment_].curve = std::clamp(dragStart_.points[bentSegment_].curve
                                                          + (dragStartY_ - event.position.y) * 2.0f / height,
                                                      -1.0f, 1.0f);
        processor_.setEnergyCurve(curve);
        repaint();
    }
}

void CurveEditor::mouseDoubleClick(const juce::MouseEvent& event)
{
    deletePoint(event.position);
}

} // namespace tgp
