// SPDX-License-Identifier: LGPL-3.0-or-later
#include "timeline_media_provider.h"

#include <QMetaObject>
#include <QThreadPool>
#include <algorithm>
#include <cmath>
#include <mutex>

namespace QindaTK {
namespace {
constexpr int kMaxColumns = 8192;
constexpr int kMaxQueued = 1024;
constexpr int kMaxCacheEntries = 256;
constexpr double kPi = 3.14159265358979323846;

quint32 stableHash(const QString &text)
{
    quint32 hash = 2166136261u;
    for (const QChar character : text) {
        hash ^= character.unicode();
        hash *= 16777619u;
    }
    return hash;
}

// AGENT-NOTE: labeled synthetic reference signal, not decoded audio. The
// envelope is analytic, so a column spanning many carrier periods reports the
// true envelope maximum instead of a subsampled guess.
class SyntheticBackend final : public TimelineMediaBackend {
public:
    QString backendId() const override { return QStringLiteral("qindatk.synthetic-reference"); }
    void start(const TimelineMediaRequest &request, CancellationToken cancel,
               Completion completion) override
    {
        if (request.kind != QLatin1String("peaks")) {
            TimelineMediaResult result;
            result.error = {ErrorCode::Unsupported,
                            QStringLiteral("The synthetic reference backend decodes no video frames"),
                            {}, backendId()};
            completion(result);
            return;
        }
        QThreadPool::globalInstance()->start([request, cancel, completion] {
            TimelineMediaResult result;
            const quint32 seed = stableHash(request.resourceId);
            const double carrier = 55.0 * double(2 + seed % 6);
            const double period = 0.75 + double((seed >> 8) % 8) * 0.25;
            const double scale = double(request.ticksPerSecond.denominator)
                               / double(request.ticksPerSecond.numerator);
            const double first = double(request.sourceStart) * scale;
            const double span = double(request.sourceEnd - request.sourceStart) * scale;
            const auto envelope = [period](double t) {
                return 0.15 + 0.8 * std::fabs(std::sin(kPi * t / period));
            };
            result.peaks.resize(request.columns);
            for (int column = 0; column < request.columns; ++column) {
                if ((column & 255) == 0 && cancel.isCancelled()) {
                    result.error = {ErrorCode::Cancelled, QStringLiteral("Cancelled"), {}, {}};
                    completion(result);
                    return;
                }
                const double t0 = first + span * column / request.columns;
                const double t1 = first + span * (column + 1) / request.columns;
                double peak = 0;
                if (t1 - t0 >= 1.0 / carrier) {
                    peak = std::max(envelope(t0), envelope(t1));
                    if (t1 - t0 >= period) peak = 0.95;
                    const double crest = (std::floor(t0 / period - 0.5) + 1.5) * period;
                    if (crest >= t0 && crest <= t1) peak = 0.95;
                } else {
                    for (int i = 0; i <= 32; ++i) {
                        const double t = t0 + (t1 - t0) * i / 32.0;
                        peak = std::max(peak, std::fabs(envelope(t) * std::sin(2 * kPi * carrier * t)));
                    }
                }
                result.peaks[column] = float(std::clamp(peak, 0.0, 1.0));
            }
            completion(result);
        });
    }
};
} // namespace

TimelineMediaBackendPtr createSyntheticTimelineMediaBackend()
{
    return std::make_shared<SyntheticBackend>();
}

struct TimelineMediaProvider::Delivery {
    std::mutex mutex;
    TimelineMediaProvider *provider = nullptr;
};

TimelineMediaProvider::TimelineMediaProvider(QObject *parent)
    : QObject(parent), m_delivery(std::make_shared<Delivery>())
{
    m_delivery->provider = this;
}

TimelineMediaProvider::~TimelineMediaProvider()
{
    {
        std::lock_guard<std::mutex> lock(m_delivery->mutex);
        m_delivery->provider = nullptr;
    }
    for (auto &record : m_records) record.cancel.cancel();
}

void TimelineMediaProvider::setBackend(TimelineMediaBackendPtr backend)
{
    for (const auto &id : m_records.keys()) drop(id, true);
    m_backend = std::move(backend);
    m_reference = false;
    clearCache();
    emit changed();
    emit statisticsChanged();
}

void TimelineMediaProvider::setReferenceBackend(bool enabled)
{
    if (m_reference == enabled) return;
    setBackend(enabled ? createSyntheticTimelineMediaBackend() : nullptr);
    m_reference = enabled;
    emit changed();
}

QString TimelineMediaProvider::backendId() const
{
    return m_backend ? m_backend->backendId() : QString();
}

void TimelineMediaProvider::setMaxInFlight(int count)
{
    count = std::clamp(count, 1, 64);
    if (m_maxInFlight == count) return;
    m_maxInFlight = count;
    emit changed();
    pump();
}

QString TimelineMediaProvider::keyFor(const TimelineMediaRequest &request) const
{
    return QStringLiteral("%1|%2|%3|%4|%5|%6/%7|%8")
        .arg(request.kind, request.resourceId)
        .arg(request.sourceStart).arg(request.sourceEnd).arg(request.columns)
        .arg(request.ticksPerSecond.numerator).arg(request.ticksPerSecond.denominator)
        .arg(request.resourceGeneration);
}

QString TimelineMediaProvider::request(TimelineMediaRequest request)
{
    if (!request.ownerKey.isEmpty()) {
        const QString previous = m_owners.value(request.ownerKey);
        if (!previous.isEmpty()) drop(previous, true);
    }
    request.serial = ++m_serial;
    request.requestId = QStringLiteral("media-%1").arg(request.serial);
    request.resourceGeneration = m_generations.value(request.resourceId);
    Record record;
    record.request = request;
    record.key = keyFor(request);
    const QString id = request.requestId;
    QString problem;
    if (!m_backend) problem = tr("No timeline media backend is configured");
    else if (request.resourceId.isEmpty()) problem = tr("The clip has no media resource");
    else if (request.columns < 1 || request.columns > kMaxColumns) problem = tr("Invalid media column count");
    else if (request.sourceEnd <= request.sourceStart || !request.ticksPerSecond.isValid())
        problem = tr("Invalid media source range");
    if (!problem.isEmpty() || m_cache.contains(record.key)) {
        record.state = problem.isEmpty() ? QStringLiteral("ready") : QStringLiteral("failed");
        if (problem.isEmpty()) record.result = m_cache.value(record.key);
        else record.result.error = {ErrorCode::Unavailable, problem, {}, backendId()};
    } else {
        m_queue.push_back(id);
        while (m_queue.size() > kMaxQueued) drop(m_queue.first(), true);
    }
    m_records.insert(id, record);
    if (!request.ownerKey.isEmpty()) m_owners.insert(request.ownerKey, id);
    if (record.state != QLatin1String("queued")) {
        // Notify after the caller has stored the returned ID.
        const QString owner = request.ownerKey;
        QMetaObject::invokeMethod(this, [this, id, owner] {
            const auto it = m_records.constFind(id);
            if (it == m_records.cend()) return;
            if (it->state == QLatin1String("ready")) emit ready(id, owner);
            else emit failed(id, owner, it->result.error.message);
        }, Qt::QueuedConnection);
    }
    pump();
    emit statisticsChanged();
    return id;
}

QString TimelineMediaProvider::requestPeaks(const QString &ownerKey, const QString &resourceId,
                                            const QString &sourceStartTicks,
                                            const QString &sourceEndTicks,
                                            const QString &ticksPerSecondNumerator,
                                            const QString &ticksPerSecondDenominator, int columns)
{
    TimelineMediaRequest request;
    request.ownerKey = ownerKey;
    request.resourceId = resourceId;
    request.kind = QStringLiteral("peaks");
    request.sourceStart = TimelineTime::parseDecimal(sourceStartTicks).value_or(0);
    request.sourceEnd = TimelineTime::parseDecimal(sourceEndTicks).value_or(0);
    const auto numerator = TimelineTime::parseDecimal(ticksPerSecondNumerator);
    const auto denominator = TimelineTime::parseDecimal(ticksPerSecondDenominator);
    if (numerator && denominator) request.ticksPerSecond = {*numerator, *denominator};
    else request.ticksPerSecond = {0, 0};
    request.columns = columns;
    return this->request(request);
}

QString TimelineMediaProvider::requestFrames(const QString &ownerKey, const QString &resourceId,
                                             const QString &sourceStartTicks,
                                             const QString &sourceEndTicks,
                                             const QString &ticksPerSecondNumerator,
                                             const QString &ticksPerSecondDenominator, int count)
{
    TimelineMediaRequest request;
    request.ownerKey = ownerKey;
    request.resourceId = resourceId;
    request.kind = QStringLiteral("frames");
    request.sourceStart = TimelineTime::parseDecimal(sourceStartTicks).value_or(0);
    request.sourceEnd = TimelineTime::parseDecimal(sourceEndTicks).value_or(0);
    const auto numerator = TimelineTime::parseDecimal(ticksPerSecondNumerator);
    const auto denominator = TimelineTime::parseDecimal(ticksPerSecondDenominator);
    if (numerator && denominator) request.ticksPerSecond = {*numerator, *denominator};
    else request.ticksPerSecond = {0, 0};
    request.columns = count;
    return this->request(request);
}

void TimelineMediaProvider::pump()
{
    while (m_backend && m_inFlight < m_maxInFlight && !m_queue.isEmpty()) {
        const QString id = m_queue.takeFirst();
        auto it = m_records.find(id);
        if (it == m_records.end() || it->state != QLatin1String("queued")) continue;
        it->state = QStringLiteral("running");
        ++m_inFlight;
        const quint64 serial = it->request.serial;
        m_serials.insert(serial, id);
        auto delivery = m_delivery;
        m_backend->start(it->request, it->cancel.token(),
                         [delivery, serial](TimelineMediaResult result) {
            std::lock_guard<std::mutex> lock(delivery->mutex);
            if (!delivery->provider) return;
            QMetaObject::invokeMethod(delivery->provider,
                                      [delivery, serial, result = std::move(result)]() mutable {
                if (delivery->provider) delivery->provider->finish(serial, std::move(result));
            }, Qt::QueuedConnection);
        });
    }
    emit statisticsChanged();
}

void TimelineMediaProvider::finish(quint64 serial, TimelineMediaResult result)
{
    const QString id = m_serials.take(serial);
    m_inFlight = std::max(0, m_inFlight - 1);
    auto it = m_records.find(id);
    // AGENT-GUARD: only the running record that asked may publish. A cancelled,
    // superseded or invalidated request is counted and dropped, never shown.
    if (id.isEmpty() || it == m_records.end() || it->state != QLatin1String("running")
        || m_generations.value(it->request.resourceId) != it->request.resourceGeneration) {
        ++m_stale;
        if (it != m_records.end() && it->state == QLatin1String("running"))
            it->state = QStringLiteral("cancelled");
        pump();
        return;
    }
    it->result = std::move(result);
    it->state = it->result.error ? QStringLiteral("failed") : QStringLiteral("ready");
    ++m_completed;
    const QString owner = it->request.ownerKey;
    if (!it->result.error) {
        remember(it->key, it->result);
        emit ready(id, owner);
    } else {
        emit failed(id, owner, it->result.error.message);
    }
    pump();
}

void TimelineMediaProvider::drop(const QString &requestId, bool countCancel)
{
    auto it = m_records.find(requestId);
    if (it == m_records.end()) return;
    if (it->state == QLatin1String("queued")) m_queue.removeAll(requestId);
    it->cancel.cancel();
    if (m_owners.value(it->request.ownerKey) == requestId) m_owners.remove(it->request.ownerKey);
    m_records.erase(it);
    if (countCancel) ++m_cancelled;
}

bool TimelineMediaProvider::cancel(const QString &requestId)
{
    if (!m_records.contains(requestId)) return false;
    drop(requestId, true);
    emit statisticsChanged();
    return true;
}

void TimelineMediaProvider::cancelOwner(const QString &ownerKey)
{
    const QString id = m_owners.value(ownerKey);
    if (!id.isEmpty()) cancel(id);
}

QVariantMap TimelineMediaProvider::result(const QString &requestId) const
{
    const auto it = m_records.constFind(requestId);
    if (it == m_records.cend()) return {{QStringLiteral("state"), QStringLiteral("unknown")}};
    QVariantList peaks;
    peaks.reserve(it->result.peaks.size());
    for (float peak : it->result.peaks) peaks.push_back(double(peak));
    return {{QStringLiteral("state"), it->state}, {QStringLiteral("peaks"), peaks},
            {QStringLiteral("frames"), it->result.frames},
            {QStringLiteral("error"), it->result.error.message},
            {QStringLiteral("ownerKey"), it->request.ownerKey},
            {QStringLiteral("requestId"), requestId}};
}

void TimelineMediaProvider::invalidateResource(const QString &resourceId)
{
    m_generations[resourceId] += 1;
    const QString marker = QStringLiteral("|%1|").arg(resourceId);
    for (auto it = m_cache.begin(); it != m_cache.end();) {
        if (it.key().contains(marker)) {
            m_cacheOrder.removeAll(it.key());
            it = m_cache.erase(it);
        } else {
            ++it;
        }
    }
    emit statisticsChanged();
    emit resourceInvalidated(resourceId);
}

void TimelineMediaProvider::clearCache()
{
    m_cache.clear();
    m_cacheOrder.clear();
    emit statisticsChanged();
}

void TimelineMediaProvider::remember(const QString &key, const TimelineMediaResult &result)
{
    m_cacheOrder.removeAll(key);
    m_cacheOrder.push_back(key);
    m_cache.insert(key, result);
    while (m_cacheOrder.size() > kMaxCacheEntries) m_cache.remove(m_cacheOrder.takeFirst());
}

} // namespace QindaTK
