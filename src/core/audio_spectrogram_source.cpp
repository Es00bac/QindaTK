// SPDX-License-Identifier: LGPL-3.0-or-later
// AudioSpectrogramSource: bounded spectrogram requests over cached spectrum
// tiles, computed on the thread pool (D-136, docs/audio-editing.md).
#include "audio_analysis_source.h"

#include <QFutureWatcher>
#include <QtConcurrentRun>
#include <algorithm>
#include <cmath>
#include <tuple>

namespace QindaTK {
namespace {

struct SpectrogramReply {
    std::optional<AudioSpectrogramImage> image;
    std::vector<AudioSpectrumTilePtr> computed;
};

} // namespace

AudioSpectrogramSource::AudioSpectrogramSource(QObject *parent)
    : AudioAnalysisSource(parent)
{
    m_request.columns = columnCount();
}

void AudioSpectrogramSource::settingsTouched()
{
    // Every setting shapes the request, so a running one is never published
    // after a change.
    abandon();
    schedule();
}

#define QTK_SPECTRAL_SETTER(setter, field, type)            \
    void AudioSpectrogramSource::setter(type value)         \
    {                                                       \
        if (m_request.field == value) return;               \
        m_request.field = value;                            \
        emit settingsChanged();                             \
        settingsTouched();                                  \
    }
QTK_SPECTRAL_SETTER(setFftSize, fftSize, int)
QTK_SPECTRAL_SETTER(setHopSize, hop, int)
QTK_SPECTRAL_SETTER(setRows, rows, int)
QTK_SPECTRAL_SETTER(setMinimumHz, minimumHz, double)
QTK_SPECTRAL_SETTER(setMaximumHz, maximumHz, double)
QTK_SPECTRAL_SETTER(setFloorDb, floorDb, double)
QTK_SPECTRAL_SETTER(setMaxFrames, maxFrames, int)
#undef QTK_SPECTRAL_SETTER

void AudioSpectrogramSource::setFrequencyScale(const QString &scale)
{
    const bool logarithmic = scale != QLatin1String("linear");
    if (m_request.logarithmic == logarithmic) return;
    m_request.logarithmic = logarithmic;
    emit settingsChanged();
    settingsTouched();
}

void AudioSpectrogramSource::invalidate()
{
    m_cache.clear();
    m_cacheBytes = 0;
}

QString AudioSpectrogramSource::cacheKey(int channel, int fftSize, qint64 hop, qint64 index) const
{
    return QStringLiteral("%1/%2/%3/%4/%5").arg(m_revision).arg(channel).arg(fftSize).arg(hop).arg(index);
}

void AudioSpectrogramSource::store(const QString &key, const AudioSpectrumTilePtr &tile)
{
    auto it = m_cache.find(key);
    if (it != m_cache.end()) {
        it->used = ++m_useSerial;
        return;
    }
    m_cache.insert(key, {tile, ++m_useSerial});
    m_cacheBytes += tile->bytes();
    // AGENT-GUARD: the cache is bounded by bytes; the least recently used
    // tiles go first and the newest tile always stays.
    while (m_cacheBytes > kCacheBudgetBytes && m_cache.size() > 1) {
        auto oldest = m_cache.begin();
        for (auto scan = m_cache.begin(); scan != m_cache.end(); ++scan)
            if (scan->used < oldest->used) oldest = scan;
        m_cacheBytes -= oldest->tile->bytes();
        m_cache.erase(oldest);
    }
}

void AudioSpectrogramSource::run()
{
    const AudioBufferPtr buffer = currentBuffer();
    const auto publishFailure = [this](const QString &state, const QString &error) {
        m_image.reset();
        m_tiles.clear();
        m_boundaries.clear();
        emit resultChanged();
        setState(state, error);
    };
    if (!buffer) {
        if (!session() || resourceId().isEmpty()) publishFailure(QStringLiteral("empty"), {});
        else publishFailure(QStringLiteral("failed"), tr("The source %1 has no decoded samples").arg(resourceId()));
        return;
    }
    AudioSpectrogramRequest request = m_request;
    request.channel = channel();
    request.columns = columnCount();
    std::tie(request.start, request.end) = viewRange();
    int stride = 1;
    if (const QString problem = AudioSpectrogram::normalize(request, *buffer, &stride); !problem.isEmpty()) {
        publishFailure(QStringLiteral("failed"), problem);
        return;
    }
    const CancellationToken token = restartCancellation();
    const quint64 serial = m_serial;
    const quint64 revision = m_revision;
    // Tiles already computed for this revision, channel, size and hop.
    const QString prefix = cacheKey(request.channel, request.fftSize, request.hop, 0).chopped(1);
    QHash<qint64, AudioSpectrumTilePtr> cached;
    for (auto it = m_cache.begin(); it != m_cache.end(); ++it)
        if (it.key().startsWith(prefix)) {
            it->used = ++m_useSerial;
            cached.insert(it->tile->index, it->tile);
        }
    setState(QStringLiteral("loading"));
    auto *watcher = new QFutureWatcher<SpectrogramReply>(this);
    connect(watcher, &QFutureWatcher<SpectrogramReply>::finished, this,
            [this, watcher, serial, revision, request] {
        const SpectrogramReply reply = watcher->result();
        watcher->deleteLater();
        if (!stillCurrent(serial, revision)) return; // superseded or cancelled: never published
        for (const AudioSpectrumTilePtr &tile : reply.computed)
            store(cacheKey(tile->channel, tile->fftSize, tile->hop, tile->index), tile);
        if (!reply.image) {
            setState(QStringLiteral("failed"), tr("The spectrogram could not be computed"));
            return;
        }
        m_image = reply.image;
        m_floorUsed = request.floorDb;
        const AudioSpectrogramImage &image = *m_image;
        QVariantList tiles;
        for (int column = 0; column < image.columns; ++column)
            for (int row = 0; row < image.rows; ++row) {
                const double intensity = image.intensity(column, row, m_floorUsed);
                if (intensity <= 0.0) continue; // at or below the floor: nothing to draw
                tiles.push_back(QVariantMap{{QStringLiteral("x0"), double(column) / image.columns},
                                            {QStringLiteral("x1"), double(column + 1) / image.columns},
                                            {QStringLiteral("lowHz"), image.bandEdges[size_t(row)]},
                                            {QStringLiteral("highHz"), image.bandEdges[size_t(row) + 1]},
                                            {QStringLiteral("intensity"), intensity}});
            }
        m_tiles = tiles;
        m_boundaries.clear();
        for (qint64 boundary : image.boundaries) m_boundaries.push_back(QString::number(boundary));
        emit resultChanged();
        setState(QStringLiteral("ready"));
    });
    // AGENT-NOTE (D-136): FFTs run on the thread pool; the reply carries the
    // tiles it computed so the cache is only touched on the GUI thread.
    watcher->setFuture(QtConcurrent::run([buffer, request, stride, cached, token] {
        SpectrogramReply reply;
        QHash<qint64, AudioSpectrumTilePtr> tiles = cached;
        const auto tile = [&](qint64 index) -> AudioSpectrumTilePtr {
            if (const auto it = tiles.constFind(index); it != tiles.constEnd()) return it.value();
            AudioSpectrumTilePtr computed =
                AudioSpectrogram::computeTile(*buffer, request.channel, request.fftSize, request.hop, index, token);
            if (computed) {
                tiles.insert(index, computed);
                reply.computed.push_back(computed);
            }
            return computed;
        };
        reply.image = AudioSpectrogram::render(*buffer, request, stride, tile, token);
        return reply;
    }));
}

QVariantMap AudioSpectrogramSource::cell(int column, int row) const
{
    if (!m_image || column < 0 || row < 0 || column >= m_image->columns || row >= m_image->rows)
        return {{QStringLiteral("ok"), false}};
    return {{QStringLiteral("ok"), true},
            {QStringLiteral("decibels"), double(m_image->at(column, row))},
            {QStringLiteral("intensity"), m_image->intensity(column, row, m_floorUsed)},
            {QStringLiteral("lowHz"), m_image->bandEdges[size_t(row)]},
            {QStringLiteral("highHz"), m_image->bandEdges[size_t(row) + 1]},
            {QStringLiteral("startSample"), QString::number(m_image->boundaries[size_t(column)])},
            {QStringLiteral("endSample"), QString::number(m_image->boundaries[size_t(column) + 1])}};
}

int AudioSpectrogramSource::rowForFrequency(double hz) const
{
    if (!m_image) return -1;
    const auto &edges = m_image->bandEdges;
    for (int row = 0; row < m_image->rows; ++row)
        if (hz >= edges[size_t(row)] && (hz < edges[size_t(row) + 1] || (row + 1 == m_image->rows && hz <= edges.back())))
            return row;
    return -1;
}

} // namespace QindaTK
