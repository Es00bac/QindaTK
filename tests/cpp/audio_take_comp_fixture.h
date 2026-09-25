// SPDX-License-Identifier: LGPL-3.0-or-later
// Shared fixtures for the WP-065 take comp tests (tst_audio_take_comp,
// tst_audio_take_comp_engine): WAV helpers, a synthetic loop recording and a
// compact text form of a comp's segments.
#pragma once

#include "audio_session.h"

#include <QHash>
#include <QString>
#include <QStringList>
#include <functional>
#include <vector>

namespace QindaTK::CompFixture {

inline constexpr int Rate = 48000;
inline constexpr qint64 TicksPerSample = TimelineTime::kDefaultTicksPerSecond / Rate; // 14700, exact

inline qint64 ticks(qint64 samples) { return samples * TicksPerSample; }
inline QString text(qint64 value) { return QString::number(value); }
// Every file frame has its own exact float value.
inline float ramp(qint64 frame) { return float(frame + 1) / 65536.0f; }
inline float down(qint64 frame) { return -float(frame + 1) / 65536.0f; }

inline QString writeMono(const QString &path, qint64 frames, const std::function<float(qint64)> &value)
{
    AudioBuffer buffer;
    buffer.sampleRate = Rate;
    buffer.channels = 1;
    buffer.frames = frames;
    buffer.samples.resize(size_t(frames));
    for (qint64 i = 0; i < frames; ++i) buffer.samples[size_t(i)] = value(i);
    return AudioFile::writeWav(path, buffer) ? path : QString();
}

inline std::vector<float> readMono(const QString &path)
{
    const AudioBufferPtr buffer = AudioFile::readWav(path);
    return buffer ? std::vector<float>(buffer->channel(0), buffer->channel(0) + buffer->frames) : std::vector<float>{};
}

// The main output from sample 0 to `endSample` (-1: the end of the last clip), mono.
inline std::vector<float> render(AudioSession &session, const QString &path, qint64 endSample = -1)
{
    const QVariantMap result = session.renderToFile(path, QStringLiteral("0"),
                                                    endSample < 0 ? QString() : text(ticks(endSample)), 0, 1);
    return result.value(QStringLiteral("ok")).toBool() ? readMono(path) : std::vector<float>{};
}

// What AudioEngine::recordingFinished reports for a take recorded from
// transport sample 1900 with 100 frames of round-trip latency while the loop
// [2000, 3000) wrapped after file frames 1100 and 2100; stopped at 3150.
inline QVariantMap loopTake(const QString &path)
{
    const auto jump = [](qint64 frame) {
        return QVariantMap{{QStringLiteral("frame"), text(frame)}, {QStringLiteral("position"), QStringLiteral("2000")}};
    };
    return {{QStringLiteral("path"), path},
            {QStringLiteral("startSample"), QStringLiteral("1800")},
            {QStringLiteral("recordedSample"), QStringLiteral("1900")},
            {QStringLiteral("frames"), QStringLiteral("3150")},
            {QStringLiteral("sampleRate"), Rate},
            {QStringLiteral("channels"), 1},
            {QStringLiteral("droppedFrames"), QStringLiteral("0")},
            {QStringLiteral("latencyFrames"), 100},
            {QStringLiteral("deviceLost"), false},
            {QStringLiteral("error"), QString()},
            {QStringLiteral("jumps"), QVariantList{jump(1100), jump(2100)}},
            {QStringLiteral("jumpsDropped"), QStringLiteral("0")},
            {QStringLiteral("punchEnabled"), false}};
}

// A linear take heard from `start` (no latency), optionally punched.
inline QVariantMap linearTake(const QString &path, qint64 start, qint64 frames, qint64 punchStart = 0,
                              qint64 punchEnd = 0)
{
    return {{QStringLiteral("path"), path},
            {QStringLiteral("startSample"), text(start)},
            {QStringLiteral("recordedSample"), text(start)},
            {QStringLiteral("frames"), text(frames)},
            {QStringLiteral("sampleRate"), Rate},
            {QStringLiteral("channels"), 1},
            {QStringLiteral("droppedFrames"), QStringLiteral("0")},
            {QStringLiteral("latencyFrames"), 0},
            {QStringLiteral("error"), QString()},
            {QStringLiteral("jumps"), QVariantList{}},
            {QStringLiteral("jumpsDropped"), QStringLiteral("0")},
            {QStringLiteral("punchEnabled"), punchEnd > punchStart},
            {QStringLiteral("punchStart"), text(punchStart)},
            {QStringLiteral("punchEnd"), text(punchEnd)}};
}

// "Take 1:0-200|Take 3:200-1150": each segment as label:start-end.
inline QString layout(const AudioTakeCompModel *comps, const QString &compId)
{
    const QVariantMap data = comps->compData(compId);
    QHash<QString, QString> labels;
    for (const QVariant &take : data.value(QStringLiteral("takes")).toList())
        labels.insert(take.toMap().value(QStringLiteral("takeId")).toString(),
                      take.toMap().value(QStringLiteral("label")).toString());
    QStringList parts;
    for (const QVariant &value : data.value(QStringLiteral("segments")).toList()) {
        const QVariantMap segment = value.toMap();
        parts << QStringLiteral("%1:%2-%3").arg(labels.value(segment.value(QStringLiteral("takeId")).toString()),
                                               segment.value(QStringLiteral("start")).toString(),
                                               segment.value(QStringLiteral("end")).toString());
    }
    return parts.join(QLatin1Char('|'));
}

// The take ID with this label.
inline QString takeId(const AudioTakeCompModel *comps, const QString &compId, const QString &label)
{
    for (const QVariant &take : comps->compData(compId).value(QStringLiteral("takes")).toList())
        if (take.toMap().value(QStringLiteral("label")).toString() == label)
            return take.toMap().value(QStringLiteral("takeId")).toString();
    return {};
}

} // namespace QindaTK::CompFixture
