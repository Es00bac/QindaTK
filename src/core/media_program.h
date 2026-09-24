// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "media_decoder.h"

#include <QJsonObject>
#include <QSize>
#include <QStringList>
#include <QVariantMap>
#include <QVector>
#include <functional>
#include <memory>
#include <optional>
#include <utility>

namespace QindaTK {
class TimelineModel;

// A transition window on a program span (docs/sequence-edits.md, D-120):
// program ticks [start, start + duration). On the incoming span it is the
// fade in and `partnerClipId` names the outgoing span it blends from; on the
// outgoing span it is the fade out and names the incoming span.
struct MediaTransitionWindow {
    QString type;       // dissolve, wipe, dip
    qint64 start = 0;
    qint64 duration = 0; // 0: no transition
    QVariantMap parameters;
    QString partnerClipId;
    bool isNull() const { return duration <= 0; }
    bool contains(qint64 ticks) const { return !isNull() && ticks >= start && ticks - start < duration; }
};

struct MediaClipSpan {
    QString clipId;
    QString resourceId;
    QString uri;      // resolved local path or file URI; empty when unresolved
    QString revision; // resolver content revision; part of the program cache key
    qint64 start = 0; // program ticks
    qint64 duration = 1;
    qint64 sourceOffset = 0; // ticks into the source media
    MediaTransitionWindow fadeIn;  // spans include their transition handles
    MediaTransitionWindow fadeOut;
};

struct MediaSourceFrameRef {
    QString clipId;
    QString resourceId;
    qint64 frame = -1; // exact source presentation index
    qint64 pts = 0;    // the original's indexed pts of that frame
};

struct MediaRenderedFrame {
    QImage image;
    qint64 frame = -1;     // program frame
    qint64 startTicks = 0; // exact frame start in the program timebase
    QVector<MediaSourceFrameRef> sources;
    QStringList missingResourceIds;
    QStringList outOfRangeClipIds;
    bool corrupt = false;
    OperationError error;
};

struct MediaAudioMix {
    QVector<float> samples; // interleaved at the program channel count
    qint64 firstSample = 0;
    QStringList missingResourceIds;
    OperationError error;
};

// AGENT-CONTRACT (docs/media-playback.md "Programs", D-097): an immutable
// snapshot of what a monitor shows or an export writes, shared read-only by
// worker threads. A source program shows one media file frame by frame (its
// frames are the source's presentation indices); a timeline program
// composites a TimelineModel revision at its TimeMap frame rate:
//   program frame f starts at originTicks + ceil(f * timebase / frameRate);
//   video tracks draw from the last track in trackOrder (back) to the first
//   (front), skipping hidden tracks; each clip maps program ticks T to
//   source ticks sourceOffset + (T - start), held to a source frame;
//   audio mixes every unmuted audio track (only soloed ones when any is),
//   placed at the first sample at or after each clip's start;
//   a clip's transition (TimelineClip::transition, WP-071) extends the
//   outgoing and incoming spans over its window and MediaTransition blends
//   their pictures and crossfades their sound there.
// Sample positions and ticks share origin 0, as in TimelineModel.
class MediaProgram {
public:
    enum class Kind { Empty, Source, Timeline };
    // {uri, revision} for a resource ID; an empty uri marks it unresolved.
    using Resolver = std::function<std::pair<QString, QString>(const QString &resourceId)>;
    static constexpr int kMaxClips = 100000;

    Kind kind = Kind::Empty;
    QString programId;
    QString revision;
    TimelineRate timebase{TimelineTime::kDefaultTicksPerSecond, 1};
    TimelineRate frameRate{25, 1};
    int sampleRate = 48000;
    int channels = 2;
    qint64 originTicks = 0;
    QSize frameSize;
    qint64 frameCount = 0;

    MediaClipSpan source;    // source programs: the original
    QString playbackUri;     // the file decoded for pictures (proxy or original)
    bool proxy = false;
    MediaSourceInfoPtr sourceInfo; // the original's index; identity never uses the proxy's

    QVector<MediaClipSpan> video; // back to front
    QVector<MediaClipSpan> audio;
    QStringList unresolved;

    bool isEmpty() const { return kind == Kind::Empty || frameCount <= 0; }
    QString cacheKey() const;
    std::optional<qint64> frameStartTicks(qint64 frame) const;
    std::optional<qint64> frameStartSample(qint64 frame) const;
    // The frame showing at `sample`; std::nullopt outside [0, endSample()).
    std::optional<qint64> frameAtSample(qint64 sample) const;
    qint64 endSample() const;
    QVariantMap describe() const;

    // Worker-thread operations; `pool` belongs to the calling thread.
    MediaRenderedFrame render(qint64 frame, const QSize &outputSize, MediaDecoderPool &pool,
                              const CancellationToken &cancel) const;
    MediaAudioMix mixAudio(qint64 firstSample, qint64 count, MediaDecoderPool &pool,
                           const CancellationToken &cancel) const;

    // GUI-thread construction from the committed timeline state.
    static std::shared_ptr<MediaProgram> fromTimeline(const TimelineModel &timeline, const Resolver &resolve,
                                                      const QSize &frameSize, int channels,
                                                      QString *error = nullptr);
    static std::shared_ptr<MediaProgram> forSource(const MediaClipSpan &original, const QString &playbackUri,
                                                   bool proxy, MediaSourceInfoPtr info, int sampleRate,
                                                   int channels);
    QJsonObject toJson() const;
    static std::shared_ptr<MediaProgram> fromJson(const QJsonObject &json, QString *error = nullptr);
};
using MediaProgramPtr = std::shared_ptr<const MediaProgram>;

} // namespace QindaTK
