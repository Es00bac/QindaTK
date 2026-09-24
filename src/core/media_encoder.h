// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "application_contracts.h"
#include "timeline_time.h"

#include <QImage>
#include <QSize>
#include <QString>
#include <memory>

namespace QindaTK {

struct MediaEncodeSettings {
    QSize frameSize;               // required when video is written; 1..8192 each side
    TimelineRate frameRate{25, 1}; // constant output frame rate
    bool video = true;
    int sampleRate = 48000;
    int channels = 2; // 0 writes no audio stream; at most 8
    int gopSize = 1;  // FFV1 key-frame interval; 1 makes every frame a seek point
};

// AGENT-CONTRACT (docs/media-playback.md "Export", D-096): writes lossless
// FFV1 video (8-bit RGB) and PCM s16le audio in a Matroska file. Frames and
// samples are consecutive: frame f starts at f / frameRate, sample s at
// s / sampleRate, so audio written per frame stays aligned with its frame.
// An encoder destroyed or aborted before finish() removes its partial file.
class MediaEncoder {
public:
    MediaEncoder();
    ~MediaEncoder();
    MediaEncoder(const MediaEncoder &) = delete;
    MediaEncoder &operator=(const MediaEncoder &) = delete;

    static bool available();
    OperationError open(const QString &path, const MediaEncodeSettings &settings);
    // The next frame; its size must equal settings.frameSize.
    OperationError writeVideo(const QImage &frame);
    // The next `frames` sample frames of `channels` interleaved floats in [-1, 1].
    OperationError writeAudio(const float *interleaved, qint64 frames);
    OperationError finish();
    void abort();
    qint64 videoFramesWritten() const;
    qint64 audioSamplesWritten() const;

    struct Private;

private:
    std::unique_ptr<Private> d;
};

} // namespace QindaTK
