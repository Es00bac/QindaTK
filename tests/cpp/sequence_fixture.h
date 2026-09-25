// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "time_map.h"
#include "timeline_codec.h"
#include "timeline_model.h"

#include <QUuid>
#include <memory>

// Known-answer sequence for the WP-071 edit tests (docs/sequence-edits.md).
// 25 fps, 48 kHz, default 705,600,000 ticks/s timebase, one frame F ticks.
//   V1 (video): A [0, 50F) src 10F..  | B [50F, 100F) src 20F.. | C [100F, 125F) src 0 (no handles)
//   A1 (audio): a [0, 50F) linked to A | b [50F, 100F) linked to B
// Every source is 100F long except C's (25F), so A has a 40F tail handle, B a
// 20F head and 30F tail handle, and C neither.
namespace SequenceFixture {
using namespace QindaTK;

inline constexpr qint64 S = TimelineTime::kDefaultTicksPerSecond;
inline constexpr qint64 F = S / 25; // 28,224,000 ticks

inline QUuid uid(int n)
{
    return QUuid(QStringLiteral("00000000-0000-4000-8000-%1").arg(n, 12, 10, QLatin1Char('0')));
}

enum Id { V1 = 1, A1 = 2, ClipA = 11, ClipB = 12, ClipC = 13, SoundA = 21, SoundB = 22, LinkA = 91, LinkB = 92 };

inline TimelineTrack track(int n, const QString &kind)
{
    TimelineTrack t;
    t.id = uid(n);
    t.name = kind + QString::number(n);
    t.kind = kind;
    return t;
}

inline TimelineClip clip(int n, int trackNumber, const QString &kind, qint64 start, qint64 duration,
                         qint64 offset, qint64 sourceDuration, const QString &resource, int link = 0)
{
    TimelineClip c;
    c.id = uid(n);
    c.trackId = uid(trackNumber);
    c.kind = kind;
    c.start = start;
    c.duration = duration;
    c.sourceOffset = offset;
    c.sourceDuration = sourceDuration;
    c.resourceId = resource;
    c.label = QStringLiteral("Clip %1").arg(n);
    if (link) c.linkId = uid(link);
    return c;
}

// A TimelineModel with its own 25 fps TimeMap and an NLE policy (no overlaps).
struct Session {
    TimeMap map;
    TimelineModel model;
    Session()
    {
        map.setSampleRate(48000);
        map.setFrameRate(25, 1);
        model.setTimeMap(&map);
        model.policy()->setOverlapMode(TimelineEditPolicy::RejectOverlap);
    }
    bool loadStandard()
    {
        const QString video = QStringLiteral("video"), audio = QStringLiteral("audio");
        return model.setContent({S, 1}, {track(V1, video), track(A1, audio)},
                                {clip(ClipA, V1, video, 0, 50 * F, 10 * F, 100 * F, QStringLiteral("a"), LinkA),
                                 clip(ClipB, V1, video, 50 * F, 50 * F, 20 * F, 100 * F, QStringLiteral("b"), LinkB),
                                 clip(ClipC, V1, video, 100 * F, 25 * F, 0, 25 * F, QStringLiteral("c")),
                                 clip(SoundA, A1, audio, 0, 50 * F, 10 * F, 100 * F, QStringLiteral("a"), LinkA),
                                 clip(SoundB, A1, audio, 50 * F, 50 * F, 20 * F, 100 * F, QStringLiteral("b"), LinkB)},
                                {});
    }
    const TimelineClip &at(int n) const { return *model.clipRecord(uid(n)); }
    // One gesture from 0 to `delta`, committed; returns the error (empty on success).
    OperationError edit(TimelineEditKind kind, const QVector<int> &clips, qint64 delta, bool linked = true,
                        bool ripple = false)
    {
        TimelineEditRequest request;
        request.kind = kind;
        for (int n : clips) request.clipIds.push_back(uid(n));
        request.linked = linked;
        request.ripple = ripple;
        request.frameExact = true;
        TimelineEditInput input;
        input.pointerTicks = delta;
        input.snap = false;
        return model.apply(request, input);
    }
    OperationError transition(int clipId, const QString &type, qint64 length, const QString &alignment,
                              const QVariantMap &parameters = {}, bool linked = false)
    {
        TimelineEditRequest request;
        request.kind = TimelineEditKind::SetTransition;
        request.clipIds = {uid(clipId)};
        request.linked = linked;
        request.frameExact = true;
        TimelineTransition t;
        t.type = type;
        t.afterCut = length;
        t.alignment = alignment;
        t.parameters = parameters;
        request.transition = t;
        TimelineEditInput input;
        input.snap = false;
        return model.apply(request, input);
    }
};
} // namespace SequenceFixture
