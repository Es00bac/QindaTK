// SPDX-License-Identifier: LGPL-3.0-or-later
#include "capability_registry.h"
#include <QSet>
#include <QThread>

namespace QindaTK {
namespace {
CapabilityResult missing(const QString &featureId, const QString &operationId)
{
    return {featureId, operationId, {}, {}, false,
            QStringLiteral("No provider registered for this operation")};
}
bool better(const CapabilityDescriptor &candidate, const CapabilityDescriptor &current)
{
    if (candidate.available != current.available) return candidate.available;
    if (candidate.priority != current.priority) return candidate.priority > current.priority;
    return candidate.backendId < current.backendId;
}
bool valid(const CapabilityDescriptor &entry)
{
    return !entry.featureId.isEmpty() && !entry.operationId.isEmpty()
        && !entry.backendId.isEmpty() && !entry.backendVersion.isEmpty()
        && (entry.available || !entry.unavailableReason.isEmpty());
}
} // namespace

QVariantMap CapabilityResult::toQml() const
{
    return {{QStringLiteral("featureId"), featureId},
            {QStringLiteral("operationId"), operationId},
            {QStringLiteral("backendId"), backendId},
            {QStringLiteral("backendVersion"), backendVersion},
            {QStringLiteral("available"), available},
            {QStringLiteral("reason"), reason}};
}

bool CapabilityRegistry::publish(const CapabilityDescriptor &entry)
{
    Q_ASSERT(thread() == QThread::currentThread());
    if (!valid(entry)) return false;
    for (auto &existing : m_entries) {
        if (existing.featureId != entry.featureId
            || existing.operationId != entry.operationId
            || existing.backendId != entry.backendId) continue;
        if (existing == entry) return true;
        existing = entry;
        ++m_generation;
        emit generationChanged();
        emit capabilityChanged(entry.featureId, entry.operationId);
        return true;
    }
    m_entries.append(entry);
    ++m_generation;
    emit generationChanged();
    emit capabilityChanged(entry.featureId, entry.operationId);
    return true;
}

int CapabilityRegistry::removeBackend(const QString &backendId)
{
    Q_ASSERT(thread() == QThread::currentThread());
    QVector<QPair<QString, QString>> affected;
    int removed = 0;
    for (qsizetype i = m_entries.size(); i > 0; --i) {
        const auto &entry = m_entries[i - 1];
        if (entry.backendId != backendId) continue;
        const QPair<QString, QString> key{entry.featureId, entry.operationId};
        if (!affected.contains(key)) affected.append(key);
        m_entries.removeAt(i - 1);
        ++removed;
    }
    if (!removed) return 0;
    ++m_generation;
    emit generationChanged();
    for (const auto &key : affected) emit capabilityChanged(key.first, key.second);
    return removed;
}

CapabilityResult CapabilityRegistry::resolve(const QString &featureId,
                                              const QString &operationId) const
{
    Q_ASSERT(thread() == QThread::currentThread());
    const CapabilityDescriptor *chosen = nullptr;
    for (const auto &entry : m_entries) {
        if (entry.featureId != featureId || entry.operationId != operationId) continue;
        if (!chosen || better(entry, *chosen)) chosen = &entry;
    }
    if (!chosen) return missing(featureId, operationId);
    return {featureId, operationId, chosen->backendId, chosen->backendVersion,
            chosen->available, chosen->available ? QString{} : chosen->unavailableReason};
}

QVariantMap CapabilityRegistry::query(const QString &featureId,
                                       const QString &operationId) const
{
    return resolve(featureId, operationId).toQml();
}

bool CapabilityRegistry::isAvailable(const QString &featureId,
                                      const QString &operationId) const
{
    return resolve(featureId, operationId).available;
}

QString CapabilityRegistry::disabledReason(const QString &featureId,
                                            const QString &operationId) const
{
    return resolve(featureId, operationId).reason;
}

QStringList CapabilityRegistry::operations(const QString &featureId) const
{
    Q_ASSERT(thread() == QThread::currentThread());
    QSet<QString> unique;
    for (const auto &entry : m_entries)
        if (entry.featureId == featureId) unique.insert(entry.operationId);
    QStringList result = unique.values();
    result.sort();
    return result;
}

void CapabilityBinding::setRegistry(CapabilityRegistry *registry)
{
    if (m_registry == registry) return;
    QObject::disconnect(m_changeConnection);
    QObject::disconnect(m_destroyConnection);
    m_registry = registry;
    if (registry) {
        m_changeConnection = connect(registry, &CapabilityRegistry::capabilityChanged,
                                     this, [this](const QString &feature, const QString &operation) {
            if (feature == m_featureId && operation == m_operationId) refresh();
        });
        m_destroyConnection = connect(registry, &QObject::destroyed, this, [this] {
            m_registry = nullptr;
            emit registryChanged();
            refresh();
        });
    }
    emit registryChanged();
    refresh();
}

void CapabilityBinding::setFeatureId(const QString &featureId)
{
    if (m_featureId == featureId) return;
    m_featureId = featureId;
    emit featureIdChanged();
    refresh();
}

void CapabilityBinding::setOperationId(const QString &operationId)
{
    if (m_operationId == operationId) return;
    m_operationId = operationId;
    emit operationIdChanged();
    refresh();
}

void CapabilityBinding::refresh()
{
    const auto next = m_registry ? m_registry->resolve(m_featureId, m_operationId)
                                 : missing(m_featureId, m_operationId);
    if (next == m_result) return;
    m_result = next;
    emit changed();
}

} // namespace QindaTK
