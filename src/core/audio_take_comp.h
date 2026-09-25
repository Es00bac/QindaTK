// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QString>
#include <QVariantMap>
#include <QVector>
#include <optional>

namespace QindaTK {

// AGENT-CONTRACT (D-174, docs/audio-comping.md): a take comp is a
// non-destructive selection over takes. Positions are comp samples: frames
// at the comp's sampleRate counted from the comp origin, which a timeline
// clip with resourceId == comp id places (sourceOffset of that clip is the
// comp position at its start). Every take is a range of one media resource at
// the comp's rate; take audio is never rewritten or copied.
enum class AudioCompFade { Linear, EqualPower };

struct AudioTake {
    QString id;
    QString resourceId;
    QString label;
    qint64 sourceOffset = 0; // source frame aligned with comp sample 0 (may be negative)
    qint64 sourceStart = 0;  // usable source frames [sourceStart, sourceEnd)
    qint64 sourceEnd = 0;
    qint64 compStart() const { return sourceStart - sourceOffset; }
    qint64 compEnd() const { return sourceEnd - sourceOffset; }
    friend bool operator==(const AudioTake &, const AudioTake &) = default;
};

// A comp segment: [start, end) of the comp plays `takeId`. Segments are
// sorted, disjoint and inside [0, length); gaps are silence.
struct AudioCompSegment {
    QString id;
    QString takeId;
    qint64 start = 0;
    qint64 end = 0;
    friend bool operator==(const AudioCompSegment &, const AudioCompSegment &) = default;
};

struct AudioTakeComp {
    QString id; // "comp:<uuid>", also the timeline clip's resourceId
    int sampleRate = 48000;
    qint64 length = 0;
    qint64 fadeSamples = 0; // crossfade length at a junction of two takes
    AudioCompFade fadeShape = AudioCompFade::EqualPower;
    QVector<AudioTake> takes;
    QVector<AudioCompSegment> segments;
    const AudioTake *take(const QString &takeId) const;
    friend bool operator==(const AudioTakeComp &, const AudioTakeComp &) = default;
};

// What plays for one segment after the crossfade policy (D-175): the segment
// range, the extended range it is heard over, and its fade regions (empty
// when start == end). A fade region belongs to the segment's run (touching
// segments of one take), so it can reach past the segment's own edges; it
// only weights the samples the piece plays. A fade-in weight at sample s of [a, b) is
// t = (s - a + 0.5) / (b - a); linear t, equal power sin(t·π/2); a fade out
// uses 1 - t and cos(t·π/2).
struct AudioCompPiece {
    QString segmentId;
    QString takeId;
    qint64 start = 0;
    qint64 end = 0;
    qint64 playStart = 0;
    qint64 playEnd = 0;
    qint64 fadeInStart = 0;
    qint64 fadeInEnd = 0;
    qint64 fadeOutStart = 0;
    qint64 fadeOutEnd = 0;
};

// Everything the render graph needs for one comp clip (the comp, or the
// audition take when one is set), resolved on the GUI thread.
struct AudioCompPlayback {
    struct Part {
        AudioCompPiece piece;
        QString resourceId;
        qint64 sourceOffset = 0;
        qint64 sourceStart = 0;
        qint64 sourceEnd = 0;
    };
    bool valid = false;
    int sampleRate = 0;
    qint64 length = 0;
    AudioCompFade fadeShape = AudioCompFade::EqualPower;
    QVector<Part> parts;
};

namespace AudioComp {
inline constexpr qint64 kMaximumLength = qint64(1) << 40;
QString newId(const char *prefix);
QString fadeName(AudioCompFade shape);
// Empty when valid; otherwise the first problem found.
QString validate(const AudioTakeComp &comp);
// Edits (D-176): each validates, changes `comp` only on success and returns
// an empty string, or returns why it was refused.
QString select(AudioTakeComp &comp, const QString &takeId, qint64 start, qint64 end);
QString split(AudioTakeComp &comp, qint64 at);
QString merge(AudioTakeComp &comp, const QString &segmentId);
QString clear(AudioTakeComp &comp, qint64 start, qint64 end);
QString setFade(AudioTakeComp &comp, qint64 samples, AudioCompFade shape);
// Adds takes (unique IDs, same rate); the caller then selects their ranges.
QString addTakes(AudioTakeComp &comp, const QVector<AudioTake> &takes);
// The pieces that play (D-175). `auditionTakeId` non-empty: that take alone
// over its coverage inside the comp and inside [auditionStart, auditionEnd)
// (auditionEnd < 0: to the comp end), without fades.
QVector<AudioCompPiece> resolve(const AudioTakeComp &comp, const QString &auditionTakeId = {},
                                qint64 auditionStart = 0, qint64 auditionEnd = -1);
AudioCompPlayback playback(const AudioTakeComp &comp, const QString &auditionTakeId = {}, qint64 auditionStart = 0,
                           qint64 auditionEnd = -1);
// Fade weight of sample `s` in [a, b) (fade in when `in`).
// Realtime-safe (the render graph calls it per faded sample).
double fadeWeight(AudioCompFade shape, bool in, qint64 s, qint64 a, qint64 b) noexcept;

QVariantMap toMap(const AudioTakeComp &comp);
std::optional<AudioTakeComp> fromMap(const QVariantMap &map, QString *error);
QVariantMap pieceMap(const AudioCompPiece &piece);
} // namespace AudioComp

} // namespace QindaTK
