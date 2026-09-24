// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "timeline_model.h"

#include <QString>
#include <QUuid>

// Shared known-answer fixtures for the timeline tests. Positions are exact
// ticks in the default 705,600,000 ticks-per-second timebase.
namespace TimelineFixture {
using namespace QindaTK;

inline constexpr qint64 S = TimelineTime::kDefaultTicksPerSecond;

inline QUuid uid(int n)
{
    return QUuid(QStringLiteral("00000000-0000-4000-8000-%1").arg(n, 12, 10, QLatin1Char('0')));
}

inline TimelineTrack track(int n, const QString &kind = QStringLiteral("audio"))
{
    TimelineTrack t;
    t.id = uid(n);
    t.name = QStringLiteral("Track %1").arg(n);
    t.kind = kind;
    return t;
}

inline TimelineClip clip(int n, int trackNumber, qint64 start, qint64 duration,
                         const QString &kind = QStringLiteral("audio"))
{
    TimelineClip c;
    c.id = uid(n);
    c.trackId = uid(trackNumber);
    c.start = start;
    c.duration = duration;
    c.kind = kind;
    c.label = QStringLiteral("Clip %1").arg(n);
    c.resourceId = QStringLiteral("resource:%1").arg(n);
    return c;
}

inline TimelineEditRequest request(TimelineEditKind kind, const QVector<QUuid> &clips,
                                   qint64 grab = 0, bool ripple = false)
{
    TimelineEditRequest r;
    r.kind = kind;
    r.clipIds = clips;
    r.grabTicks = grab;
    r.ripple = ripple;
    return r;
}

inline TimelineEditInput at(qint64 pointer, qint64 tolerance = 0, int trackDelta = 0)
{
    TimelineEditInput input;
    input.pointerTicks = pointer;
    input.snapToleranceTicks = tolerance;
    input.trackDelta = trackDelta;
    return input;
}

// 100 tracks x 100 one-second clips with one-second gaps: 10,000 clips.
inline bool loadLargeSession(TimelineModel &model, int tracks = 100, int clipsPerTrack = 100)
{
    QVector<TimelineTrack> trackList;
    QVector<TimelineClip> clipList;
    trackList.reserve(tracks);
    clipList.reserve(tracks * clipsPerTrack);
    for (int t = 0; t < tracks; ++t) {
        trackList.push_back(track(t + 1));
        for (int c = 0; c < clipsPerTrack; ++c)
            clipList.push_back(clip(100000 + t * clipsPerTrack + c, t + 1, qint64(c) * 2 * S, S));
    }
    return model.setContent({S, 1}, trackList, clipList, {});
}
} // namespace TimelineFixture
