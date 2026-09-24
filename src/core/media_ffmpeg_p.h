// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

// Private to the qindatk library: FFmpeg headers never reach an installed
// header, and nothing here exists in a build without FFmpeg (D-096).
#if defined(QINDATK_HAVE_FFMPEG)

#ifndef __STDC_CONSTANT_MACROS
#define __STDC_CONSTANT_MACROS
#endif

#include "media_decoder.h"

#include <cerrno>
#include <cstdint>
#include <optional>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/pixdesc.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

namespace QindaTK {
namespace MediaFfmpeg {
// av_err2str() uses a C compound literal; this is the C++ equivalent.
QString errorText(int code);
OperationError failure(ErrorCode code, const QString &message);
OperationError cancelled();
// libavformat interrupt callback; `opaque` is a const CancellationToken *.
int interrupt(void *opaque);
// A positive rational within TimelineRate's part limits, else nullopt.
std::optional<TimelineRate> rate(AVRational value);
// Units per second of a time base (the inverse rational), when valid.
std::optional<TimelineRate> unitsPerSecond(AVRational timeBase);
QString name(const char *text);
// Opens and probes a local file with a cancellable interrupt callback.
AVFormatContext *openFormat(const QString &path, const CancellationToken *token,
                            OperationError *error);
// Allocates and opens a decoder for one stream of `format`.
OperationError openDecoder(AVFormatContext *format, int stream, AVCodecContext **context);
} // namespace MediaFfmpeg

struct MediaDecoder::Private {
    QString path;
    MediaSourceInfoPtr info;
    // The running call's token; the interrupt callbacks read it by address.
    CancellationToken cancel;
    AVFormatContext *videoFormat = nullptr;
    AVCodecContext *video = nullptr;
    SwsContext *scaler = nullptr;
    bool videoPositioned = false;
    bool videoDemuxEnded = false;
    bool videoDecoderEnded = false;
    bool hasLastVideoPts = false;
    qint64 lastVideoPts = 0;
    // AGENT-GUARD: an interrupted read leaves AVIO in an error state; the next
    // video call reopens the demuxer instead of trusting it (reopenVideo()).
    bool videoNeedsReopen = false;
    AVFormatContext *audioFormat = nullptr;
    AVCodecContext *audio = nullptr;
    AVPacket *packet = nullptr;
    AVFrame *frame = nullptr;

    ~Private() { closeAll(); }
    void closeAll();
    OperationError ensureBuffers();
    OperationError openAudio();
    void closeAudio();
    // Reopens the video demuxer and decoder at the start of the file.
    OperationError reopenVideo();
};

} // namespace QindaTK

#endif // QINDATK_HAVE_FFMPEG
