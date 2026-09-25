// SPDX-License-Identifier: LGPL-3.0-or-later
#include "slide_timeline.h"
#include "curve_evaluator.h"

#include <QHash>
#include <QVariantList>
#include <algorithm>
#include <cmath>

namespace QindaTK::Slides {
namespace {
CurveDefinition cubic(const QString &id, QPointF out, QPointF in)
{
    CurveDefinition curve;
    curve.id = id;
    CurveKnot first{QStringLiteral("start"), 0.0, 0.0, std::nullopt, out, CurveInterpolation::CubicBezier};
    CurveKnot last{QStringLiteral("end"), 1.0, 1.0, in, std::nullopt, CurveInterpolation::Linear};
    curve.knots = {first, last};
    return curve;
}
// AGENT-NOTE: the named easings are CSS-style cubic Béziers whose control
// x values are ordered (x1 <= x2), which CurveEvaluator requires for a
// unique inverse (docs/curve.md#validation-and-interpolation).
const QHash<QString, CurveDefinition> &curves()
{
    static const QHash<QString, CurveDefinition> table{
        {QStringLiteral("in-cubic"), cubic(QStringLiteral("in-cubic"), {0.32, 0.0}, {0.67, 0.0})},
        {QStringLiteral("out-cubic"), cubic(QStringLiteral("out-cubic"), {0.33, 1.0}, {0.68, 1.0})},
        {QStringLiteral("in-out-cubic"), cubic(QStringLiteral("in-out-cubic"), {0.42, 0.0}, {0.58, 1.0})}};
    return table;
}
const Effect *findEffect(const Slide &slide, const QString &id)
{
    for (const Effect &e : slide.effects) if (e.id == id) return &e;
    return nullptr;
}
} // namespace

std::optional<qint64> secondsToTicks(double seconds)
{
    if (!std::isfinite(seconds) || seconds < 0 || seconds > 1e6) return std::nullopt;
    const qint64 milliseconds = std::llround(seconds * 1000.0);
    return milliseconds * (TicksPerSecond / 1000);
}

double ticksToSeconds(qint64 ticks) { return double(ticks) / double(TicksPerSecond); }

double ease(const QString &easing, double progress)
{
    const double p = std::clamp(progress, 0.0, 1.0);
    if (p <= 0.0 || p >= 1.0) return p;
    const auto it = curves().constFind(easing);
    if (it == curves().constEnd()) return p; // linear: exact
    const CurveEvaluation result = CurveEvaluator::evaluateNormalized(*it, p);
    return result.sample ? std::clamp(result.sample->normalizedY, 0.0, 1.0) : p;
}

SlideSchedule schedule(const Slide &slide)
{
    SlideSchedule s;
    s.transitionTicks = slide.transition.kind == QStringLiteral("none") ? 0 : slide.transition.durationTicks;
    s.stepDurations = {s.transitionTicks};
    int step = 0;
    bool stepHasEffect = false;
    qint64 previousTrigger = 0;
    qint64 previousEnd = 0;
    for (const Effect &e : slide.effects) {
        qint64 trigger = 0;
        if (e.trigger == QStringLiteral("click")) {
            ++step;
            s.stepDurations.append(0);
            trigger = 0;
        } else if (!stepHasEffect) {
            trigger = step == 0 ? s.transitionTicks : 0;
        } else if (e.trigger == QStringLiteral("with")) {
            trigger = previousTrigger;
        } else {
            trigger = previousEnd;
        }
        stepHasEffect = true;
        const qint64 start = trigger + e.delayTicks;
        const qint64 end = start + e.durationTicks;
        s.effects.append({e.id, e.targetId, step, start, end});
        s.stepDurations[step] = std::max(s.stepDurations[step], end);
        previousTrigger = trigger;
        previousEnd = end;
    }
    s.buildCount = step;
    return s;
}

SlideFrame evaluate(const DeckState &deck, const QString &slideId, int step, qint64 ticks,
                    const QString &fromSlideId)
{
    SlideFrame frame;
    frame.slideId = slideId;
    const int index = slideIndex(deck, slideId);
    if (index < 0) return frame;
    const Slide &slide = deck.slides.at(index);
    const SlideSchedule plan = schedule(slide);
    frame.step = std::clamp(step, 0, plan.buildCount);
    frame.ticks = std::max<qint64>(0, ticks);
    if (frame.step == 0 && !fromSlideId.isEmpty() && fromSlideId != slideId && plan.transitionTicks > 0
        && slideIndex(deck, fromSlideId) >= 0 && frame.ticks < plan.transitionTicks) {
        frame.transitionKind = slide.transition.kind;
        frame.transitionProgress = double(frame.ticks) / double(plan.transitionTicks);
        frame.fromSlideId = fromSlideId;
    }
    for (const ResolvedObject &object : resolveObjects(deck, slide)) {
        ObjectFrame f{object.id};
        bool first = true;
        for (const EffectTiming &timing : plan.effects) {
            if (timing.targetId != object.id) continue;
            const Effect *effect = findEffect(slide, timing.effectId);
            if (!effect) continue;
            if (first && isEntryEffect(effect->kind)) f.opacity = 0; // hidden until its entry
            first = false;
            const bool earlier = timing.step < frame.step;
            if (!earlier && (timing.step > frame.step || frame.ticks < timing.start)) continue;
            double progress = 1;
            if (!earlier && timing.end > timing.start)
                progress = std::min(1.0, double(frame.ticks - timing.start) / double(timing.end - timing.start));
            const double e = ease(effect->easing, progress);
            f.opacity = 1; f.dx = 0; f.dy = 0; f.scale = 1;
            if (effect->kind == QStringLiteral("fade-in")) f.opacity = e;
            else if (effect->kind == QStringLiteral("fly-in")) f.dy = (1 - e) * (deck.size.height() - object.bounds.y());
            else if (effect->kind == QStringLiteral("zoom-in")) f.scale = e;
            else if (effect->kind == QStringLiteral("fade-out")) f.opacity = 1 - e;
        }
        frame.objects << f;
    }
    return frame;
}

SlideFrame finalFrame(const DeckState &deck, const QString &slideId)
{
    const int index = slideIndex(deck, slideId);
    if (index < 0) return evaluate(deck, slideId, 0, 0);
    const SlideSchedule plan = schedule(deck.slides.at(index));
    return evaluate(deck, slideId, plan.buildCount, plan.stepDurations.value(plan.buildCount));
}

QVector<PlaybackPosition> playbackPositions(const DeckState &deck, const QString &slideId, int framesPerSecond)
{
    const int index = slideIndex(deck, slideId);
    if (index < 0 || framesPerSecond < 1 || framesPerSecond > 240) return {};
    const SlideSchedule plan = schedule(deck.slides.at(index));
    QVector<PlaybackPosition> out;
    for (int step = 0; step <= plan.buildCount; ++step) {
        const qint64 end = plan.stepDurations.value(step);
        for (qint64 n = 0;; ++n) {
            const auto ticks = TimelineTime::mulDiv(n, quint64(TicksPerSecond), quint64(framesPerSecond),
                                                    TimeMap::Floor);
            if (!ticks || *ticks >= end) break;
            if (out.size() >= MaximumPlaybackFrames) return {};
            out.append({step, *ticks});
        }
        if (out.size() >= MaximumPlaybackFrames) return {};
        out.append({step, end}); // the completed build
    }
    return out;
}

QVariantMap frameToVariant(const SlideFrame &frame)
{
    QVariantList objects;
    for (const ObjectFrame &o : frame.objects)
        objects << QVariantMap{{"id", o.id}, {"opacity", o.opacity}, {"dx", o.dx}, {"dy", o.dy}, {"scale", o.scale}};
    return {{"slideId", frame.slideId}, {"step", frame.step}, {"ticks", QString::number(frame.ticks)},
            {"transitionKind", frame.transitionKind}, {"transitionProgress", frame.transitionProgress},
            {"fromSlideId", frame.fromSlideId}, {"blackedOut", frame.blackedOut},
            {"annotationCount", frame.annotations.size()}, {"pointerVisible", frame.pointerVisible},
            {"objects", objects}};
}

} // namespace QindaTK::Slides
