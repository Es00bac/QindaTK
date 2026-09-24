// SPDX-License-Identifier: LGPL-3.0-or-later
#include "media_program.h"
#include "media_transition.h"

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

// Applies the program's finishing (D-126) to a successfully rendered frame.
void finish(const MediaProgram &program, MediaRenderedFrame &result)
{
    if (result.error || result.image.isNull() || !program.finishing.changesPictures()) return;
    const auto milliseconds = program.frameStartMilliseconds(result.frame);
    if (!milliseconds) {
        result.error = programError(ErrorCode::Validation, QStringLiteral("Frame %1 has no program time").arg(result.frame));
        return;
    }
    if (OperationError error = program.finishing.apply(&result.image, *milliseconds)) result.error = error;
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
        finish(*this, result);
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
    // Draws one span's frame at `ticks` into `target`; false only on a hard
    // failure (cancellation or decode error), which ends the render.
    const auto drawSpan = [&](const MediaClipSpan &clip, QPainter &target) {
        if (clip.uri.isEmpty()) {
            result.missingResourceIds.append(clip.resourceId);
            return true;
        }
        OperationError error;
        MediaDecoder *decoder = pool.acquire(clip.uri, cancel, &error);
        if (!decoder) {
            if (error.code == ErrorCode::Cancelled) {
                result.error = error;
                return false;
            }
            result.missingResourceIds.append(clip.resourceId);
            return true;
        }
        const MediaSourceInfoPtr info = decoder->info();
        const auto elapsed = TimelineTime::subtract(ticks, clip.start);
        const auto sourceTicks = elapsed ? TimelineTime::add(clip.sourceOffset, *elapsed) : std::nullopt;
        const auto sourceFrame = info && info->hasVideo() && sourceTicks && *sourceTicks >= 0
            ? info->frameAtTicks(*sourceTicks, timebase) : std::nullopt;
        if (!sourceFrame) {
            result.outOfRangeClipIds.append(clip.clipId);
            return true;
        }
        const QRect rect = fitRect(*info, size);
        const MediaDecodedFrame decoded = decoder->decodeFrame(*sourceFrame, rect.size(), cancel);
        if (decoded.error) {
            result.error = decoded.error;
            return false;
        }
        target.drawImage(rect, decoded.image);
        result.corrupt = result.corrupt || decoded.corrupt;
        result.sources.append({clip.clipId, clip.resourceId, *sourceFrame, decoded.pts});
        return true;
    };
    const auto frameEnd = frameStartTicks(frame + 1).value_or(ticks + 1);
    for (qsizetype i = 0; i < video.size(); ++i) {
        const MediaClipSpan &clip = video.at(i);
        const auto end = TimelineTime::add(clip.start, clip.duration);
        if (!end || ticks < clip.start || ticks >= *end) continue;
        // An outgoing span inside its window is drawn with its incoming partner.
        if (clip.fadeOut.contains(ticks)) continue;
        if (!clip.fadeIn.contains(ticks)) {
            if (!drawSpan(clip, painter)) {
                painter.end();
                return result;
            }
            continue;
        }
        const MediaClipSpan *outgoing = nullptr;
        for (qsizetype j = i - 1; j >= 0 && !outgoing; --j)
            if (video.at(j).clipId == clip.fadeIn.partnerClipId) outgoing = &video.at(j);
        QImage layers[2];
        for (int side = 0; side < 2; ++side) {
            layers[side] = QImage(size, QImage::Format_ARGB32_Premultiplied);
            layers[side].fill(Qt::transparent);
            layers[side].setColorSpace(QColorSpace(QColorSpace::SRgb));
            const MediaClipSpan *span = side == 0 ? outgoing : &clip;
            if (!span) continue;
            QPainter layerPainter(&layers[side]);
            const bool drawn = drawSpan(*span, layerPainter);
            layerPainter.end();
            if (!drawn) {
                painter.end();
                return result;
            }
        }
        const int weight = MediaTransition::frameWeight(ticks, frameEnd, clip.fadeIn.start, clip.fadeIn.duration);
        painter.drawImage(0, 0, MediaTransition::blend(layers[0], layers[1], clip.fadeIn.type, weight,
                                                       clip.fadeIn.parameters));
    }
    painter.end();
    result.missingResourceIds.removeDuplicates();
    result.image = image;
    finish(*this, result);
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
    const TimelineRate samples{sampleRate, 1};
    // A span's gain at program sample s: its transition windows crossfade it.
    const auto windowSamples = [&](const MediaTransitionWindow &window, qint64 &windowFirst, qint64 &windowCount) {
        const auto from = TimelineTime::rescale(window.start, timebase, samples, TimeMap::Ceil);
        const auto end = TimelineTime::add(window.start, window.duration);
        const auto to = end ? TimelineTime::rescale(*end, timebase, samples, TimeMap::Ceil) : std::nullopt;
        windowFirst = from.value_or(0);
        windowCount = from && to ? *to - *from : 0;
    };
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
        if (clip.fadeIn.isNull() && clip.fadeOut.isNull()) {
            for (qsizetype i = 0; i < block.samples.size(); ++i) out[i] += block.samples.at(i);
            return true;
        }
        qint64 inFirst = 0, inCount = 0, outFirstSample = 0, outCount = 0;
        if (!clip.fadeIn.isNull()) windowSamples(clip.fadeIn, inFirst, inCount);
        if (!clip.fadeOut.isNull()) windowSamples(clip.fadeOut, outFirstSample, outCount);
        const qsizetype sampleFrames = block.samples.size() / channels;
        for (qsizetype f = 0; f < sampleFrames; ++f) {
            const qint64 sample = outFirst + f;
            const float gain = MediaTransition::audioGain(sample, inFirst, inCount, true, clip.fadeIn.parameters)
                * MediaTransition::audioGain(sample, outFirstSample, outCount, false, clip.fadeOut.parameters);
            for (int c = 0; c < channels; ++c) out[f * channels + c] += block.samples.at(f * channels + c) * gain;
        }
        return true;
    };
    if (kind == Kind::Source) {
        readInto(source, firstSample, firstSample, count);
        mix.missingResourceIds.removeDuplicates();
        return mix;
    }
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
