// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "slide_deck_types.h"

#include <QVariantMap>

namespace QindaTK::Slides {

// AGENT-CONTRACT (docs/slides.md#timing, D-159): a slide's builds are the
// effects before its first "click" effect (step 0, which starts after the
// slide transition) and one step per "click" effect. Within a step, an
// effect is triggered at the step start ("click" or first), at its
// predecessor's trigger ("with") or at its predecessor's end ("after");
// it starts `delay` ticks later and lasts `duration` ticks. All of this is
// exact integer ticks; only the eased progress is a double.
struct EffectTiming {
    QString effectId;
    QString targetId;
    int step = 0;
    qint64 start = 0;  // ticks from the start of `step`
    qint64 end = 0;
};

struct SlideSchedule {
    int buildCount = 0;              // number of click steps (steps 0..buildCount)
    qint64 transitionTicks = 0;      // step 0 only
    QVector<qint64> stepDurations;   // buildCount + 1 entries
    QVector<EffectTiming> effects;   // in effect order
};

struct ObjectFrame {
    QString id;
    double opacity = 1;
    double dx = 0;     // slide pixels
    double dy = 0;
    double scale = 1;  // about the object's centre
};

// Live presenter ink over the current slide (OFF-23 annotation commands),
// in slide pixels. `tool` is "pen" or "highlight". Ink belongs to the
// running show, not the document: it is never saved or exported.
struct AnnotationStroke {
    QString tool;
    QVector<QPointF> points;
};

// Everything the audience sees at one exact moment. It never carries notes.
struct SlideFrame {
    QString slideId;
    int step = 0;
    qint64 ticks = 0;
    QVector<ObjectFrame> objects;   // resolved object order
    QString transitionKind = QStringLiteral("none");
    double transitionProgress = 1;  // 1 once the transition finished
    QString fromSlideId;            // outgoing slide shown during the transition
    bool blackedOut = false;
    QVector<AnnotationStroke> annotations; // drawn over the slide
    bool pointerVisible = false;           // the presenter's pointer dot
    QPointF pointer;
};

SlideSchedule schedule(const Slide &slide);
// Eased progress in [0,1] through CurveEvaluator (docs/curve.md); unknown
// names evaluate linearly.
double ease(const QString &easing, double progress);
// Deterministic: the same deck, slide, step and ticks give the same frame.
// `step` is clamped to 0..buildCount and `ticks` to 0.. (no upper clamp:
// every effect is complete past its end). An empty `fromSlideId` or step >
// 0 shows no transition.
SlideFrame evaluate(const DeckState &deck, const QString &slideId, int step, qint64 ticks,
                    const QString &fromSlideId = {});
// The slide with every build complete (export, thumbnails, previous-slide).
SlideFrame finalFrame(const DeckState &deck, const QString &slideId);

// Exported playback (docs/slides.md#preview): every build step of a slide
// in order, sampled at `framesPerSecond` (frame n of a step at
// floor(n * TicksPerSecond / fps) ticks) and closed by the step's completed
// frame, the same positions a preview or a show reaches. Empty when fps is
// outside 1-240, the slide is unknown or the result would exceed
// MaximumPlaybackFrames.
struct PlaybackPosition {
    int step = 0;
    qint64 ticks = 0;
};
inline constexpr int MaximumPlaybackFrames = 36000;
QVector<PlaybackPosition> playbackPositions(const DeckState &deck, const QString &slideId, int framesPerSecond);

// Seconds (a presentation value) to ticks, rounded to the nearest
// millisecond first (705,600 ticks each) so 0.1 s is exactly 70,560,000
// ticks; nullopt when not finite, negative or above 1e6 s.
std::optional<qint64> secondsToTicks(double seconds);
double ticksToSeconds(qint64 ticks);
// QML shape: {slideId, step, ticks (decimal), transitionKind,
// transitionProgress, fromSlideId, blackedOut, annotationCount,
// pointerVisible, objects: [{id, opacity, dx, dy, scale}]}.
QVariantMap frameToVariant(const SlideFrame &frame);

} // namespace QindaTK::Slides
