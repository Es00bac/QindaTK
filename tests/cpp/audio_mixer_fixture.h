// SPDX-License-Identifier: LGPL-3.0-or-later
// Shared by tst_audio_mixer and tst_audio_mixer_dsp (WP-062): a session whose
// tracks play constants, and the frame the device graph renders.
#pragma once

#include "audio_mixer_model.h"
#include "audio_parameter_binding.h"
#include "audio_session.h"

#include <QTemporaryDir>
#include <cmath>
#include <numbers>
#include <optional>
#include <vector>

namespace QindaTK::MixerFixture {

inline constexpr int Rate = 48000;
inline constexpr qint64 TicksPerSample = TimelineTime::kDefaultTicksPerSecond / Rate;
inline const double Quarter = std::numbers::pi / 4.0;

inline bool ok(const QVariantMap &result) { return result.value(QStringLiteral("ok")).toBool(); }
inline double linear(double db) { return std::pow(10.0, db / 20.0); }
inline bool near(double actual, double expected) { return std::abs(actual - expected) < 1e-6; }

struct Fixture {
    QTemporaryDir dir;
    AudioSession session;
    AudioMixerModel *mixer = session.mixer();
    std::vector<double> scratch = std::vector<double>(AudioRenderGraph::kMaxBlock);

    // A track playing one second of a constant; a second value makes the file stereo.
    QString track(const QString &name, float left, std::optional<float> right = std::nullopt)
    {
        const QString id = session.addTrack(name);
        AudioBuffer buffer;
        buffer.sampleRate = Rate;
        buffer.channels = right ? 2 : 1;
        buffer.frames = Rate;
        buffer.samples.assign(std::size_t(Rate) * std::size_t(buffer.channels), left);
        if (right) std::fill(buffer.samples.begin() + Rate, buffer.samples.end(), *right);
        const QString file = dir.filePath(name + QStringLiteral(".wav"));
        if (id.isEmpty() || !AudioFile::writeWav(file, buffer) || !ok(session.importMedia(file, id))) return {};
        return QStringLiteral("track:") + id;
    }
    QString bus(const QString &name) { return mixer->addBus(name).value(QStringLiteral("channelId")).toString(); }
    // What the outputs receive at `sample`.
    std::vector<float> frame(qint64 sample = 0, int channels = 2)
    {
        std::vector<float> out(std::size_t(channels), 0.0f);
        session.compileGraph(Rate)->render(sample, 1, out.data(), channels, scratch.data());
        return out;
    }
    float left(qint64 sample = 0) { return frame(sample).at(0); }
    // A mixer parameter's static value, as a committed automation edit.
    bool set(const QString &owner, const QString &kind, double value)
    {
        const QString id = mixer->ensureParameter(owner, kind);
        const AutomationModel::Lane *lane = session.automation()->lane(id);
        if (!lane) return false;
        if (lane->parameter.defaultValue == value) return true;
        return ok(session.automation()->applyEdit({{QStringLiteral("kind"), QStringLiteral("editParameter")},
                                                   {QStringLiteral("parameterId"), id},
                                                   {QStringLiteral("default"), value}}));
    }
    bool point(const QString &parameterId, qint64 sample, double value)
    {
        return ok(session.automation()->applyEdit({{QStringLiteral("kind"), QStringLiteral("addPoint")},
                                                   {QStringLiteral("parameterId"), parameterId},
                                                   {QStringLiteral("ticks"), QString::number(sample * TicksPerSample)},
                                                   {QStringLiteral("value"), value}}));
    }
    bool moveTrack(const QString &channel, const QString &before)
    {
        TimelineEditRequest request;
        request.kind = TimelineEditKind::ReorderTrack;
        request.trackId = AudioMixer::trackOf(channel);
        request.beforeTrackId = AudioMixer::trackOf(before);
        return !session.timeline()->apply(request, {});
    }
    QVariant value(const QString &channel, const char *role) { return mixer->channel(channel).value(QLatin1String(role)); }
};

} // namespace QindaTK::MixerFixture
