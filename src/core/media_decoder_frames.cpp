// SPDX-License-Identifier: LGPL-3.0-or-later
#include "media_decoder.h"
#include "media_ffmpeg_p.h"

#include <QFileInfo>
#include <QDateTime>
#include <algorithm>

namespace QindaTK {

#if defined(QINDATK_HAVE_FFMPEG)
namespace {
using MediaFfmpeg::failure;

// Decoding up to this many frames forward is cheaper than a seek.
constexpr qint64 kForwardDecodeLimit = 96;
constexpr int kMaxPacketErrors = 64;

enum class Step { Found, Passed, Ended, Failed, Cancelled };

bool seekVideo(MediaDecoder::Private *d, qint64 keyIndex)
{
    const MediaVideoFrameEntry &entry = d->info->frames.at(keyIndex);
    const int stream = d->info->videoStream;
    // AGENT-NOTE: seeking to the key packet's decode timestamp lands at or
    // before it for demuxers that index by dts (MP4) and by pts (Matroska).
    int code = av_seek_frame(d->videoFormat, stream, entry.dts, AVSEEK_FLAG_BACKWARD);
    if (code < 0) code = av_seek_frame(d->videoFormat, stream, entry.pts, AVSEEK_FLAG_BACKWARD);
    if (code < 0) code = avformat_seek_file(d->videoFormat, stream, INT64_MIN, entry.dts, entry.dts, 0);
    avcodec_flush_buffers(d->video);
    d->videoPositioned = code >= 0;
    d->videoDemuxEnded = false;
    d->videoDecoderEnded = false;
    d->hasLastVideoPts = false;
    return code >= 0;
}

// Decodes forward until the frame whose timestamp is `target` is in d->frame.
Step decodeUntil(MediaDecoder::Private *d, qint64 target, const CancellationToken &cancel,
                 int *packetErrors)
{
    while (true) {
        const int received = avcodec_receive_frame(d->video, d->frame);
        if (received == 0) {
            qint64 pts = d->frame->best_effort_timestamp;
            if (pts == AV_NOPTS_VALUE) pts = d->frame->pts;
            if (pts == AV_NOPTS_VALUE) {
                av_frame_unref(d->frame);
                continue;
            }
            d->lastVideoPts = pts;
            d->hasLastVideoPts = true;
            if (pts == target) return Step::Found;
            av_frame_unref(d->frame);
            if (pts > target) return Step::Passed;
            continue;
        }
        if (received == AVERROR_EOF) {
            d->videoDecoderEnded = true;
            return Step::Ended;
        }
        if (received != AVERROR(EAGAIN)) {
            // Frame-threaded decoders report a damaged frame here; skip it.
            if (++*packetErrors > kMaxPacketErrors) return Step::Failed;
            continue;
        }
        if (cancel.isCancelled()) return Step::Cancelled;
        if (d->videoDemuxEnded) return Step::Ended;
        const int read = av_read_frame(d->videoFormat, d->packet);
        if (read < 0) {
            if (cancel.isCancelled()) return Step::Cancelled;
            d->videoDemuxEnded = true;
            avcodec_send_packet(d->video, nullptr); // drain buffered frames
            continue;
        }
        if (d->packet->stream_index != d->info->videoStream) {
            av_packet_unref(d->packet);
            continue;
        }
        const int sent = avcodec_send_packet(d->video, d->packet);
        av_packet_unref(d->packet);
        // A damaged packet is skipped; the target may still decode.
        if (sent < 0 && sent != AVERROR(EAGAIN) && ++*packetErrors > kMaxPacketErrors)
            return Step::Failed;
    }
}

// AGENT-CONTRACT (docs/video-scopes.md "Source coding", VideoScopesModel):
// the matrix named here is the one handed to swscale below, and the name is
// reported on MediaDecodedFrame::matrix, so a scope set to "source" reads
// the frame under the exact Y'CbCr decode it went through.
std::pair<int, QString> decodeMatrix(const AVFrame *frame)
{
    switch (frame->colorspace) {
    case AVCOL_SPC_BT709: return {SWS_CS_ITU709, QStringLiteral("bt709")};
    case AVCOL_SPC_BT2020_NCL:
    case AVCOL_SPC_BT2020_CL: return {SWS_CS_BT2020, QStringLiteral("bt2020")};
    case AVCOL_SPC_SMPTE240M: return {SWS_CS_SMPTE240M, QStringLiteral("smpte240m")};
    case AVCOL_SPC_FCC: return {SWS_CS_FCC, QStringLiteral("fcc")};
    case AVCOL_SPC_UNSPECIFIED:
        return frame->height >= 720 ? std::pair{SWS_CS_ITU709, QStringLiteral("bt709")}
                                    : std::pair{SWS_CS_ITU601, QStringLiteral("bt601")};
    default: return {SWS_CS_ITU601, QStringLiteral("bt601")};
    }
}

QImage convertFrame(MediaDecoder::Private *d, const QSize &maxSize, OperationError *error,
                    QString *matrix, QString *range)
{
    const AVFrame *frame = d->frame;
    const int width = frame->width;
    const int height = frame->height;
    const auto source = static_cast<AVPixelFormat>(frame->format);
    const AVPixFmtDescriptor *descriptor = av_pix_fmt_desc_get(source);
    if (width < 1 || height < 1 || width > MediaDecoder::kMaxDimension
        || height > MediaDecoder::kMaxDimension || !descriptor) {
        *error = failure(ErrorCode::Validation, QStringLiteral("The decoded frame has an invalid size or format"));
        return {};
    }
    QSize target(width, height);
    if (maxSize.width() > 0 && maxSize.height() > 0
        && (width > maxSize.width() || height > maxSize.height()))
        target = target.scaled(maxSize, Qt::KeepAspectRatio).expandedTo(QSize(1, 1));
    const bool scaled = target != QSize(width, height);
    d->scaler = sws_getCachedContext(d->scaler, width, height, source, target.width(), target.height(),
                                     AV_PIX_FMT_RGB32, scaled ? SWS_BICUBIC : SWS_POINT,
                                     nullptr, nullptr, nullptr);
    if (!d->scaler) {
        *error = failure(ErrorCode::Unsupported, QStringLiteral("No conversion from %1 to RGB")
                                                     .arg(MediaFfmpeg::name(descriptor->name)));
        return {};
    }
    *matrix = QStringLiteral("rgb");
    *range = QStringLiteral("full");
    if (!(descriptor->flags & AV_PIX_FMT_FLAG_RGB) && descriptor->nb_components >= 3) {
        const int fullRange = frame->color_range == AVCOL_RANGE_JPEG ? 1 : 0;
        const auto [coefficients, name] = decodeMatrix(frame);
        sws_setColorspaceDetails(d->scaler, sws_getCoefficients(coefficients), fullRange,
                                 sws_getCoefficients(SWS_CS_DEFAULT), 1, 0, 1 << 16, 1 << 16);
        *matrix = name;
        *range = fullRange ? QStringLiteral("full") : QStringLiteral("limited");
    }
    // AV_PIX_FMT_RGB32 is native-endian 0xAARRGGBB, QImage::Format_ARGB32.
    QImage image(target, QImage::Format_ARGB32);
    if (image.isNull()) {
        *error = failure(ErrorCode::BackendFailure, QStringLiteral("Cannot allocate the frame image"));
        return {};
    }
    uint8_t *destination[4] = {image.bits(), nullptr, nullptr, nullptr};
    const int strides[4] = {int(image.bytesPerLine()), 0, 0, 0};
    const int rows = sws_scale(d->scaler, frame->data, frame->linesize, 0, height, destination, strides);
    if (rows != target.height()) {
        *error = failure(ErrorCode::BackendFailure, QStringLiteral("Frame conversion failed"));
        return {};
    }
    if (!(descriptor->flags & AV_PIX_FMT_FLAG_ALPHA)) {
        // AGENT-GUARD: opaque sources may leave the padding byte undefined
        // (bgr0); Format_RGB32 requires 0xff there.
        for (int y = 0; y < image.height(); ++y) {
            auto *line = reinterpret_cast<QRgb *>(image.scanLine(y));
            for (int x = 0; x < image.width(); ++x) line[x] |= 0xff000000u;
        }
        image.reinterpretAsFormat(QImage::Format_RGB32);
    }
    image.setColorSpace(d->info->frameColorSpace());
    return image;
}

} // namespace

MediaDecodedFrame MediaDecoder::decodeFrame(qint64 index, const QSize &maxSize,
                                            const CancellationToken &cancel)
{
    MediaDecodedFrame result;
    result.index = index;
    if (!d->info || (!d->videoNeedsReopen && (!d->video || !d->videoFormat))) {
        result.error = failure(ErrorCode::Unsupported, QStringLiteral("No video stream is open"));
        return result;
    }
    const auto &frames = d->info->frames;
    if (index < 0 || index >= frames.size()) {
        result.error = failure(ErrorCode::InvalidArgument, QStringLiteral("Frame %1 is outside the indexed range 0–%2")
                                   .arg(index).arg(frames.size() - 1));
        return result;
    }
    if (cancel.isCancelled()) {
        result.error = MediaFfmpeg::cancelled();
        return result;
    }
    d->cancel = cancel;
    if (const OperationError error = d->ensureBuffers()) {
        result.error = error;
        return result;
    }
    if (d->videoNeedsReopen) {
        if (const OperationError error = d->reopenVideo()) {
            result.error = error;
            return result;
        }
    }
    const qint64 target = frames.at(index).pts;
    // Seek points to try in order; -1 continues from the current position.
    QVector<qint64> plan;
    if (d->videoPositioned && !d->videoDecoderEnded && d->hasLastVideoPts && target > d->lastVideoPts) {
        const qint64 last = d->info->indexOfPts(d->lastVideoPts);
        if (last >= 0 && index - last <= kForwardDecodeLimit) plan.append(-1);
    }
    const qint64 key = d->info->keyframeAtOrBefore(index);
    plan.append(key);
    if (key > 0) {
        const qint64 earlier = d->info->keyframeAtOrBefore(key - 1);
        plan.append(earlier);
        if (earlier > 0) plan.append(0);
    }
    // Last resort for demuxers that cannot seek: reopen and decode from the start.
    plan.append(-2);
    int packetErrors = 0;
    Step step = Step::Failed;
    for (const qint64 point : plan) {
        if (point == -2) {
            if (d->reopenVideo()) break;
        } else if (point >= 0 && !seekVideo(d.get(), point)) {
            continue;
        }
        step = decodeUntil(d.get(), target, cancel, &packetErrors);
        if (step == Step::Found || step == Step::Cancelled || step == Step::Failed) break;
    }
    if (step != Step::Found) {
        d->videoPositioned = false;
        if (step == Step::Cancelled) d->videoNeedsReopen = true;
        result.error = step == Step::Cancelled ? MediaFfmpeg::cancelled()
            : failure(ErrorCode::Validation, QStringLiteral("Frame %1 could not be decoded; the media may be damaged")
                                                 .arg(index));
        return result;
    }
    result.pts = target;
    result.corrupt = (d->frame->flags & AV_FRAME_FLAG_CORRUPT) != 0;
    result.image = convertFrame(d.get(), maxSize, &result.error, &result.matrix, &result.range);
    av_frame_unref(d->frame);
    return result;
}

#endif // QINDATK_HAVE_FFMPEG

MediaDecoderPool::MediaDecoderPool(int limit) : m_limit(std::max(1, limit)) {}

MediaDecoder *MediaDecoderPool::acquire(const QString &uri, const CancellationToken &cancel,
                                        OperationError *error)
{
    const QString path = mediaLocalPath(uri);
    const QFileInfo file(path);
    const auto fail = [error](ErrorCode code, const QString &message) -> MediaDecoder * {
        if (error) *error = {code, message, QStringLiteral("uri"), QStringLiteral("media")};
        return nullptr;
    };
    if (path.isEmpty()) return fail(ErrorCode::Unsupported, QStringLiteral("Media decoding accepts local paths and file URIs"));
    const auto stale = [&path](const Entry &entry) { return entry.path == path; };
    if (!file.exists() || !file.isFile()) {
        m_entries.erase(std::remove_if(m_entries.begin(), m_entries.end(), stale), m_entries.end());
        return fail(ErrorCode::Unavailable, QStringLiteral("Media is offline: %1").arg(path));
    }
    const qint64 size = file.size();
    const qint64 modified = file.lastModified().toMSecsSinceEpoch();
    for (Entry &entry : m_entries) {
        if (entry.path == path && entry.fileSize == size && entry.modifiedMs == modified
            && entry.decoder->isOpen()) {
            entry.used = ++m_clock;
            return entry.decoder.get();
        }
    }
    m_entries.erase(std::remove_if(m_entries.begin(), m_entries.end(), stale), m_entries.end());
    auto decoder = std::make_unique<MediaDecoder>();
    const OperationError opened = decoder->open(path, cancel);
    if (opened) {
        if (error) *error = opened;
        return nullptr;
    }
    if (int(m_entries.size()) >= m_limit) {
        const auto oldest = std::min_element(m_entries.begin(), m_entries.end(),
            [](const Entry &a, const Entry &b) { return a.used < b.used; });
        m_entries.erase(oldest);
    }
    m_entries.push_back({path, size, modified, std::move(decoder), ++m_clock});
    return m_entries.back().decoder.get();
}

void MediaDecoderPool::clear()
{
    m_entries.clear();
}

} // namespace QindaTK
