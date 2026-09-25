// SPDX-License-Identifier: LGPL-3.0-or-later
// WP-068 session fixtures shared by tst_audio_pitch_session and
// tst_audio_pitch_requests: a voice-like harmonic tone, the A3→E4 legato
// source in a session with an AudioPitchSource, offline renders, and
// re-analysis of rendered samples.
#pragma once

#include "audio_pitch.h"
#include "audio_pitch_source.h"
#include "audio_session.h"

#include <QTemporaryDir>
#include <QtTest>
#include <cmath>
#include <functional>

// Included once per test executable; the functions are not inline.
namespace QindaTK::PitchFixture {

constexpr int Rate = 48000;
constexpr qint64 TicksPerSample = TimelineTime::kDefaultTicksPerSecond / Rate;
constexpr double Pi = 3.14159265358979323846;

QString ticks(qint64 samples) { return QString::number(samples * TicksPerSample); }
bool ok(const QVariantMap &result) { return result.value(QStringLiteral("ok")).toBool(); }

// 1/h harmonics under a 1 kHz formant with a phase-continuous f0(t).
AudioBuffer voice(const std::function<double(double)> &f0, double seconds)
{
    AudioBuffer buffer;
    buffer.sampleRate = Rate;
    buffer.channels = 1;
    buffer.frames = qint64(seconds * Rate);
    buffer.samples.resize(size_t(buffer.frames));
    double phase = 0.0;
    for (qint64 i = 0; i < buffer.frames; ++i) {
        const double hz = f0(double(i) / Rate);
        double v = 0.0;
        for (int h = 1; h * hz < 5000.0; ++h)
            v += (0.25 + 1.0 / (1.0 + std::pow((h * hz - 1000.0) / 250.0, 2))) / h * std::sin(h * phase);
        buffer.samples[size_t(i)] = float(0.3 * v);
        phase += 2.0 * Pi * hz / Rate;
    }
    return buffer;
}

// A3 for half a second, then E4 (legato): notes [600, 24600) and [24600, 47880).
QString writeLegato(const QString &path)
{
    return AudioFile::writeWav(path, voice([](double t) { return t < 0.5 ? 220.0 : 330.0; }, 1.0)) ? path : QString();
}

std::vector<float> render(AudioSession &session, const QString &file)
{
    if (!ok(session.renderToFile(file, QStringLiteral("0"), ticks(48000), Rate, 1))) return {};
    const AudioBufferPtr samples = AudioFile::readWav(file);
    return samples ? std::vector<float>(samples->channel(0), samples->channel(0) + samples->frames) : std::vector<float>{};
}

// Largest |cents − expected| of the rendered frames centred in [start, end); +inf if one is unvoiced.
double worstError(const std::vector<float> &x, qint64 start, qint64 end, double expected)
{
    const auto a = AudioPitch::analyse(x.data(), qint64(x.size()), Rate, start, end, {}, {});
    double worst = a ? 0.0 : std::numeric_limits<double>::infinity();
    for (const AudioPitch::Frame &frame : a->frames)
        worst = frame.voiced ? std::max(worst, std::fabs(frame.cents - expected)) : std::numeric_limits<double>::infinity();
    return worst;
}

double pitchAt(const AudioPitchSource &source, qint64 sample)
{
    return source.noteAt(QString::number(sample)).value(QStringLiteral("pitchCents")).toDouble();
}

struct Fixture {
    QTemporaryDir dir;
    AudioSession session;
    AudioPitchSource pitch;
    QString id;
    // The legato fixture, or `buffer` when one is given.
    explicit Fixture(const AudioBuffer *buffer = nullptr)
    {
        const QString track = session.addTrack(QStringLiteral("Vocal"));
        const QString wav = dir.filePath(QStringLiteral("source.wav"));
        const QString path = buffer ? (AudioFile::writeWav(wav, *buffer) ? wav : QString()) : writeLegato(wav);
        id = session.importMedia(path, track).value(QStringLiteral("resourceId")).toString();
        pitch.setSession(&session);
        pitch.setResourceId(id);
        pitch.setColumnCount(200);
    }
    QVariantMap editTo(double cents)
    {
        return pitch.editFor(QStringLiteral("segment-600"), QStringLiteral("note-600"), QStringLiteral("pitch"),
                             QStringLiteral("600"), QStringLiteral("600"), cents, 1.0);
    }
};


} // namespace QindaTK::PitchFixture
