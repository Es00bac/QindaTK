// SPDX-License-Identifier: LGPL-3.0-or-later
#include "media_export.h"

#include <QColorSpace>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPainter>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace QindaTK {
namespace {
constexpr qint64 kMaxTestFrames = 65536;
constexpr int kCodeCells = 16;
constexpr double kPi = 3.14159265358979323846;

OperationError jobError(ErrorCode code, const QString &message)
{
    return {code, message, {}, QStringLiteral("media-export")};
}

int stripHeight(int height)
{
    return std::max(8, height / 8);
}

// A sibling temporary name; the finished file replaces `path` atomically.
QString temporaryFor(const QString &path)
{
    const QFileInfo info(path);
    return info.dir().filePath(QStringLiteral(".%1.part-%2").arg(info.fileName())
                                   .arg(QCoreApplication::applicationPid()));
}

OperationError publish(const QString &temporary, const QString &path)
{
    // std::rename replaces an existing target atomically on POSIX systems.
    if (std::rename(QFile::encodeName(temporary).constData(), QFile::encodeName(path).constData()) != 0) {
        QFile::remove(temporary);
        return jobError(ErrorCode::BackendFailure, QStringLiteral("Cannot move the finished file to %1").arg(path));
    }
    return {};
}

bool validSettings(const MediaTestSignalSettings &settings)
{
    const QSize size = settings.frameSize;
    return size.width() >= 32 && size.height() >= 32 && size.width() <= 3840 && size.height() <= 2160
        && settings.frameRate.isValid() && settings.frameRate.numerator <= 240 * settings.frameRate.denominator
        && settings.frames >= 1 && settings.frames <= kMaxTestFrames && settings.channels >= 0
        && settings.channels <= 8 && settings.sampleRate >= 8000 && settings.sampleRate <= 192000
        && settings.popInterval >= 0 && settings.popPhase >= 0;
}

bool isPop(qint64 frame, const MediaTestSignalSettings &settings)
{
    return settings.popInterval > 0 && frame >= settings.popPhase
        && (frame - settings.popPhase) % settings.popInterval == 0;
}
} // namespace

QImage MediaExport::testSignalFrame(qint64 frame, const MediaTestSignalSettings &settings)
{
    const int width = settings.frameSize.width();
    const int height = settings.frameSize.height();
    QImage image(settings.frameSize, QImage::Format_RGB32);
    if (image.isNull()) return image;
    image.setColorSpace(QColorSpace(QColorSpace::SRgb));
    const int strip = stripHeight(height);
    const int picture = height - strip;
    QPainter painter(&image);
    if (isPop(frame, settings)) {
        painter.fillRect(QRect(0, 0, width, picture), Qt::white);
    } else {
        static const QColor bars[] = {QColor(191, 191, 191), QColor(191, 191, 0), QColor(0, 191, 191),
                                      QColor(0, 191, 0), QColor(191, 0, 191), QColor(191, 0, 0),
                                      QColor(0, 0, 191)};
        const int rotation = ((settings.barRotation % 7) + 7) % 7;
        for (int bar = 0; bar < 7; ++bar) {
            const int left = bar * width / 7;
            painter.fillRect(QRect(left, 0, (bar + 1) * width / 7 - left, picture),
                             bars[(bar + rotation) % 7]);
        }
        const int cell = width / kCodeCells;
        painter.fillRect(QRect(int(frame % kCodeCells) * cell, picture / 3, cell, picture / 3), Qt::black);
    }
    for (int bit = 0; bit < kCodeCells; ++bit) {
        const int left = bit * width / kCodeCells;
        const QColor color = ((frame >> bit) & 1) ? Qt::white : Qt::black;
        painter.fillRect(QRect(left, picture, (bit + 1) * width / kCodeCells - left, strip), color);
    }
    painter.end();
    return image;
}

qint64 MediaExport::readTestSignalCode(const QImage &frame)
{
    if (frame.isNull() || frame.width() < kCodeCells || frame.height() < 8) return -1;
    const int strip = stripHeight(frame.height());
    const int y = frame.height() - strip / 2 - 1;
    qint64 code = 0;
    for (int bit = 0; bit < kCodeCells; ++bit) {
        const int left = bit * frame.width() / kCodeCells;
        const int right = (bit + 1) * frame.width() / kCodeCells;
        if (qGreen(frame.pixel((left + right) / 2, y)) >= 128) code |= qint64(1) << bit;
    }
    return code;
}

MediaExportResult MediaExport::writeTestSignal(const QString &path, const MediaTestSignalSettings &settings,
                                               const CancellationToken &cancel, const JobProgress &progress)
{
    MediaExportResult result;
    if (!validSettings(settings)) {
        result.error = jobError(ErrorCode::InvalidArgument, QStringLiteral("Test signals are 32×32–3840×2160, "
            "up to 240 fps and 65536 frames, with 0–8 audio channels at 8–192 kHz"));
        return result;
    }
    MediaEncodeSettings encode;
    encode.frameSize = settings.frameSize;
    encode.frameRate = settings.frameRate;
    encode.sampleRate = settings.sampleRate;
    encode.channels = settings.channels;
    const QString temporary = temporaryFor(path);
    MediaEncoder encoder;
    if (OperationError error = encoder.open(temporary, encode)) {
        result.error = error;
        return result;
    }
    const TimelineRate samples{settings.sampleRate, 1};
    QVector<float> audio;
    for (qint64 frame = 0; frame < settings.frames; ++frame) {
        if (cancel.isCancelled()) {
            encoder.abort();
            result.error = jobError(ErrorCode::Cancelled, QStringLiteral("Test signal cancelled"));
            return result;
        }
        if (OperationError error = encoder.writeVideo(testSignalFrame(frame, settings))) {
            encoder.abort();
            result.error = error;
            return result;
        }
        if (settings.channels > 0) {
            const qint64 from = TimelineTime::rescale(frame, settings.frameRate, samples, TimeMap::Ceil).value_or(0);
            const qint64 to = TimelineTime::rescale(frame + 1, settings.frameRate, samples, TimeMap::Ceil).value_or(from);
            audio.fill(0.0f, (to - from) * settings.channels);
            if (isPop(frame, settings)) {
                for (qint64 s = from; s < to; ++s) {
                    const float value = float(0.5 * std::sin(2.0 * kPi * 1000.0 * double(s) / settings.sampleRate));
                    std::fill_n(audio.begin() + (s - from) * settings.channels, settings.channels, value);
                }
            }
            if (OperationError error = encoder.writeAudio(audio.constData(), to - from)) {
                encoder.abort();
                result.error = error;
                return result;
            }
        }
        if (progress) progress(double(frame + 1) / double(settings.frames));
    }
    if (OperationError error = encoder.finish()) {
        result.error = error;
        return result;
    }
    result.frames = encoder.videoFramesWritten();
    result.samples = encoder.audioSamplesWritten();
    result.error = publish(temporary, path);
    return result;
}

MediaExportResult MediaExport::writeProxy(const QString &sourceUri, const QString &path, int maxHeight,
                                          const CancellationToken &cancel, const JobProgress &progress)
{
    MediaExportResult result;
    MediaDecoder decoder;
    if (OperationError error = decoder.open(sourceUri, cancel)) {
        result.error = error;
        return result;
    }
    const MediaSourceInfoPtr info = decoder.info();
    if (!info->hasVideo()) {
        result.error = jobError(ErrorCode::Unsupported, QStringLiteral("The media has no video to proxy"));
        return result;
    }
    if (!info->constantRate) {
        result.error = jobError(ErrorCode::Unsupported,
            QStringLiteral("Variable-rate media has no proxy; conform it through a timeline program"));
        return result;
    }
    const qint64 displayWidth = std::max<qint64>(1, (qint64(info->width) * info->sampleAspect.numerator
                                                     + info->sampleAspect.denominator / 2)
                                                        / info->sampleAspect.denominator);
    const int height = std::max(2, std::min(std::clamp(maxHeight, 16, 2160), info->height) & ~1);
    const int width = std::max(2, int(std::clamp<qint64>((displayWidth * height + info->height / 2) / info->height,
                                                         2, 8192)) & ~1);
    MediaEncodeSettings settings;
    settings.frameSize = QSize(width, height);
    settings.frameRate = info->nominalRate;
    settings.channels = info->hasAudio() ? std::min(info->audioChannels, 8) : 0;
    settings.sampleRate = info->hasAudio() ? info->audioSampleRate : 48000;
    const QString temporary = temporaryFor(path);
    MediaEncoder encoder;
    if (OperationError error = encoder.open(temporary, settings)) {
        result.error = error;
        return result;
    }
    const auto fail = [&](const OperationError &error) {
        encoder.abort();
        result.error = error;
        return result;
    };
    const TimelineRate samples{settings.sampleRate, 1};
    const qint64 count = info->frameCount();
    for (qint64 frame = 0; frame < count; ++frame) {
        if (cancel.isCancelled()) return fail(jobError(ErrorCode::Cancelled, QStringLiteral("Proxy cancelled")));
        const MediaDecodedFrame decoded = decoder.decodeFrame(frame, settings.frameSize, cancel);
        if (decoded.error) return fail(decoded.error);
        const QImage image = decoded.image.size() == settings.frameSize ? decoded.image
            : decoded.image.scaled(settings.frameSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        if (OperationError error = encoder.writeVideo(image)) return fail(error);
        if (settings.channels > 0) {
            const qint64 from = info->frameStartTicks(frame, samples).value_or(0);
            const qint64 to = frame + 1 < count ? info->frameStartTicks(frame + 1, samples).value_or(from)
                                                : info->durationTicks(samples).value_or(from);
            const MediaAudioBlock block = decoder.readAudio(from, to - from, settings.sampleRate,
                                                            settings.channels, cancel);
            if (block.error) return fail(block.error);
            if (OperationError error = encoder.writeAudio(block.samples.constData(), to - from))
                return fail(error);
        }
        if (progress) progress(double(frame + 1) / double(count));
    }
    if (OperationError error = encoder.finish()) {
        result.error = error;
        return result;
    }
    result.frames = encoder.videoFramesWritten();
    result.samples = encoder.audioSamplesWritten();
    result.error = publish(temporary, path);
    return result;
}

} // namespace QindaTK
