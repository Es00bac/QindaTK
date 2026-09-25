// SPDX-License-Identifier: LGPL-3.0-or-later
// AudioSession: graph compilation and the offline render (split from
// audio_session.cpp). The device and the file render the same graph code.
#include "audio_session.h"

#include <QFileInfo>
#include <QUrl>
#include <algorithm>
#include <vector>

namespace QindaTK {
namespace {

QString localPath(const QString &file)
{
    const QUrl url(file);
    return url.isLocalFile() ? url.toLocalFile() : file;
}

QVariantMap failure(const QString &message)
{
    return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
}

} // namespace

AudioBufferPtr AudioSession::mediaBuffer(const QString &resourceId, int sampleRate)
{
    // Edited samples (D-134); the resampled copy is dropped per source revision.
    const AudioBufferPtr decoded = m_decoded.contains(resourceId) ? m_edits->buffer(resourceId) : nullptr;
    if (!decoded || decoded->sampleRate == sampleRate) return decoded;
    const QString key = QStringLiteral("%1/%2").arg(sampleRate).arg(resourceId);
    if (const AudioBufferPtr cached = m_resampled.value(key)) return cached;
    AudioBufferPtr converted = AudioFile::resample(*decoded, sampleRate);
    m_resampled.insert(key, converted);
    return converted;
}

AudioRenderGraphPtr AudioSession::compileGraph(int sampleRate, bool offline)
{
    AudioRenderGraph::Sources sources;
    sources.timeline = m_timeline;
    sources.media = [this](const QString &id, int rate) { return mediaBuffer(id, rate); };
    sources.schedule = m_schedule->acquire();
    sources.sampleRate = sampleRate;
    sources.mixer = m_mixer ? &m_mixer->content() : nullptr;
    if (m_mixer) sources.meterFirst = m_mixer->watchedMeters();
    sources.offline = offline;
    sources.midi = &m_midi->content();
    // Exports play the committed comps, never a preview or an audition (D-176).
    sources.comp = [this, offline](const QString &id) { return m_comps->playback(id, offline); };
    const AudioPluginInsertsPtr inserts = m_plugins ? m_plugins->inserts() : nullptr; // alive until compile() returns
    sources.plugins = inserts.get();
    // Device ports play on the stream that runs now; a rate or device change
    // emits formatChanged and recompiles (docs/audio-mixer.md, D-112).
    if (m_engine && !offline && m_engine->streamFormat().outputChannels > 0) {
        sources.deviceId = m_engine->activeConfiguration().value(QStringLiteral("deviceId")).toString();
        sources.deviceChannels = m_engine->streamFormat().outputChannels;
    }
    return AudioRenderGraph::compile(sources);
}

QVariantMap AudioSession::renderToFile(const QString &file, const QString &startTicks, const QString &endTicks,
                                       int sampleRate, int channels)
{
    clearError();
    const int rate = sampleRate > 0 ? sampleRate : this->sampleRate();
    // Plugins run for this render only, at its rate (D-192); the device
    // passes them dry until the claim ends with this function.
    const auto plugins = m_plugins->beginOffline(rate);
    const AudioRenderGraphPtr graph = compileGraph(rate, true);
    const TimelineRate timebase = m_timeline->timebase();
    const auto toSamples = [&](const QString &ticks) -> std::optional<qint64> {
        const auto value = TimelineTime::parseDecimal(ticks);
        return value ? TimelineTime::rescale(*value, timebase, {rate, 1}, TimeMap::NearestAway) : std::nullopt;
    };
    const auto start = toSamples(startTicks);
    const auto end = endTicks.isEmpty() ? std::optional<qint64>(graph->length()) : toSamples(endTicks);
    if (!start || !end || *start < 0 || *end < *start || channels < 1 || channels > 2) {
        fail(tr("The render range or channel count is invalid"));
        return failure(m_lastError);
    }
    const QString path = QFileInfo(localPath(file)).absoluteFilePath();
    AudioWavWriter writer;
    QString error;
    if (!writer.open(path, rate, channels, &error)) {
        fail(error);
        return failure(error);
    }
    std::vector<float> block(size_t(AudioRenderGraph::kMaxBlock) * channels);
    std::vector<double> scratch(AudioRenderGraph::kMaxBlock);
    // Latency compensation delays the whole mix by the plugins' latency: the
    // render runs that far past the end and drops as many leading frames.
    const qint64 latency = graph->pluginLatency();
    qint64 skip = latency;
    for (qint64 at = *start; at < *end + latency;) {
        const int count = int(std::min<qint64>(AudioRenderGraph::kMaxBlock, *end + latency - at));
        std::fill_n(block.begin(), size_t(count) * channels, 0.0f);
        graph->render(at, count, block.data(), channels, scratch.data());
        const qint64 dropped = std::min<qint64>(skip, count);
        if (dropped < count) writer.write(block.data() + size_t(dropped) * channels, count - dropped);
        skip -= dropped;
        at += count;
    }
    if (!writer.finish(&error)) {
        fail(error);
        return failure(error);
    }
    return {{QStringLiteral("ok"), true},
            {QStringLiteral("path"), path},
            {QStringLiteral("frames"), QString::number(*end - *start)},
            {QStringLiteral("startSample"), QString::number(*start)},
            {QStringLiteral("sampleRate"), rate},
            {QStringLiteral("channels"), channels},
            {QStringLiteral("missingMedia"), graph->missingResources()},
            {QStringLiteral("pluginLatency"), int(latency)}};
}

} // namespace QindaTK
