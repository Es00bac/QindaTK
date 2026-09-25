// SPDX-License-Identifier: LGPL-3.0-or-later
// Entity lifecycle for LocalPropertySyncClient: create, observed-remove
// delete and revival. See docs/collaboration.md "Objects and deletes" (D-166).
#include "local_property_sync.h"
#include "property_sync_detail_p.h"
#include <QUuid>

namespace QindaTK {
using namespace sync_detail;

bool LocalPropertySyncClient::isTextProperty(const QString &propertyId) const
{
    return m_textProperties.contains(propertyId);
}

QVariant LocalPropertySyncClient::baseValue(const QString &entityId,
                                            const QString &propertyId) const
{
    const QString key = keyOf(entityId, propertyId);
    if (m_baselineValues.contains(key)) return m_baselineValues.value(key);
    const auto descriptor = m_document ? m_document->descriptor(propertyId) : std::nullopt;
    return descriptor ? descriptor->defaultValue : QVariant{};
}

bool LocalPropertySyncClient::addSupport(const QString &entityId, const QString &operationId)
{
    auto &support = m_support[entityId];
    if (support.size() >= MaxObserved)
        return fail(QStringLiteral("Entity has too many create operations"));
    support.insert(operationId);
    return true;
}

// AGENT-CONTRACT (docs/collaboration.md "Objects and deletes"): an entity is
// alive while any supporter is not in the union of observed sets carried by
// Delete operations. Supporters are its baseline token (the entity UUID),
// its Create operations and the winning operation of each of its properties.
// The winners and the removed union are order independent, so every replica
// converges; an unobserved concurrent winning edit revives a deleted entity.
bool LocalPropertySyncClient::entityAlive(const QString &entityId) const
{
    const QSet<QString> removed = m_removed.value(entityId);
    if (m_baselineEntities.contains(entityId) && !removed.contains(entityId)) return true;
    for (const QString &id : m_support.value(entityId))
        if (!removed.contains(id)) return true;
    for (const QString &property : m_document->propertyIds()) {
        const auto winner = m_winners.constFind(keyOf(entityId, property));
        if (winner != m_winners.cend() && !removed.contains(winner->operationId)) return true;
    }
    return false;
}

bool LocalPropertySyncClient::reconcileEntity(const QString &entityId)
{
    const EntityId entity = EntityId::parse(entityId);
    const bool alive = entityAlive(entityId);
    const bool present = m_document->contains(entity);
    if (alive == present) return true;
    if (!alive) {
        if (!m_document->removeEntity(entity))
            return fail(QStringLiteral("Cannot remove synchronized entity"));
        m_expectedRevision = m_document->revision().value;
        return true;
    }
    QVariantMap values;
    for (const QString &property : m_document->propertyIds()) {
        if (isTextProperty(property)) {
            TextSequence *text = sequence(entityId, property);
            if (!text) return false;
            values.insert(property, visibleText(*text));
            continue;
        }
        const auto winner = m_winners.constFind(keyOf(entityId, property));
        values.insert(property, winner != m_winners.cend() ? winner->value
                                                           : baseValue(entityId, property));
    }
    if (!m_document->addEntity(entity, values))
        return fail(QStringLiteral("Cannot add synchronized entity"));
    m_expectedRevision = m_document->revision().value;
    return true;
}

bool LocalPropertySyncClient::receiveEntity(const PropertySyncOperation &operation)
{
    if (operation.kind == Kind::Create) {
        if (!addSupport(operation.entityId, operation.operationId)) return false;
    } else {
        const auto observed = decodeIdList(operation.value.toByteArray());
        if (!observed) return fail(QStringLiteral("Invalid delete operation"));
        auto &removed = m_removed[operation.entityId];
        for (const QString &id : *observed) removed.insert(id);
    }
    return reconcileEntity(operation.entityId);
}

QString LocalPropertySyncClient::createEntity(const QVariantMap &values)
{
    if (!readyToEdit()) return {};
    for (auto it = values.cbegin(); it != values.cend(); ++it) {
        const auto descriptor = m_document->descriptor(it.key());
        if (!descriptor || isTextProperty(it.key())
            || descriptor->type != it.value().metaType() || !encodableValue(it.value())) {
            fail(QStringLiteral("Initial values must be typed non-text properties"));
            return {};
        }
    }
    const QString entityId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto create = makeOperation(Kind::Create, entityId, entityProperty(), true);
    if (!submitLocal(create, step(Kind::Create, create.operationId, entityId)))
        return m_seen.contains(create.operationId) ? entityId : QString();
    for (auto it = values.cbegin(); it != values.cend(); ++it)
        if (!setValueImpl(entityId, it.key(), it.value(), false)) return entityId;
    return entityId;
}

bool LocalPropertySyncClient::deleteEntity(const QString &entityId)
{
    return deleteEntityImpl(entityId, true);
}

bool LocalPropertySyncClient::deleteEntityImpl(const QString &entityId, bool recordHistory)
{
    if (!readyToEdit()) return false;
    if (!canonicalUuid(entityId) || !m_document->contains(EntityId::parse(entityId)))
        return fail(QStringLiteral("Entity is not present"));
    // The delete names exactly the supporters this replica has observed.
    const QSet<QString> removed = m_removed.value(entityId);
    QStringList observed;
    if (m_baselineEntities.contains(entityId) && !removed.contains(entityId))
        observed.append(entityId);
    for (const QString &id : m_support.value(entityId))
        if (!removed.contains(id)) observed.append(id);
    for (const QString &property : m_document->propertyIds()) {
        const auto winner = m_winners.constFind(keyOf(entityId, property));
        if (winner != m_winners.cend() && !removed.contains(winner->operationId)
            && !observed.contains(winner->operationId))
            observed.append(winner->operationId);
    }
    if (observed.isEmpty() || observed.size() > MaxObserved)
        return fail(QStringLiteral("Entity delete cannot name its supporters"));
    observed.sort();
    const auto operation = makeOperation(Kind::Delete, entityId, entityProperty(),
                                         encodeIdList(observed));
    std::optional<HistoryEntry> entry;
    if (recordHistory) entry = step(Kind::Delete, operation.operationId, entityId);
    return submitLocal(operation, entry);
}

bool LocalPropertySyncClient::reviveEntity(const QString &entityId, bool recordHistory)
{
    if (!readyToEdit()) return false;
    if (m_document->contains(EntityId::parse(entityId)))
        return fail(QStringLiteral("Entity is already present"));
    // A new Create supporter revives the entity with its retained winners.
    const auto operation = makeOperation(Kind::Create, entityId, entityProperty(), true);
    std::optional<HistoryEntry> entry;
    if (recordHistory) entry = step(Kind::Create, operation.operationId, entityId);
    return submitLocal(operation, entry);
}

} // namespace QindaTK
