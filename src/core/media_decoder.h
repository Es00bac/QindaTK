// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "application_contracts.h"
#include "media_source_info.h"

#include <QImage>
#include <QSize>
#include <QVector>
#include <memory>
#include <vector>

namespace QindaTK {

struct MediaDecodedFrame {
    // Format_RGB32 for opaque video, Format_ARGB32 (straight alpha) when the
    // stream carries alpha; tagged with MediaSourceInfo::frameColorSpace().
    QImage image;
    qint64 index = -1; // exact presentation index
    qint64 pts = 0;    // stream pts; always equals the indexed pts of `index`
    bool corrupt = false; // the decoder reported concealed damage in this frame
    // The Y'CbCr decode the pixels went through: matrix bt709, bt601,
    // bt2020, smpte240m or fcc ("rgb" for RGB-coded video, no matrix) and
    // range limited or full (docs/video-scopes.md "Source coding").
    QString matrix;
    QString range;
    OperationError error;
};

struct MediaAudioBlock {
    QVector<float> samples; // interleaved; silence where the media has no audio
    qint64 firstSample = 0; // media sample of samples[0] at sampleRate
    int sampleRate = 0;
    int channels = 0;
    qint64 decodedFrames = 0; // sample frames that came from the stream
    OperationError error;
};

// AGENT-CONTRACT (docs/media-playback.md "Decoder", D-096): one MediaDecoder
// is used by one thread at a time. Every call that reads media checks its
// CancellationToken between packets and inside libavformat I/O, returns
// ErrorCode::Cancelled promptly and leaves the decoder usable. Returned
// frames own their pixels; no FFmpeg buffer escapes this class.
class MediaDecoder {
public:
    static constexpr int kMaxDimension = 8192;
    static constexpr qint64 kMaxAudioRead = 48000LL * 60; // sample frames per call

    MediaDecoder();
    ~MediaDecoder();
    MediaDecoder(const MediaDecoder &) = delete;
    MediaDecoder &operator=(const MediaDecoder &) = delete;

    static bool available();
    static QString backendVersion();

    // Opens and indexes a local file, reusing MediaIndexCache for an
    // unchanged file. ErrorCode::Unavailable for a missing file,
    // Unsupported for unreadable or unsupported media.
    OperationError open(const QString &uri, const CancellationToken &cancel = {});
    bool isOpen() const;
    MediaSourceInfoPtr info() const;
    void close();

    // The frame whose pts is the indexed pts of `index`, scaled to fit
    // `maxSize` (an empty size keeps the coded size). Increasing indices a
    // short distance ahead decode forward without seeking.
    MediaDecodedFrame decodeFrame(qint64 index, const QSize &maxSize,
                                  const CancellationToken &cancel = {});
    // Media samples [firstSample, firstSample + count) at `sampleRate`,
    // relative to media time zero, as interleaved float. Mono sources are
    // duplicated to every output channel at unity gain.
    MediaAudioBlock readAudio(qint64 firstSample, qint64 count, int sampleRate, int channels,
                              const CancellationToken &cancel = {});

    struct Private;

private:
    std::unique_ptr<Private> d;
};

// A per-thread pool of open decoders keyed by local path. A file whose size
// or modification time changed is reopened. Not thread-safe by design: each
// worker thread owns its pool.
class MediaDecoderPool {
public:
    explicit MediaDecoderPool(int limit = 8);
    MediaDecoder *acquire(const QString &uri, const CancellationToken &cancel,
                          OperationError *error);
    void clear();
    int size() const { return int(m_entries.size()); }

private:
    struct Entry {
        QString path;
        qint64 fileSize = 0;
        qint64 modifiedMs = 0;
        std::unique_ptr<MediaDecoder> decoder;
        quint64 used = 0;
    };
    std::vector<Entry> m_entries;
    int m_limit = 8;
    quint64 m_clock = 0;
};

} // namespace QindaTK
