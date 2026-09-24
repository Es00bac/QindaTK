// SPDX-License-Identifier: LGPL-3.0-or-later
// The media backend without FFmpeg (D-096): every operation reports an
// explicit capability error instead of inventing frames or samples.
#include "media_decoder.h"
#include "media_encoder.h"

#if !defined(QINDATK_HAVE_FFMPEG)

namespace QindaTK {
namespace {
OperationError unsupported()
{
    return {ErrorCode::Unsupported, QStringLiteral("QindaTK was built without the FFmpeg media backend"),
            {}, QStringLiteral("media")};
}
} // namespace

struct MediaDecoder::Private {};

MediaDecoder::MediaDecoder() : d(std::make_unique<Private>()) {}
MediaDecoder::~MediaDecoder() = default;
bool MediaDecoder::available() { return false; }
QString MediaDecoder::backendVersion() { return {}; }
OperationError MediaDecoder::open(const QString &, const CancellationToken &) { return unsupported(); }
bool MediaDecoder::isOpen() const { return false; }
MediaSourceInfoPtr MediaDecoder::info() const { return {}; }
void MediaDecoder::close() {}

MediaDecodedFrame MediaDecoder::decodeFrame(qint64 index, const QSize &, const CancellationToken &)
{
    MediaDecodedFrame frame;
    frame.index = index;
    frame.error = unsupported();
    return frame;
}

MediaAudioBlock MediaDecoder::readAudio(qint64 firstSample, qint64, int sampleRate, int channels,
                                        const CancellationToken &)
{
    MediaAudioBlock block;
    block.firstSample = firstSample;
    block.sampleRate = sampleRate;
    block.channels = channels;
    block.error = unsupported();
    return block;
}

struct MediaEncoder::Private {};

MediaEncoder::MediaEncoder() : d(std::make_unique<Private>()) {}
MediaEncoder::~MediaEncoder() = default;
bool MediaEncoder::available() { return false; }
OperationError MediaEncoder::open(const QString &, const MediaEncodeSettings &) { return unsupported(); }
OperationError MediaEncoder::writeVideo(const QImage &) { return unsupported(); }
OperationError MediaEncoder::writeAudio(const float *, qint64) { return unsupported(); }
OperationError MediaEncoder::finish() { return unsupported(); }
void MediaEncoder::abort() {}
qint64 MediaEncoder::videoFramesWritten() const { return 0; }
qint64 MediaEncoder::audioSamplesWritten() const { return 0; }

} // namespace QindaTK

#endif // !QINDATK_HAVE_FFMPEG
