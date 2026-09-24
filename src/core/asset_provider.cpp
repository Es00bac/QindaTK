// SPDX-License-Identifier: LGPL-3.0-or-later
#include "asset_provider.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>
#include <algorithm>

namespace QindaTK {
namespace {
bool sameHeader(const SnapshotHeader &a, const SnapshotHeader &b)
{
    return a.schemaId == b.schemaId && a.documentId == b.documentId
        && a.revision == b.revision && a.resetGeneration == b.resetGeneration;
}

QVariantMap dimensions(const QRect &rect)
{
    return {{QStringLiteral("x"), rect.x()}, {QStringLiteral("y"), rect.y()},
            {QStringLiteral("width"), rect.width()}, {QStringLiteral("height"), rect.height()}};
}
} // namespace

AssetProvider::AssetProvider(QObject *parent)
    : QObject(parent), m_jobs(new JobModel(this)),
      m_backend(std::make_shared<FileAssetBackend>())
{
    connect(m_jobs, &JobModel::jobFinished, this,
            [this](const QString &id, JobModel::JobState state,
                   const QVariantMap &result, const QString &error) {
        finishFlight(id, state, result, error);
    });
}

void AssetProvider::setResolver(ResourceResolver *resolver)
{
    if (m_resolver == resolver) return;
    if (m_resolver) disconnect(m_resolver, nullptr, this, nullptr);
    for (const QString &id : m_subscriptions.keys()) markStale(id);
    m_resolver = resolver;
    clearCache();
    if (m_resolver) {
        connect(m_resolver, &ResourceResolver::resourceChanged,
                this, &AssetProvider::invalidateResource);
        connect(m_resolver, &QObject::destroyed, this, [this]() {
            m_resolver = nullptr;
            for (const QString &id : m_subscriptions.keys()) markStale(id);
            clearCache();
            emit resolverChanged();
        });
    }
    emit resolverChanged();
    emit changed();
}

void AssetProvider::setBackend(AssetBackendPtr backend)
{
    if (!backend) return;
    m_backend = std::move(backend);
    for (const QString &id : m_subscriptions.keys()) markStale(id);
    clearCache();
    emit changed();
}

void AssetProvider::setCacheBudgetBytes(qint64 bytes)
{
    if (bytes < 0 || bytes > qint64(4) * 1024 * 1024 * 1024
        || bytes == m_cacheBudget) return;
    m_cacheBudget = bytes;
    trimCache();
    emit cacheChanged();
}

QString AssetProvider::keyFor(const AssetRequest &request)
{
    const QVariantMap fields{{QStringLiteral("resourceId"), request.resourceId},
        {QStringLiteral("uri"), request.uri}, {QStringLiteral("revision"), request.revision},
        {QStringLiteral("kind"), request.kind}, {QStringLiteral("region"), dimensions(request.region)},
        {QStringLiteral("width"), request.size.width()},
        {QStringLiteral("height"), request.size.height()},
        {QStringLiteral("timeTicks"), QString::number(request.timeTicks)},
        {QStringLiteral("rateNumerator"), QString::number(request.rateNumerator)},
        {QStringLiteral("rateDenominator"), QString::number(request.rateDenominator)},
        {QStringLiteral("quality"), request.quality}, {QStringLiteral("columns"), request.columns},
        {QStringLiteral("pageIndex"), request.pageIndex}, {QStringLiteral("rotation"), request.rotation}};
    return QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(fields))
                                 .toJson(QJsonDocument::Compact));
}

QVariantMap AssetProvider::resultMap(const AssetResult &result)
{
    QVariantMap data{{QStringLiteral("metadata"), result.metadata},
        {QStringLiteral("bytes"), result.bytes},
        {QStringLiteral("mimeType"), result.mimeType},
        {QStringLiteral("peaks"), result.peaks}};
    if (!result.bytes.isEmpty() && result.mimeType.startsWith(QLatin1String("image/")))
        data.insert(QStringLiteral("previewUrl"), QStringLiteral("data:")
            + result.mimeType + QStringLiteral(";base64,")
            + QString::fromLatin1(result.bytes.toBase64()));
    return data;
}

QString AssetProvider::request(AssetRequest request, PropertyDocument *document,
                               const QString &viewId)
{
    if (request.resourceId.isEmpty() || request.kind.isEmpty() || !m_backend
        || m_subscriptions.size() >= 4096 || request.quality < 1 || request.quality > 100
        || request.columns < 0 || request.columns > 16384 || request.pageIndex < 0
        || (request.rotation != 0 && request.rotation != 90
            && request.rotation != 180 && request.rotation != 270)
        || request.size.width() < 0 || request.size.height() < 0
        || request.size.width() > 8192 || request.size.height() > 8192
        || request.rateNumerator <= 0 || request.rateDenominator <= 0) {
        m_lastError = QStringLiteral("Invalid asset request or missing backend");
        emit changed();
        return {};
    }
    QString availability = QStringLiteral("ready");
    if (m_resolver) {
        const QVariantMap resource = m_resolver->lookup(request.resourceId);
        if (resource.isEmpty()) {
            m_lastError = QStringLiteral("Unknown resource ID");
            emit changed();
            return {};
        }
        request.uri = resource.value(QStringLiteral("uri")).toString();
        request.revision = resource.value(QStringLiteral("revision")).toString();
        availability = resource.value(QStringLiteral("availability")).toString();
    }
    if (request.uri.isEmpty()) {
        m_lastError = QStringLiteral("Asset URI required");
        emit changed();
        return {};
    }
    const QString key = keyFor(request);
    Subscription sub;
    sub.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    sub.key = key;
    sub.resourceId = request.resourceId;
    sub.viewId = viewId;
    sub.viewGeneration = m_viewGenerations.value(viewId);
    sub.document = document;
    if (document) sub.expected = document->header();
    if (availability == QLatin1String("missing")) {
        sub.state = QStringLiteral("missing");
        sub.error = QStringLiteral("Resource is missing");
    }
    m_subscriptions.insert(sub.id, sub);
    if (document) {
        auto &stored = m_subscriptions[sub.id];
        stored.changedConnection = connect(document, &PropertyDocument::changed, this,
            [this, id = sub.id]() {
                const auto it = m_subscriptions.constFind(id);
                if (it != m_subscriptions.cend() && !current(*it)) markStale(id);
            });
        stored.destroyedConnection = connect(document, &QObject::destroyed, this,
            [this, id = sub.id]() { markStale(id); });
    }
    if (sub.state == QLatin1String("missing")) {
        emit assetChanged(sub.id);
        emit changed();
        return sub.id;
    }
    auto cached = m_cache.find(key);
    if (cached != m_cache.end()) {
        cached->used = ++m_access;
        auto &stored = m_subscriptions[sub.id];
        stored.state = QStringLiteral("ready");
        stored.data = cached->data;
        emit assetChanged(sub.id);
        emit assetReady(sub.id);
        emit changed();
        return sub.id;
    }
    auto flight = m_flights.find(key);
    if (flight != m_flights.end()) {
        flight->subscribers.append(sub.id);
        emit changed();
        return sub.id;
    }
    const AssetBackendPtr backend = m_backend;
    JobSpec job;
    job.label = QStringLiteral("Load %1: %2").arg(request.kind, request.resourceId);
    job.priority = request.kind == QLatin1String("thumbnail") ? 10 : 0;
    job.work = [backend, request](const CancellationToken &token,
                                  const JobProgress &report) {
        const AssetResult result = backend->fetch(request, token, report);
        return JobResult{resultMap(result), result.error};
    };
    // Map before enqueue: JobModel may finish an immediate worker quickly, but
    // QFutureWatcher delivers its completion back on this event loop.
    const QString jobId = m_jobs->enqueue(std::move(job));
    if (jobId.isEmpty()) {
        m_lastError = m_jobs->lastError();
        m_subscriptions.remove(sub.id);
        emit changed();
        return {};
    }
    m_flights.insert(key, Flight{jobId, request.resourceId, key,
        m_resourceGenerations.value(request.resourceId), {sub.id}});
    m_jobKeys.insert(jobId, key);
    m_lastError.clear();
    emit changed();
    return sub.id;
}

QString AssetProvider::requestAsset(const QString &resourceId, const QString &kind,
                                    const QVariantMap &options,
                                    PropertyDocument *document, const QString &viewId)
{
    AssetRequest request;
    request.resourceId = resourceId;
    request.kind = kind;
    request.quality = options.value(QStringLiteral("quality"), 75).toInt();
    request.columns = options.value(QStringLiteral("columns"), 0).toInt();
    bool pageOk = false, rotationOk = false;
    request.pageIndex = options.value(QStringLiteral("pageIndex"), 0).toInt(&pageOk);
    request.rotation = options.value(QStringLiteral("rotation"), 0).toInt(&rotationOk);
    const QVariantMap size = options.value(QStringLiteral("size")).toMap();
    request.size = QSize(size.value(QStringLiteral("width")).toInt(),
                         size.value(QStringLiteral("height")).toInt());
    const QVariantMap region = options.value(QStringLiteral("region")).toMap();
    request.region = QRect(region.value(QStringLiteral("x")).toInt(),
                           region.value(QStringLiteral("y")).toInt(),
                           region.value(QStringLiteral("width")).toInt(),
                           region.value(QStringLiteral("height")).toInt());
    if ((options.contains(QStringLiteral("timeTicks"))
         && options.value(QStringLiteral("timeTicks")).metaType().id() != QMetaType::QString)
        || (options.contains(QStringLiteral("rateNumerator"))
            && options.value(QStringLiteral("rateNumerator")).metaType().id() != QMetaType::QString)
        || (options.contains(QStringLiteral("rateDenominator"))
            && options.value(QStringLiteral("rateDenominator")).metaType().id() != QMetaType::QString)) {
        m_lastError = QStringLiteral("Exact time values must be decimal strings");
        emit changed();
        return {};
    }
    bool ticksOk = true, numeratorOk = true, denominatorOk = true;
    const QString ticks = options.value(QStringLiteral("timeTicks"), QStringLiteral("0")).toString();
    const QString numerator = options.value(QStringLiteral("rateNumerator"), QStringLiteral("1")).toString();
    const QString denominator = options.value(QStringLiteral("rateDenominator"), QStringLiteral("1")).toString();
    request.timeTicks = ticks.toLongLong(&ticksOk);
    request.rateNumerator = numerator.toLongLong(&numeratorOk);
    request.rateDenominator = denominator.toLongLong(&denominatorOk);
    if (!ticksOk || !numeratorOk || !denominatorOk || !pageOk || !rotationOk
        || request.pageIndex < 0 || (request.rotation != 0 && request.rotation != 90
                                   && request.rotation != 180 && request.rotation != 270)
        || request.rateNumerator <= 0
        || request.rateDenominator <= 0 || request.quality < 1 || request.quality > 100
        || request.columns < 0 || request.size.width() < 0 || request.size.height() < 0
        || request.region.width() < 0 || request.region.height() < 0) {
        m_lastError = QStringLiteral("Invalid asset request parameters");
        emit changed();
        return {};
    }
    return this->request(std::move(request), document, viewId);
}

bool AssetProvider::current(const Subscription &subscription) const
{
    if (!subscription.viewId.isEmpty()
        && m_viewGenerations.value(subscription.viewId) != subscription.viewGeneration)
        return false;
    if (subscription.expected.documentId.isValid()
        && (!subscription.document
            || !sameHeader(subscription.document->header(), subscription.expected)))
        return false;
    return true;
}

void AssetProvider::finishFlight(const QString &jobId, JobModel::JobState state,
                                 const QVariantMap &output, const QString &error)
{
    const QString key = m_jobKeys.take(jobId);
    if (key.isEmpty()) return;
    const Flight flight = m_flights.take(key);
    const bool freshResource = m_resourceGenerations.value(flight.resourceId)
                               == flight.resourceGeneration;
    if (state == JobModel::Succeeded && freshResource && !flight.subscribers.isEmpty()) {
        const qint64 bytes = output.value(QStringLiteral("bytes")).toByteArray().size();
        const qint64 peaks = output.value(QStringLiteral("peaks")).toList().size() * qint64(sizeof(double));
        const qint64 preview = output.value(QStringLiteral("previewUrl")).toString().size()
                               * qint64(sizeof(char16_t));
        const qint64 cost = bytes + preview + peaks + 1024;
        if (cost <= m_cacheBudget && cost <= 64 * 1024 * 1024) {
            m_cache.insert(key, CacheEntry{output, flight.resourceId, cost, ++m_access});
            m_cacheBytes += cost;
            trimCache();
            emit cacheChanged();
        }
    }
    for (const QString &id : flight.subscribers) {
        auto sub = m_subscriptions.find(id);
        if (sub == m_subscriptions.end()) continue;
        if (!freshResource || !current(*sub)) {
            sub->state = QStringLiteral("stale");
            sub->error = QStringLiteral("Request context changed");
        } else if (state == JobModel::Succeeded) {
            sub->state = QStringLiteral("ready");
            sub->data = output;
        } else if (state == JobModel::Cancelled || state == JobModel::Stale) {
            sub->state = state == JobModel::Stale ? QStringLiteral("stale")
                                                   : QStringLiteral("cancelled");
            sub->error = error;
        } else {
            sub->state = QStringLiteral("failed");
            sub->error = error;
        }
        emit assetChanged(id);
        if (sub->state == QLatin1String("ready")) emit assetReady(id);
    }
    emit changed();
}

void AssetProvider::trimCache()
{
    while (m_cacheBytes > m_cacheBudget && !m_cache.isEmpty()) {
        auto victim = m_cache.begin();
        for (auto it = m_cache.begin(); it != m_cache.end(); ++it)
            if (it->used < victim->used) victim = it;
        m_cacheBytes -= victim->cost;
        m_cache.erase(victim);
    }
}

bool AssetProvider::release(const QString &subscriptionId)
{
    auto sub = m_subscriptions.find(subscriptionId);
    if (sub == m_subscriptions.end()) return false;
    const QString key = sub->key;
    disconnect(sub->changedConnection);
    disconnect(sub->destroyedConnection);
    m_subscriptions.erase(sub);
    auto flight = m_flights.find(key);
    if (flight != m_flights.end()) {
        flight->subscribers.removeAll(subscriptionId);
        if (flight->subscribers.isEmpty()) {
            const QString jobId = flight->jobId;
            m_flights.erase(flight);
            m_jobKeys.remove(jobId);
            m_jobs->cancelJob(jobId);
        }
    }
    emit changed();
    return true;
}

QVariantMap AssetProvider::asset(const QString &subscriptionId) const
{
    const auto it = m_subscriptions.constFind(subscriptionId);
    if (it == m_subscriptions.cend()) return {};
    return {{QStringLiteral("subscriptionId"), it->id},
        {QStringLiteral("resourceId"), it->resourceId},
        {QStringLiteral("state"), it->state},
        {QStringLiteral("data"), it->data},
        {QStringLiteral("error"), it->error}};
}

void AssetProvider::invalidateView(const QString &viewId)
{
    if (viewId.isEmpty()) return;
    ++m_viewGenerations[viewId];
    QStringList ids;
    for (auto it = m_subscriptions.cbegin(); it != m_subscriptions.cend(); ++it)
        if (it->viewId == viewId) ids.append(it.key());
    for (const QString &id : ids) markStale(id);
    emit changed();
}

void AssetProvider::invalidateResource(const QString &resourceId)
{
    if (resourceId.isEmpty()) return;
    ++m_resourceGenerations[resourceId];
    for (auto it = m_cache.begin(); it != m_cache.end();) {
        if (it->resourceId == resourceId) {
            m_cacheBytes -= it->cost;
            it = m_cache.erase(it);
        } else ++it;
    }
    QStringList ids;
    for (auto it = m_subscriptions.cbegin(); it != m_subscriptions.cend(); ++it)
        if (it->resourceId == resourceId) ids.append(it.key());
    for (const QString &id : ids) markStale(id);
    emit cacheChanged();
    emit changed();
}

void AssetProvider::markStale(const QString &subscriptionId)
{
    auto sub = m_subscriptions.find(subscriptionId);
    if (sub == m_subscriptions.end() || sub->state == QLatin1String("stale")) return;
    sub->state = QStringLiteral("stale");
    sub->error = QStringLiteral("Request context changed");
    sub->data.clear();
    const QString key = sub->key;
    emit assetChanged(subscriptionId);
    auto flight = m_flights.find(key);
    if (flight != m_flights.end()) {
        flight->subscribers.removeAll(subscriptionId);
        if (flight->subscribers.isEmpty()) {
            const QString jobId = flight->jobId;
            m_flights.erase(flight);
            m_jobKeys.remove(jobId);
            m_jobs->cancelJob(jobId);
        }
    }
    emit changed();
}

void AssetProvider::clearCache()
{
    m_cache.clear();
    m_cacheBytes = 0;
    emit cacheChanged();
}

} // namespace QindaTK
