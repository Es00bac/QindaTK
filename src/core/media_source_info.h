// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "timeline_time.h"

#include <QColorSpace>
#include <QString>
#include <QVariantMap>
#include <QVector>
#include <memory>
#include <optional>

namespace QindaTK {

// One indexed video packet. After indexing the list is in presentation order.
struct MediaVideoFrameEntry {
    qint64 pts = 0;      // presentation timestamp, stream time base
    qint64 dts = 0;      // decode timestamp, or pts when the container has none
    qint64 duration = 0; // stream time base; 0 when the container does not say
    bool key = false;    // the demuxer marked this packet as a seek point
};

// One indexed audio packet: its timestamp and the stream samples before it.
struct MediaAudioPacketEntry {
    qint64 pts = 0;
    qint64 firstSample = 0;
};

// AGENT-CONTRACT (docs/media-playback.md "Frame identity", D-097): a source
// video frame is identified by (resource, content revision, zero-based
// presentation index). Its time follows the stream's rate policy:
//   constant rate: frame i starts at exactly i / nominalRate seconds, even
//     when the container rounded the stored pts (Matroska keeps milliseconds);
//   variable rate: frame i starts at pts[i] - pts[0] in the stream time base.
// Every conversion to ticks/samples rounds once: a frame starts at the first
// tick at or after its exact start (Ceil); a tick belongs to the last frame
// that has started by then (hold), so frameAtTicks(frameStartTicks(i)) == i.
// Media time zero is the first video frame; audio sample positions are stated
// relative to it through audioOffsetSamples.
struct MediaSourceInfo {
    static constexpr qint64 kMaxFrames = 10000000;
    static constexpr qint64 kMaxAudioPackets = 20000000;

    QString path;
    qint64 fileSize = 0;
    qint64 modifiedMs = 0;
    QString container;
    QString backendVersion;
    double containerSeconds = 0.0; // header duration, presentation only
    QString sourceTimecode;

    int videoStream = -1;
    QString videoCodec;
    QString pixelFormat;
    int width = 0;
    int height = 0;
    bool hasAlpha = false;
    TimelineRate sampleAspect{1, 1};
    TimelineRate ptsPerSecond{1, 1}; // inverse of the video stream time base
    TimelineRate nominalRate{25, 1}; // frames per second (display rate for VFR)
    bool constantRate = true;
    QVector<MediaVideoFrameEntry> frames;
    QString fieldOrder;
    QString colorPrimaries;
    QString colorTransfer;
    QString colorMatrix;
    QString colorRange;

    int audioStream = -1;
    QString audioCodec;
    int audioSampleRate = 0;
    int audioChannels = 0;
    TimelineRate audioPtsPerSecond{1, 1};
    QVector<MediaAudioPacketEntry> audioPackets; // sorted by pts
    qint64 audioSampleCount = 0;
    qint64 audioStartPts = 0;
    qint64 audioOffsetSamples = 0; // stream sample 0 relative to media time zero
    bool audioPtsSampleExact = false; // audio time base is 1/sampleRate

    bool truncated = false; // the header promised more media than the file holds
    bool damaged = false;   // read errors, corrupt packets or duplicate timestamps
    QString damage;

    bool hasVideo() const { return videoStream >= 0 && !frames.isEmpty(); }
    bool hasAudio() const { return audioStream >= 0 && audioSampleRate > 0; }
    qint64 frameCount() const { return frames.size(); }

    // `rate` is any tick rate in ticks per second; audio samples are ticks at
    // the sample rate. std::nullopt for an index or tick outside the media.
    std::optional<qint64> frameStartTicks(qint64 index, TimelineRate rate) const;
    std::optional<qint64> durationTicks(TimelineRate rate) const;
    std::optional<qint64> frameAtTicks(qint64 ticks, TimelineRate rate) const;
    // Presentation index of an indexed pts, or -1.
    qint64 indexOfPts(qint64 pts) const;
    // Index of the last key frame at or before `index` (0 when none is marked).
    qint64 keyframeAtOrBefore(qint64 index) const;
    // The last indexed audio packet whose first sample is <= sample, or -1.
    qint64 audioPacketAtOrBefore(qint64 sample) const;
    // Stream sample of the audio packet with this pts, or -1 when unindexed.
    qint64 audioFirstSampleOfPts(qint64 pts) const;

    // Colour policy: SDR Rec.601/709 and untagged video is tagged encoded sRGB
    // (a preview approximation); BT.2020, PQ, HLG, P3 and linear streams keep
    // their own tag so consumers convert explicitly. `policy` names the case.
    QColorSpace frameColorSpace(QString *policy = nullptr) const;
    QVariantMap toVariantMap() const;

    // Index construction (pure; shared by the decoder and tests).
    static bool isConstantRate(const QVector<MediaVideoFrameEntry> &frames,
                               TimelineRate ptsPerSecond, TimelineRate candidate);
    // Sorts frames by pts, drops duplicate timestamps (marking damage), and
    // chooses the rate policy from the container's two declared rates.
    void finalizeIndex(std::optional<TimelineRate> realRate,
                       std::optional<TimelineRate> averageRate);
};
using MediaSourceInfoPtr = std::shared_ptr<const MediaSourceInfo>;

// Process-wide, thread-safe, bounded (64 entries) cache of indexed sources,
// keyed by absolute path, size and modification time.
namespace MediaIndexCache {
MediaSourceInfoPtr find(const QString &path, qint64 fileSize, qint64 modifiedMs);
void insert(const MediaSourceInfoPtr &info);
void clear();
} // namespace MediaIndexCache

// Local path for a file URL or plain path; empty for other schemes.
QString mediaLocalPath(const QString &uri);

} // namespace QindaTK
