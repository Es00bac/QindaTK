// SPDX-License-Identifier: LGPL-3.0-or-later
#include "trace_index.h"

#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QtConcurrent>
#include <algorithm>
#include <qnumeric.h>

namespace QindaTK {
namespace {
constexpr int MaxInput = 50000;
constexpr int MaxRetained = 20000;
bool validLabel(const QString &s, int max = 256)
{ return !s.trimmed().isEmpty() && s.size() <= max && !s.contains(QChar::Null); }
}

bool TraceIndex::decimal(const QVariant &value, qint64 *out)
{
    const QString s = value.toString();
    bool ok = false;
    const qint64 n = s.toLongLong(&ok);
    if (!ok || QString::number(n) != s) return false;
    *out = n;
    return true;
}

bool TraceIndex::convert(qint64 ticks, const ClockDomain &domain, qint64 *ns)
{
    qint64 product = 0;
    return !qMulOverflow(ticks, domain.nanosPerTick, &product)
           && !qAddOverflow(product, domain.offsetNs, ns);
}

bool TraceIndex::setClockDomains(const QVariantList &domains)
{
    if (domains.isEmpty() || domains.size() > 16) {
        m_error = QStringLiteral("Provide one to sixteen clock domains"); emit changed(); return false;
    }
    QHash<QString, ClockDomain> next;
    for (const QVariant &raw : domains) {
        const auto row = raw.toMap();
        ClockDomain d;
        d.id = row.value(QStringLiteral("id")).toString();
        if (!validLabel(d.id, 64) || next.contains(d.id)
            || !decimal(row.value(QStringLiteral("nanosPerTick")), &d.nanosPerTick)
            || d.nanosPerTick <= 0
            || !decimal(row.value(QStringLiteral("offsetNs")), &d.offsetNs)) {
            m_error = QStringLiteral("Invalid or duplicate clock domain"); emit changed(); return false;
        }
        next.insert(d.id, d);
    }
    ++m_generation;
    m_domains = std::move(next);
    m_records.clear(); m_byId.clear(); m_threads.clear();
    m_rejected = 0; m_truncated = 0; m_busy = false; m_error.clear();
    emit changed();
    return true;
}

TraceIndex::Analysis TraceIndex::analyze(const QVariantList &input,
                                        const QHash<QString, ClockDomain> &domains)
{
    Analysis result;
    if (input.size() > MaxInput) {
        result.error = QStringLiteral("Trace input exceeds 50,000 records"); return result;
    }
    QSet<QString> ids;
    for (const QVariant &raw : input) {
        const auto row = raw.toMap();
        Record r;
        r.id = row.value(QStringLiteral("id")).toString();
        r.kind = row.value(QStringLiteral("kind")).toString();
        r.name = row.value(QStringLiteral("name")).toString();
        r.category = row.value(QStringLiteral("category")).toString();
        r.threadId = row.value(QStringLiteral("threadId")).toString();
        r.parentId = row.value(QStringLiteral("parentId")).toString();
        r.clockDomain = row.value(QStringLiteral("clockDomain")).toString();
        if (!validLabel(r.id, 128) || ids.contains(r.id)
            || (r.kind != QStringLiteral("span") && r.kind != QStringLiteral("event"))
            || !validLabel(r.name) || !validLabel(r.threadId, 128)
            || r.category.size() > 128 || r.parentId.size() > 128
            || !domains.contains(r.clockDomain)
            || !decimal(row.value(QStringLiteral("startTicks")), &r.startTicks)
            || (r.kind == QStringLiteral("span")
                && !decimal(row.value(QStringLiteral("endTicks")), &r.endTicks))) {
            ++result.rejected; continue;
        }
        if (r.kind == QStringLiteral("event")) r.endTicks = r.startTicks;
        const auto &domain = domains.value(r.clockDomain);
        if (!convert(r.startTicks, domain, &r.startNs)
            || !convert(r.endTicks, domain, &r.endNs) || r.endNs < r.startNs) {
            ++result.rejected; continue;
        }
        ids.insert(r.id);
        result.records.append(std::move(r));
    }
    std::sort(result.records.begin(), result.records.end(), [](const Record &a, const Record &b) {
        if (a.startNs != b.startNs) return a.startNs < b.startNs;
        return a.id < b.id;
    });
    if (result.records.size() > MaxRetained) {
        result.truncated = result.records.size() - MaxRetained;
        result.records.erase(result.records.begin(), result.records.begin() + result.truncated);
    }
    QSet<QString> threads;
    bool first = true;
    for (const Record &r : std::as_const(result.records)) {
        threads.insert(r.threadId);
        if (first) { result.start = r.startNs; result.end = r.endNs; first = false; }
        else result.start = std::min(result.start, r.startNs);
        result.end = std::max(result.end, r.endNs);
    }
    result.threads = threads.values(); result.threads.sort();
    return result;
}

QString TraceIndex::ingestAsync(const QVariantList &records)
{
    if (m_domains.isEmpty()) {
        m_error = QStringLiteral("Configure clock domains first"); emit changed(); return {};
    }
    const quint64 generation = ++m_generation;
    const QString requestId = QString::number(generation);
    const auto domains = m_domains;
    auto *watcher = new QFutureWatcher<Analysis>(this);
    connect(watcher, &QFutureWatcher<Analysis>::finished, this, [this, watcher, generation, requestId] {
        Analysis result = watcher->result();
        watcher->deleteLater();
        if (generation != m_generation) { emit analysisDiscarded(requestId); return; }
        publish(std::move(result), generation);
        emit analysisReady(requestId, count());
    });
    m_busy = true; m_error.clear(); emit changed();
    watcher->setFuture(QtConcurrent::run([records, domains] { return analyze(records, domains); }));
    return requestId;
}

void TraceIndex::publish(Analysis result, quint64 generation)
{
    if (generation != m_generation) return;
    m_records = std::move(result.records);
    m_byId.clear();
    for (int i = 0; i < m_records.size(); ++i) m_byId.insert(m_records[i].id, i);
    m_threads = std::move(result.threads);
    m_error = std::move(result.error);
    m_rejected = result.rejected; m_truncated = result.truncated;
    m_start = result.start; m_end = result.end; m_busy = false;
    emit changed();
}

void TraceIndex::clear()
{
    ++m_generation; m_records.clear(); m_byId.clear(); m_threads.clear();
    m_error.clear(); m_start = m_end = 0; m_rejected = m_truncated = 0; m_busy = false;
    emit changed();
}

QVariantMap TraceIndex::recordMap(const Record &r) const
{
    return {{QStringLiteral("id"), r.id}, {QStringLiteral("kind"), r.kind},
            {QStringLiteral("name"), r.name}, {QStringLiteral("category"), r.category},
            {QStringLiteral("threadId"), r.threadId}, {QStringLiteral("parentId"), r.parentId},
            {QStringLiteral("clockDomain"), r.clockDomain},
            {QStringLiteral("startTicks"), QString::number(r.startTicks)},
            {QStringLiteral("endTicks"), QString::number(r.endTicks)},
            {QStringLiteral("startNs"), QString::number(r.startNs)},
            {QStringLiteral("endNs"), QString::number(r.endNs)}};
}

QVariantMap TraceIndex::record(const QString &id) const
{
    const auto found = m_byId.constFind(id);
    return found == m_byId.cend() ? QVariantMap{} : recordMap(m_records.at(*found));
}

QVariantList TraceIndex::childrenOf(const QString &spanId) const
{
    QVariantList out;
    if (record(spanId).value(QStringLiteral("kind")) != QStringLiteral("span")) return out;
    for (const Record &r : m_records) if (r.parentId == spanId) out.append(recordMap(r));
    return out;
}

QStringList TraceIndex::ancestorsOf(const QString &recordId) const
{
    QStringList out;
    QSet<QString> visited;
    QString id = recordId;
    while (!id.isEmpty() && !visited.contains(id) && m_byId.contains(id)) {
        visited.insert(id);
        id = m_records.at(m_byId.value(id)).parentId;
        if (m_byId.contains(id)) out.append(id);
    }
    return out;
}

QVariantList TraceIndex::queryWindow(double fromFraction, double toFraction,
                                     const QString &threadId, const QString &textFilter,
                                     int limit) const
{
    QVariantList out;
    if (m_records.isEmpty() || !qIsFinite(fromFraction) || !qIsFinite(toFraction)) return out;
    const auto total = static_cast<quint64>(m_end) - static_cast<quint64>(m_start);
    const double denominator = total ? static_cast<double>(total) : 1.0;
    limit = std::clamp(limit, 1, 1000);
    for (const Record &r : m_records) {
        if (!threadId.isEmpty() && r.threadId != threadId) continue;
        if (!textFilter.isEmpty() && !r.name.contains(textFilter, Qt::CaseInsensitive)
            && !r.category.contains(textFilter, Qt::CaseInsensitive)) continue;
        const double start = static_cast<double>(static_cast<quint64>(r.startNs)
                                                 - static_cast<quint64>(m_start)) / denominator;
        const double end = static_cast<double>(static_cast<quint64>(r.endNs)
                                               - static_cast<quint64>(m_start)) / denominator;
        if (end < fromFraction || start > toFraction) continue;
        QVariantMap map = recordMap(r);
        map.insert(QStringLiteral("startFraction"), start);
        map.insert(QStringLiteral("endFraction"), end);
        out.append(map);
        if (out.size() >= limit) break;
    }
    return out;
}

QVariantList TraceIndex::queryRangeNs(const QString &fromNs, const QString &toNs,
                                      const QString &threadId, const QString &textFilter,
                                      int limit) const
{
    QVariantList out;
    qint64 from = 0, to = 0;
    if (!decimal(fromNs, &from) || !decimal(toNs, &to) || from > to) return out;
    limit = std::clamp(limit, 1, 1000);
    for (const Record &r : m_records) {
        if (r.startNs > to) break;
        if (r.endNs < from || (!threadId.isEmpty() && r.threadId != threadId)) continue;
        if (!textFilter.isEmpty() && !r.name.contains(textFilter, Qt::CaseInsensitive)
            && !r.category.contains(textFilter, Qt::CaseInsensitive)) continue;
        out.append(recordMap(r));
        if (out.size() >= limit) break;
    }
    return out;
}

QString TraceIndex::exportJson() const
{
    QJsonObject root;
    QJsonArray domains;
    QStringList domainIds = m_domains.keys(); domainIds.sort();
    for (const QString &id : domainIds) {
        const auto &d = m_domains[id];
        domains.append(QJsonObject{{QStringLiteral("id"), id},
                                   {QStringLiteral("nanosPerTick"), QString::number(d.nanosPerTick)},
                                   {QStringLiteral("offsetNs"), QString::number(d.offsetNs)}});
    }
    root.insert(QStringLiteral("clockDomains"), domains);
    root.insert(QStringLiteral("truncatedCount"), m_truncated);
    root.insert(QStringLiteral("rejectedCount"), m_rejected);
    QJsonArray records;
    for (const Record &r : m_records) records.append(QJsonObject::fromVariantMap(recordMap(r)));
    root.insert(QStringLiteral("records"), records);
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

} // namespace QindaTK
