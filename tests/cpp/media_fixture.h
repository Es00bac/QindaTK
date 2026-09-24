// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "media_decoder.h"
#include "media_export.h"
#include "timeline_model.h"

#include <QByteArray>
#include <QFile>
#include <QImage>
#include <QProcess>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <cmath>

// Known-answer media fixtures shared by the WP-070 tests. Test signals are
// written by the toolkit's own encoder (no CLI needed); fixtures that need a
// B-frame codec, variable timestamps or an audio offset use the ffmpeg CLI
// and the tests QSKIP without it.
namespace MediaFixture {
using namespace QindaTK;

inline constexpr qint64 kTicks = TimelineTime::kDefaultTicksPerSecond;
inline constexpr qint64 kFrame25 = kTicks / 25; // 28,224,000 ticks
inline constexpr TimelineRate kTimebase{kTicks, 1};

inline QString signal(const QString &path, qint64 frames, int width = 128, int height = 72, int channels = 0,
                      qint64 popInterval = 25, TimelineRate rate = {25, 1})
{
    MediaTestSignalSettings settings;
    settings.frameSize = QSize(width, height);
    settings.frameRate = rate;
    settings.frames = frames;
    settings.channels = channels;
    settings.popInterval = popInterval;
    return MediaExport::writeTestSignal(path, settings).error ? QString() : path;
}

inline QString fileUri(const QString &path)
{
    return QUrl::fromLocalFile(path).toString();
}

inline bool writeFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

inline QString ffmpeg()
{
    return QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
}

inline bool runFfmpeg(const QStringList &arguments)
{
    QProcess process;
    process.start(ffmpeg(), QStringList{QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"),
                                        QStringLiteral("error"), QStringLiteral("-nostdin"), QStringLiteral("-y")}
                                + arguments);
    return process.waitForFinished(120000) && process.exitStatus() == QProcess::NormalExit
        && process.exitCode() == 0;
}

// 160x16 rgb24 frames: ten 16x16 black/white blocks spell the frame index,
// which survives lossy 4:2:0 coding.
inline QByteArray blockCodeFrames(int frames)
{
    QByteArray bytes;
    bytes.reserve(frames * 160 * 16 * 3);
    for (int frame = 0; frame < frames; ++frame)
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 160; ++x) {
                const char value = ((frame >> (x / 16)) & 1) ? char(255) : char(0);
                bytes.append(value).append(value).append(value);
            }
    return bytes;
}

inline qint64 blockCode(const QImage &image)
{
    if (image.width() != 160 || image.height() != 16) return -1;
    qint64 code = 0;
    for (int bit = 0; bit < 10; ++bit)
        if (qGreen(image.pixel(bit * 16 + 8, 8)) >= 128) code |= qint64(1) << bit;
    return code;
}

// width x height rgb24 frames whose pixel (0,0) red channel is the frame
// index (lossless codecs only); frame `white` is white elsewhere.
inline QByteArray pixelCodeFrames(int frames, int width, int height, int white = -1)
{
    QByteArray bytes;
    for (int frame = 0; frame < frames; ++frame)
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x) {
                if (x == 0 && y == 0) {
                    bytes.append(char(frame & 255)).append(char(0)).append(char(0x5a));
                } else {
                    const char value = frame == white ? char(255) : char(0);
                    bytes.append(value).append(value).append(value);
                }
            }
    return bytes;
}

// Mono s16le PCM at 48 kHz: `total` samples, 16384 (0.5) in [first, first + count).
inline QByteArray burstPcm(int total, int first, int count)
{
    QByteArray bytes;
    for (int sample = 0; sample < total; ++sample) {
        const qint16 value = sample >= first && sample < first + count ? 16384 : 0;
        bytes.append(char(value & 0xff)).append(char((value >> 8) & 0xff));
    }
    return bytes;
}

inline float peak(const QVector<float> &samples, qsizetype first, qsizetype count)
{
    float result = 0.0f;
    for (qsizetype i = first; i < first + count && i < samples.size(); ++i)
        result = std::max(result, std::fabs(samples.at(i)));
    return result;
}

inline TimelineTrack track(int n, const QString &kind)
{
    TimelineTrack t;
    t.id = QUuid(QStringLiteral("00000000-0000-4000-8000-%1").arg(n, 12, 10, QLatin1Char('0')));
    t.name = QStringLiteral("%1 %2").arg(kind).arg(n);
    t.kind = kind;
    return t;
}

inline TimelineClip clip(int n, const TimelineTrack &on, qint64 start, qint64 duration, qint64 offset,
                         const QString &resourceId)
{
    TimelineClip c;
    c.id = QUuid(QStringLiteral("00000000-0000-4000-8000-%1").arg(n + 1000, 12, 10, QLatin1Char('0')));
    c.trackId = on.id;
    c.start = start;
    c.duration = duration;
    c.sourceOffset = offset;
    c.resourceId = resourceId;
    c.kind = on.kind;
    c.label = QStringLiteral("Clip %1").arg(n);
    return c;
}
} // namespace MediaFixture
