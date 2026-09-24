// SPDX-License-Identifier: LGPL-3.0-or-later
#include "media_player.h"
#include "media_export.h"

#include <QSet>
#include <algorithm>

namespace QindaTK {
namespace {
QString decimal(std::optional<qint64> value)
{
    return value ? QString::number(*value) : QString();
}
} // namespace

MediaPlayer::MediaPlayer(QObject *parent) : QObject(parent)
{
    m_timeMap = new TimeMap(this);
    m_engine = new MediaPlaybackEngine(this);
    m_transport = new TransportModel(this);
    m_transport->setTimeMap(m_timeMap);
    m_transport->setAdapter(m_engine);
    connect(m_engine, &MediaPlaybackEngine::framePresented, this, &MediaPlayer::frameChanged);
    connect(m_engine, &MediaPlaybackEngine::targetChanged, this, &MediaPlayer::frameChanged);
    connect(m_engine, &MediaPlaybackEngine::statisticsChanged, this, &MediaPlayer::statisticsChanged);
}

MediaPlayer::~MediaPlayer()
{
    // AGENT-GUARD: detach the transport before the engine goes away so no
    // acknowledgment or snapshot reaches a half-destroyed player.
    m_transport->setAdapter(nullptr);
    delete m_engine;
    m_engine = nullptr;
}

void MediaPlayer::setResolver(ResourceResolver *resolver)
{
    if (m_resolver == resolver) return;
    m_resolver = resolver;
    rebuild();
    emit sourceChanged();
}

void MediaPlayer::setCatalog(MediaCatalog *catalog)
{
    if (m_catalog == catalog) return;
    m_catalog = catalog;
    rebuild();
    emit sourceChanged();
}

void MediaPlayer::setResourceId(const QString &resourceId)
{
    if (m_resourceId == resourceId) return;
    m_resourceId = resourceId;
    rebuild();
    emit sourceChanged();
}

void MediaPlayer::setTimeline(TimelineModel *timeline)
{
    if (m_timeline == timeline) return;
    m_timeline = timeline;
    rebuild();
    emit sourceChanged();
}

void MediaPlayer::setFrameWidth(int width)
{
    width = std::clamp(width, 1, 8192);
    if (width == m_frameSize.width()) return;
    m_frameSize.setWidth(width);
    if (m_timeline) rebuild();
    emit sourceChanged();
}

void MediaPlayer::setFrameHeight(int height)
{
    height = std::clamp(height, 1, 8192);
    if (height == m_frameSize.height()) return;
    m_frameSize.setHeight(height);
    if (m_timeline) rebuild();
    emit sourceChanged();
}

void MediaPlayer::setAudioChannels(int channels)
{
    channels = std::clamp(channels, 1, 8);
    if (channels == m_audioChannels) return;
    m_audioChannels = channels;
    rebuild();
    emit sourceChanged();
}

void MediaPlayer::setUseProxies(bool enabled)
{
    if (enabled == m_useProxies) return;
    m_useProxies = enabled;
    rebuild();
    emit sourceChanged();
}

void MediaPlayer::setMaxPreviewWidth(int width)
{
    width = std::clamp(width, 0, 8192);
    if (width == m_previewSize.width()) return;
    m_previewSize.setWidth(width);
    m_engine->setOutputSize(m_previewSize);
    emit sourceChanged();
}

void MediaPlayer::setMaxPreviewHeight(int height)
{
    height = std::clamp(height, 0, 8192);
    if (height == m_previewSize.height()) return;
    m_previewSize.setHeight(height);
    m_engine->setOutputSize(m_previewSize);
    emit sourceChanged();
}

MediaPlayer::Resolved MediaPlayer::resolve(const QString &resourceId) const
{
    Resolved resolved;
    if (!m_resolver) {
        resolved.uri = resourceId;
        resolved.online = !mediaLocalPath(resourceId).isEmpty();
        return resolved;
    }
    const QVariantMap entry = m_resolver->lookup(resourceId);
    resolved.uri = entry.value(QStringLiteral("uri")).toString();
    resolved.revision = entry.value(QStringLiteral("revision")).toString();
    resolved.online = !entry.isEmpty()
        && entry.value(QStringLiteral("availability")).toString() != QLatin1String("missing");
    return resolved;
}

QString MediaPlayer::proxyFor(const QString &resourceId) const
{
    return m_useProxies && m_catalog ? m_catalog->activeProxyUri(resourceId) : QString();
}

void MediaPlayer::reload()
{
    rebuild();
}

void MediaPlayer::rebuild()
{
    ++m_generation;
    for (const QMetaObject::Connection &link : std::as_const(m_sourceLinks)) disconnect(link);
    m_sourceLinks.clear();
    if (m_resolver)
        m_sourceLinks << connect(m_resolver, &ResourceResolver::resourceChanged, this, &MediaPlayer::rebuild);
    if (m_catalog)
        m_sourceLinks << connect(m_catalog, &MediaCatalog::proxyChanged, this, &MediaPlayer::rebuild);
    if (m_timeline) {
        m_sourceLinks << connect(m_timeline, &TimelineModel::revisionChanged, this, &MediaPlayer::rebuild)
                      << connect(m_timeline, &TimelineModel::contentReset, this, &MediaPlayer::rebuild)
                      << connect(m_timeline, &TimelineModel::timeMapChanged, this, &MediaPlayer::rebuild);
        watchTimeMap();
        buildTimelineProgram();
        return;
    }
    watchTimeMap();
    m_proxyCount = 0;
    if (m_resourceId.isEmpty()) {
        applyProgram(nullptr, QStringLiteral("empty"), {});
        return;
    }
    const Resolved original = resolve(m_resourceId);
    if (!original.online) {
        applyProgram(nullptr, QStringLiteral("missing"), tr("Media %1 is offline").arg(m_resourceId));
        return;
    }
    const QString proxy = proxyFor(m_resourceId);
    const QString playbackUri = proxy.isEmpty() ? original.uri : proxy;
    setStateText(QStringLiteral("loading"), {});
    const quint64 generation = m_generation;
    const QPointer<MediaPlayer> self(this);
    const bool proxied = !proxy.isEmpty();
    // The original is indexed even when a proxy plays: identity is the original's.
    m_engine->probe(original.uri, [self, generation, original, playbackUri, proxied](
                                      MediaSourceInfoPtr info, OperationError error) {
        if (!self || self->m_generation != generation) return;
        self->finishProbe(original, playbackUri, proxied, info, error);
    });
}

void MediaPlayer::watchTimeMap()
{
    TimeMap *map = m_timeline ? m_timeline->timeMap() : nullptr;
    if (map == m_watchedTimeMap) return;
    disconnect(m_timeMapLink);
    m_watchedTimeMap = map;
    if (map) m_timeMapLink = connect(map, &TimeMap::changed, this, &MediaPlayer::rebuild);
}

void MediaPlayer::buildTimelineProgram()
{
    m_proxyCount = 0;
    QSet<QString> proxied;
    const auto resolver = [this, &proxied](const QString &id) -> std::pair<QString, QString> {
        const Resolved resolved = resolve(id);
        if (!resolved.online) return {QString(), resolved.revision};
        const QString proxy = proxyFor(id);
        if (proxy.isEmpty()) return {resolved.uri, resolved.revision};
        proxied.insert(id);
        return {proxy, resolved.revision + QStringLiteral(":proxy")};
    };
    QString error;
    const auto program = MediaProgram::fromTimeline(*m_timeline, resolver, m_frameSize, m_audioChannels, &error);
    m_proxyCount = int(proxied.size());
    if (!program) {
        applyProgram(nullptr, QStringLiteral("failed"), error);
        return;
    }
    const QString state = program->isEmpty() ? QStringLiteral("empty")
        : !program->unresolved.isEmpty() ? QStringLiteral("missing") : QStringLiteral("ready");
    applyProgram(program, state, program->unresolved.isEmpty() ? QString()
        : tr("Missing media: %1").arg(program->unresolved.join(QStringLiteral(", "))));
}

void MediaPlayer::finishProbe(const Resolved &original, const QString &playbackUri, bool proxy,
                              const MediaSourceInfoPtr &info, const OperationError &error)
{
    if (error || !info) {
        applyProgram(nullptr, error.code == ErrorCode::Unavailable ? QStringLiteral("missing")
                                                                   : QStringLiteral("failed"),
                     error.message);
        return;
    }
    if (!info->hasVideo()) {
        applyProgram(nullptr, QStringLiteral("failed"), tr("The media has no video frames to show"));
        return;
    }
    MediaClipSpan span;
    span.resourceId = m_resourceId;
    span.uri = original.uri;
    span.revision = original.revision;
    const int rate = info->hasAudio() ? info->audioSampleRate : 48000;
    m_proxyCount = proxy ? 1 : 0;
    applyProgram(MediaProgram::forSource(span, playbackUri, proxy, info, rate, m_audioChannels),
                 QStringLiteral("ready"), {});
}

void MediaPlayer::applyProgram(MediaProgramPtr program, const QString &state, const QString &error)
{
    m_program = std::move(program);
    if (m_timeline && m_timeline->timeMap()) {
        m_transport->setTimeMap(m_timeline->timeMap());
    } else {
        if (m_program && m_program->kind == MediaProgram::Kind::Source) {
            m_timeMap->setSampleRate(m_program->sampleRate);
            m_timeMap->setFrameRate(m_program->frameRate.numerator, m_program->frameRate.denominator);
        }
        m_transport->setTimeMap(m_timeMap);
    }
    m_engine->setOutputSize(m_previewSize);
    m_engine->setProgram(m_program);
    setStateText(state, error);
    emit programChanged();
    emit frameChanged();
}

void MediaPlayer::setStateText(const QString &state, const QString &error)
{
    if (m_state == state && m_lastError == error) return;
    m_state = state;
    m_lastError = error;
    emit stateChanged();
}

QVariantMap MediaPlayer::mediaInfo() const
{
    return m_program && m_program->sourceInfo ? m_program->sourceInfo->toVariantMap() : QVariantMap();
}

QVariantMap MediaPlayer::programInfo() const
{
    return m_program ? m_program->describe() : QVariantMap();
}

QString MediaPlayer::frameCount() const
{
    return QString::number(m_program ? m_program->frameCount : 0);
}

QString MediaPlayer::frameRateNumerator() const
{
    return m_program ? QString::number(m_program->frameRate.numerator) : QString();
}

QString MediaPlayer::frameRateDenominator() const
{
    return m_program ? QString::number(m_program->frameRate.denominator) : QString();
}

QString MediaPlayer::timebaseNumerator() const
{
    return m_program ? QString::number(m_program->timebase.numerator) : QString();
}

QString MediaPlayer::timebaseDenominator() const
{
    return m_program ? QString::number(m_program->timebase.denominator) : QString();
}

double MediaPlayer::pixelAspect() const
{
    if (!m_program || !m_program->sourceInfo || m_program->kind != MediaProgram::Kind::Source) return 1.0;
    const TimelineRate aspect = m_program->sourceInfo->sampleAspect;
    return double(aspect.numerator) / double(aspect.denominator);
}

qint64 MediaPlayer::presentedIndex() const
{
    return m_engine->presentedFrame();
}

QString MediaPlayer::presentedFrame() const
{
    const qint64 frame = presentedIndex();
    return frame >= 0 ? QString::number(frame) : QString();
}

// presented* describe the program that produced the frame on screen, which
// trails m_program until the new program's first frame is presented.
QString MediaPlayer::presentedTicks() const
{
    const qint64 frame = presentedIndex();
    const MediaProgramPtr shown = m_engine->presentedProgram();
    return shown && frame >= 0 ? decimal(shown->frameStartTicks(frame)) : QString();
}

QString MediaPlayer::presentedFrameKey() const
{
    const qint64 frame = presentedIndex();
    const MediaProgramPtr shown = m_engine->presentedProgram();
    if (!shown || frame < 0) return {};
    return QStringLiteral("%1@%2#%3").arg(shown->programId, shown->revision).arg(frame);
}

QString MediaPlayer::presentedTimecode() const
{
    const qint64 frame = presentedIndex();
    TimeMap *map = m_transport->timeMap();
    if (!map || frame < 0) return {};
    const QVariantMap formatted = map->formatTimecode(QString::number(frame));
    return formatted.value(QStringLiteral("ok")).toBool() ? formatted.value(QStringLiteral("value")).toString()
                                                         : QString();
}

QVariantList MediaPlayer::presentedSources() const
{
    QVariantList list;
    for (const MediaSourceFrameRef &source : m_engine->presented().sources)
        list.append(QVariantMap{{QStringLiteral("clipId"), source.clipId},
                                {QStringLiteral("resourceId"), source.resourceId},
                                {QStringLiteral("frame"), QString::number(source.frame)},
                                {QStringLiteral("pts"), QString::number(source.pts)}});
    return list;
}

QStringList MediaPlayer::missingResourceIds() const
{
    return m_engine->presented().missingResourceIds;
}

QString MediaPlayer::frameError() const
{
    return m_engine->presented().error.message;
}

bool MediaPlayer::presentedCorrupt() const
{
    return m_engine->presented().corrupt;
}

QString MediaPlayer::targetFrame() const
{
    const qint64 frame = m_engine->targetFrame();
    return frame >= 0 ? QString::number(frame) : QString();
}

bool MediaPlayer::pending() const
{
    return m_engine->pending();
}

QVariantMap MediaPlayer::statistics() const
{
    const MediaPlaybackStatistics &stats = m_engine->statistics();
    return {{QStringLiteral("requested"), qulonglong(stats.requested)},
            {QStringLiteral("presented"), qulonglong(stats.presented)},
            {QStringLiteral("stale"), qulonglong(stats.stale)},
            {QStringLiteral("cancelled"), qulonglong(stats.cancelled)},
            {QStringLiteral("failed"), qulonglong(stats.failed)},
            {QStringLiteral("dropped"), qulonglong(stats.dropped)},
            {QStringLiteral("cacheHits"), qulonglong(stats.cacheHits)},
            {QStringLiteral("cachedFrames"), m_engine->cachedFrames()},
            {QStringLiteral("inFlight"), m_engine->inFlight()}};
}

QImage MediaPlayer::presentedImage() const
{
    return m_engine->presented().image;
}

quint64 MediaPlayer::presentedRevision() const
{
    return m_engine->presentedRevision();
}

QString MediaPlayer::requestFrame(qint64 frame)
{
    if (!m_program || m_program->isEmpty() || frame < 0 || frame >= m_program->frameCount) return {};
    const auto sample = m_program->frameStartSample(frame);
    return sample ? m_transport->requestSeek(QString::number(*sample)) : QString();
}

QString MediaPlayer::seekFrame(const QString &frame)
{
    const auto index = TimelineTime::parseDecimal(frame);
    return index ? requestFrame(*index) : QString();
}

QString MediaPlayer::stepFrame(int delta)
{
    if (!m_program || m_program->isEmpty()) return {};
    const qint64 base = m_engine->targetFrame() >= 0 ? m_engine->targetFrame() : 0;
    return requestFrame(std::clamp<qint64>(base + delta, 0, m_program->frameCount - 1));
}

QString MediaPlayer::seekTicks(const QString &ticks)
{
    const QString frame = frameAtTicks(ticks);
    return frame.isEmpty() ? QString() : seekFrame(frame);
}

QString MediaPlayer::play()
{
    return m_transport->requestPlay();
}

QString MediaPlayer::stop()
{
    return m_transport->requestStop();
}

QString MediaPlayer::togglePlayback()
{
    return m_transport->playing() ? stop() : play();
}

QString MediaPlayer::frameStartTicks(const QString &frame) const
{
    const auto index = TimelineTime::parseDecimal(frame);
    return m_program && index ? decimal(m_program->frameStartTicks(*index)) : QString();
}

QString MediaPlayer::frameAtTicks(const QString &ticks) const
{
    const auto value = TimelineTime::parseDecimal(ticks);
    if (!m_program || m_program->isEmpty() || !value) return {};
    if (m_program->kind == MediaProgram::Kind::Source)
        return decimal(m_program->sourceInfo->frameAtTicks(*value, m_program->timebase));
    const auto relative = TimelineTime::subtract(*value, m_program->originTicks);
    if (!relative || *relative < 0) return {};
    const auto frame = TimelineTime::rescale(*relative, m_program->timebase, m_program->frameRate, TimeMap::Floor);
    return frame && *frame < m_program->frameCount ? QString::number(*frame) : QString();
}

QVariantMap MediaPlayer::exportJob(const QString &outputPath, const QString &label)
{
    if (!m_timeline) {
        setStateText(m_state, tr("Only a timeline program can be exported"));
        return {};
    }
    const auto original = [this](const QString &id) -> std::pair<QString, QString> {
        const Resolved resolved = resolve(id);
        return resolved.online ? std::pair{resolved.uri, resolved.revision} : std::pair{QString(), resolved.revision};
    };
    QString error;
    const auto program = MediaProgram::fromTimeline(*m_timeline, original, m_frameSize, m_audioChannels, &error);
    QHash<QString, QString> hashes;
    if (program && m_resolver) {
        for (const QVector<MediaClipSpan> *spans : {&program->video, &program->audio})
            for (const MediaClipSpan &span : *spans)
                hashes.insert(span.resourceId,
                              m_resolver->lookup(span.resourceId).value(QStringLiteral("sha256")).toString());
    }
    const QVariantMap snapshot = program
        ? MediaExport::queueSnapshot(*program, mediaLocalPath(outputPath),
                                     label.isEmpty() ? tr("Export program") : label, hashes, &error)
        : QVariantMap();
    if (snapshot.isEmpty()) setStateText(m_state, error);
    return snapshot;
}

} // namespace QindaTK
