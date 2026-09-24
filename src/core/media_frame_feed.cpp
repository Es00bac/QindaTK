// SPDX-License-Identifier: LGPL-3.0-or-later
#include "media_frame_feed.h"

#include <QColorSpace>
#include <QMetaObject>
#include <QSet>
#include <algorithm>

namespace QindaTK {
namespace {
// NodeGraphModel accepts tagged sRGB only; other tags convert explicitly.
QImage taggedSrgb(const QImage &image)
{
    const QColorSpace srgb(QColorSpace::SRgb);
    if (!image.colorSpace().isValid()) {
        QImage tagged = image;
        tagged.setColorSpace(srgb);
        return tagged;
    }
    return image.colorSpace() == srgb ? image : image.convertedToColorSpace(srgb);
}
} // namespace

MediaFrameFeed::MediaFrameFeed(QObject *parent) : QObject(parent)
{
    m_worker = std::make_unique<MediaDecodeWorker>([this](MediaWorkerResult result) {
        QMetaObject::invokeMethod(this, [this, result]() mutable { onResult(std::move(result)); },
                                  Qt::QueuedConnection);
    });
}

MediaFrameFeed::~MediaFrameFeed()
{
    if (m_request)
        for (const CancellationSource &source : std::as_const(m_request->jobs)) source.cancel();
    m_worker.reset();
}

void MediaFrameFeed::setResolver(ResourceResolver *resolver)
{
    if (m_resolver == resolver) return;
    if (m_resolver) disconnect(m_resolver, nullptr, this, nullptr);
    m_resolver = resolver;
    if (resolver)
        connect(resolver, &ResourceResolver::resourceChanged, this, &MediaFrameFeed::onResourceChanged);
    emit bindingsChanged();
}

void MediaFrameFeed::setComposition(MotionComposition *composition)
{
    if (m_composition == composition) return;
    m_composition = composition;
    emit bindingsChanged();
}

void MediaFrameFeed::setRenderer(MotionFrameRenderer *renderer)
{
    if (m_renderer == renderer) return;
    m_renderer = renderer;
    emit bindingsChanged();
}

void MediaFrameFeed::setGraph(NodeGraphModel *graph)
{
    if (m_graph == graph) return;
    m_graph = graph;
    emit bindingsChanged();
}

void MediaFrameFeed::setSourceResources(const QVariantMap &map)
{
    if (m_sourceResources == map) return;
    m_sourceResources = map;
    emit bindingsChanged();
}

void MediaFrameFeed::setTicksPerSecondNumerator(const QString &value)
{
    const auto parsed = TimelineTime::parseDecimal(value);
    const auto rate = parsed ? TimelineRate::normalized(*parsed, m_rate.denominator) : std::nullopt;
    if (!rate || *rate == m_rate) return;
    m_rate = *rate;
    emit bindingsChanged();
}

void MediaFrameFeed::setTicksPerSecondDenominator(const QString &value)
{
    const auto parsed = TimelineTime::parseDecimal(value);
    const auto rate = parsed ? TimelineRate::normalized(m_rate.numerator, *parsed) : std::nullopt;
    if (!rate || *rate == m_rate) return;
    m_rate = *rate;
    emit bindingsChanged();
}

void MediaFrameFeed::setMaxSourceDimension(int pixels)
{
    pixels = std::clamp(pixels, 16, 8192);
    if (pixels == m_maxDimension) return;
    m_maxDimension = pixels;
    emit bindingsChanged();
}

QString MediaFrameFeed::resourceFor(const QString &sourceId) const
{
    if (sourceId.isEmpty()) return {};
    const QString mapped = m_sourceResources.value(sourceId).toString();
    if (!mapped.isEmpty()) return mapped;
    if (!m_resolver) return {};
    return m_resolver->lookup(sourceId).isEmpty() ? QString() : sourceId;
}

QString MediaFrameFeed::onlineUri(const QString &resourceId) const
{
    if (!m_resolver) return mediaLocalPath(resourceId);
    const QVariantMap entry = m_resolver->lookup(resourceId);
    if (entry.isEmpty() || entry.value(QStringLiteral("availability")).toString() == QLatin1String("missing")) return {};
    return entry.value(QStringLiteral("uri")).toString();
}

void MediaFrameFeed::cancel()
{
    if (!m_request) return;
    for (const CancellationSource &source : std::as_const(m_request->jobs)) source.cancel();
    m_request.reset();
    emit stateChanged();
}

QString MediaFrameFeed::prepare(const QString &timeTicks)
{
    const auto time = TimelineTime::parseDecimal(timeTicks);
    if (!time) {
        m_error = tr("Frame feeds need exact decimal ticks");
        emit stateChanged();
        return {};
    }
    cancel();
    Request request;
    request.id = QString::number(++m_serial);
    request.timeTicks = TimelineTime::toDecimal(*time);
    if (m_composition && m_renderer) {
        const QVariantMap evaluated = m_composition->evaluate(request.timeTicks);
        if (!evaluated.value(QStringLiteral("ok")).toBool()) {
            m_error = evaluated.value(QStringLiteral("error")).toString();
            emit stateChanged();
            return {};
        }
        for (const QVariant &value : evaluated.value(QStringLiteral("layers")).toList()) {
            const QVariantMap layer = value.toMap();
            Target target;
            target.sourceId = layer.value(QStringLiteral("sourceId")).toString();
            target.resourceId = resourceFor(target.sourceId);
            const auto local = TimelineTime::parseDecimal(layer.value(QStringLiteral("localTicks")).toString());
            if (target.resourceId.isEmpty() || !local || !layer.value(QStringLiteral("visible")).toBool()) continue;
            target.ticks = *local;
            request.targets.append(target);
        }
    }
    if (m_graph) {
        for (const QVariant &value : m_graph->nodes()) {
            const QVariantMap node = value.toMap();
            if (node.value(QStringLiteral("type")).toString() != QLatin1String("source")) continue;
            Target target;
            target.node = true;
            target.nodeId = node.value(QStringLiteral("nodeId")).toString();
            target.sourceId = node.value(QStringLiteral("parameters")).toMap().value(QStringLiteral("sourceId")).toString();
            target.resourceId = resourceFor(target.sourceId);
            if (target.resourceId.isEmpty()) continue;
            target.ticks = *time;
            request.targets.append(target);
        }
    }
    QHash<QString, quint64> shared; // one decode per (uri, ticks)
    for (Target &target : request.targets) {
        target.uri = onlineUri(target.resourceId);
        if (target.uri.isEmpty()) continue;
        const QString key = target.uri + QLatin1Char('|') + QString::number(target.ticks);
        if (const auto it = shared.constFind(key); it != shared.cend()) {
            target.job = it.value();
            continue;
        }
        MediaWorkerJob job;
        job.kind = MediaWorkerJob::Kind::SourceFrame;
        job.serial = ++m_serial;
        job.uri = target.uri;
        job.ticks = target.ticks;
        job.rate = m_rate;
        job.outputSize = QSize(m_maxDimension, m_maxDimension);
        target.job = job.serial;
        shared.insert(key, job.serial);
        request.jobs.insert(job.serial, job.cancel);
        ++request.remaining;
        m_worker->submit(std::move(job));
    }
    const QString id = request.id;
    m_request = std::move(request);
    m_error.clear();
    emit stateChanged();
    // With nothing to decode, finish on the next turn like any other request.
    if (m_request->remaining == 0)
        QMetaObject::invokeMethod(this, [this, id]() {
            if (m_request && m_request->id == id) apply();
        }, Qt::QueuedConnection);
    return id;
}

void MediaFrameFeed::onResult(MediaWorkerResult result)
{
    // Results of a cancelled or superseded request are never applied.
    if (!m_request || !m_request->jobs.contains(result.serial) || m_request->results.contains(result.serial)) return;
    m_request->results.insert(result.serial, result);
    if (--m_request->remaining == 0) apply();
}

void MediaFrameFeed::apply()
{
    if (!m_request) return;
    const Request request = *m_request;
    m_request.reset();
    QStringList missing;
    QStringList errors;
    QVariantList fed;
    QStringList resources;
    for (const Target &target : request.targets) {
        resources.append(target.resourceId);
        const MediaWorkerResult result = target.job ? request.results.value(target.job) : MediaWorkerResult();
        const bool offline = !target.job || result.error.code == ErrorCode::Unavailable;
        if (target.job && result.error && !offline) errors.append(QStringLiteral("%1: %2").arg(target.resourceId, result.error.message));
        if (offline) missing.append(target.resourceId);
        const QImage image = target.job && !result.error ? result.rendered.image : QImage();
        bool applied = false;
        if (target.node) {
            applied = m_graph && !image.isNull() && m_graph->setSourceImage(target.nodeId, taggedSrgb(image));
            if (!applied && m_graph) m_graph->clearSourceImage(target.nodeId);
        } else {
            applied = m_renderer && !image.isNull() && m_renderer->setSourceImage(target.sourceId, image);
            if (!applied && m_renderer) m_renderer->removeSource(target.sourceId);
        }
        if (applied)
            fed.append(QVariantMap{{QStringLiteral("target"), target.node ? QStringLiteral("node") : QStringLiteral("layer")},
                                   {QStringLiteral("sourceId"), target.sourceId}, {QStringLiteral("nodeId"), target.nodeId},
                                   {QStringLiteral("resourceId"), target.resourceId},
                                   {QStringLiteral("frame"), QString::number(result.frame)}});
    }
    missing.removeDuplicates();
    resources.removeDuplicates();
    m_missing = missing;
    m_fed = fed;
    m_preparedTicks = request.timeTicks;
    m_preparedResources = resources;
    m_error = errors.join(QStringLiteral("; "));
    emit stateChanged();
    if (errors.isEmpty()) emit prepared(request.id, request.timeTicks);
    else emit failed(request.id, m_error);
}

void MediaFrameFeed::onResourceChanged(const QString &resourceId)
{
    // A relink or refresh of a fed resource re-feeds the last prepared time.
    if (!m_request && !m_preparedTicks.isEmpty() && m_preparedResources.contains(resourceId))
        prepare(m_preparedTicks);
}

} // namespace QindaTK
