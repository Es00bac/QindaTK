// SPDX-License-Identifier: LGPL-3.0-or-later
#include "media_program.h"

#include <QColorSpace>
#include <QPainter>
#include <algorithm>

namespace QindaTK {
namespace {
OperationError programError(ErrorCode code, const QString &message)
{
    return {code, message, {}, QStringLiteral("media")};
}

bool usableSize(const QSize &size)
{
    return size.width() >= 1 && size.height() >= 1 && size.width() <= 8192 && size.height() <= 8192;
}

// Letterboxed placement of a source's display size (sample aspect applied).
QRect fitRect(const MediaSourceInfo &info, const QSize &output)
{
    const qint64 aspectWidth = (qint64(info.width) * info.sampleAspect.numerator
                                + info.sampleAspect.denominator / 2) / info.sampleAspect.denominator;
    const QSize display(int(std::clamp<qint64>(aspectWidth, 1, 65536)), std::max(1, info.height));
    const QSize fitted = display.scaled(output, Qt::KeepAspectRatio).expandedTo(QSize(1, 1));
    return QRect((output.width() - fitted.width()) / 2, (output.height() - fitted.height()) / 2,
                 fitted.width(), fitted.height());
}
} // namespace

MediaRenderedFrame MediaProgram::render(qint64 frame, const QSize &outputSize, MediaDecoderPool &pool,
                                        const CancellationToken &cancel) const
{
    MediaRenderedFrame result;
    result.frame = frame;
    if (isEmpty() || frame < 0 || frame >= frameCount) {
        result.error = programError(ErrorCode::InvalidArgument, QStringLiteral("Frame %1 is outside the program").arg(frame));
        return result;
    }
    result.startTicks = frameStartTicks(frame).value_or(0);
    if (cancel.isCancelled()) {
        result.error = programError(ErrorCode::Cancelled, QStringLiteral("Media request cancelled"));
        return result;
    }
    if (kind == Kind::Source) {
        OperationError error;
        MediaDecoder *decoder = pool.acquire(playbackUri, cancel, &error);
        if (!decoder) {
            if (error.code != ErrorCode::Cancelled) result.missingResourceIds.append(source.resourceId);
            result.error = error;
            return result;
        }
        const MediaDecodedFrame decoded = decoder->decodeFrame(frame, outputSize, cancel);
        if (decoded.error) {
            result.error = decoded.error;
            return result;
        }
        result.image = decoded.image;
        result.corrupt = decoded.corrupt;
        result.sources.append({QString(), source.resourceId, frame, sourceInfo->frames.at(frame).pts});
        return result;
    }
    const QSize size = usableSize(outputSize) ? outputSize : frameSize;
    QImage image(size, QImage::Format_RGB32);
    if (image.isNull()) {
        result.error = programError(ErrorCode::BackendFailure, QStringLiteral("Cannot allocate the program frame"));
        return result;
    }
    image.fill(Qt::black);
    image.setColorSpace(QColorSpace(QColorSpace::SRgb));
    QPainter painter(&image);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    const qint64 ticks = result.startTicks;
    for (const MediaClipSpan &clip : video) {
        const auto end = TimelineTime::add(clip.start, clip.duration);
        if (!end || ticks < clip.start || ticks >= *end) continue;
        if (clip.uri.isEmpty()) {
            result.missingResourceIds.append(clip.resourceId);
            continue;
        }
        OperationError error;
        MediaDecoder *decoder = pool.acquire(clip.uri, cancel, &error);
        if (!decoder) {
            if (error.code == ErrorCode::Cancelled) {
                painter.end();
                result.error = error;
                return result;
            }
            result.missingResourceIds.append(clip.resourceId);
            continue;
        }
        const MediaSourceInfoPtr info = decoder->info();
        const auto elapsed = TimelineTime::subtract(ticks, clip.start);
        const auto sourceTicks = elapsed ? TimelineTime::add(clip.sourceOffset, *elapsed) : std::nullopt;
        const auto sourceFrame = info && info->hasVideo() && sourceTicks
            ? info->frameAtTicks(*sourceTicks, timebase) : std::nullopt;
        if (!sourceFrame) {
            result.outOfRangeClipIds.append(clip.clipId);
            continue;
        }
        const QRect target = fitRect(*info, size);
        const MediaDecodedFrame decoded = decoder->decodeFrame(*sourceFrame, target.size(), cancel);
        if (decoded.error) {
            painter.end();
            result.error = decoded.error;
            return result;
        }
        painter.drawImage(target, decoded.image);
        result.corrupt = result.corrupt || decoded.corrupt;
        result.sources.append({clip.clipId, clip.resourceId, *sourceFrame, decoded.pts});
    }
    painter.end();
    result.missingResourceIds.removeDuplicates();
    result.image = image;
    return result;
}

MediaAudioMix MediaProgram::mixAudio(qint64 firstSample, qint64 count, MediaDecoderPool &pool,
                                     const CancellationToken &cancel) const
{
    MediaAudioMix mix;
    mix.firstSample = firstSample;
    if (count < 0 || count > MediaDecoder::kMaxAudioRead) {
        mix.error = programError(ErrorCode::InvalidArgument, QStringLiteral("An audio mix covers 0–%1 sample frames")
                                                                 .arg(MediaDecoder::kMaxAudioRead));
        return mix;
    }
    mix.samples = QVector<float>(count * channels, 0.0f);
    if (count == 0 || kind == Kind::Empty) return mix;
    const auto readInto = [&](const MediaClipSpan &clip, qint64 mediaFirst, qint64 outFirst, qint64 frames) {
        if (clip.uri.isEmpty()) {
            mix.missingResourceIds.append(clip.resourceId);
            return true;
        }
        OperationError error;
        MediaDecoder *decoder = pool.acquire(clip.uri, cancel, &error);
        if (!decoder) {
            if (error.code == ErrorCode::Cancelled) {
                mix.error = error;
                return false;
            }
            mix.missingResourceIds.append(clip.resourceId);
            return true;
        }
        const MediaAudioBlock block = decoder->readAudio(mediaFirst, frames, sampleRate, channels, cancel);
        if (block.error) {
            mix.error = block.error;
            return false;
        }
        float *out = mix.samples.data() + (outFirst - firstSample) * channels;
        for (qsizetype i = 0; i < block.samples.size(); ++i) out[i] += block.samples.at(i);
        return true;
    };
    if (kind == Kind::Source) {
        readInto(source, firstSample, firstSample, count);
        mix.missingResourceIds.removeDuplicates();
        return mix;
    }
    const TimelineRate samples{sampleRate, 1};
    const qint64 last = firstSample + count;
    for (const MediaClipSpan &clip : audio) {
        const auto clipFirst = TimelineTime::rescale(clip.start, timebase, samples, TimeMap::Ceil);
        const auto end = TimelineTime::add(clip.start, clip.duration);
        const auto clipEnd = end ? TimelineTime::rescale(*end, timebase, samples, TimeMap::Ceil) : std::nullopt;
        const auto offset = TimelineTime::rescale(clip.sourceOffset, timebase, samples, TimeMap::Ceil);
        if (!clipFirst || !clipEnd || !offset) continue;
        const qint64 from = std::max(firstSample, *clipFirst);
        const qint64 to = std::min(last, *clipEnd);
        if (from >= to) continue;
        if (!readInto(clip, *offset + (from - *clipFirst), from, to - from)) return mix;
    }
    mix.missingResourceIds.removeDuplicates();
    return mix;
}

} // namespace QindaTK
