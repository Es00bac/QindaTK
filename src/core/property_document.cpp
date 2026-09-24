// SPDX-License-Identifier: LGPL-3.0-or-later
#include "property_document.h"
#include <QJsonDocument>
#include <QThread>

namespace QindaTK {
namespace {
OperationError error(ErrorCode code, const QString &message, const QString &field = {})
{
    return {code, message, field, {}};
}
}

PropertyCommand::PropertyCommand(QVector<PropertyChange> changes)
    : m_changes(std::move(changes)), m_bytes(estimate(m_changes)) {}

quint64 PropertyCommand::estimate(const QVector<PropertyChange> &changes)
{
    quint64 total = 0;
    for (const auto &change : changes) {
        const auto valueBytes = [](const QVariant &value) -> quint64 {
            if (value.metaType().id() == QMetaType::QByteArray)
                return static_cast<quint64>(value.toByteArray().size());
            if (value.metaType() == QMetaType::fromType<QVariantList>()
                || value.metaType() == QMetaType::fromType<QVariantMap>())
                return static_cast<quint64>(QJsonDocument::fromVariant(value)
                    .toJson(QJsonDocument::Compact).size());
            return static_cast<quint64>(value.toString().size()) * 2 + 32;
        };
        total += 128 + static_cast<quint64>(change.propertyId.size()) * 2
            + valueBytes(change.before) + valueBytes(change.after);
    }
    return total;
}

QVector<PropertyChange> PropertyCommand::joined(const QVector<PropertyChange> &first,
                                                const QVector<PropertyChange> &second)
{
    QVector<PropertyChange> result = first;
    for (const auto &change : second) {
        bool found = false;
        for (auto &existing : result) {
            if (existing.target != change.target || existing.propertyId != change.propertyId) continue;
            existing.after = change.after;
            found = true;
            break;
        }
        if (!found) result.append(change);
    }
    return result;
}

PropertyDocument::PropertyDocument(QObject *parent) : QObject(parent) {}

QMetaObject::Connection PropertyDocument::connectModelChanged(QObject *receiver,
                                                              std::function<void()> slot)
{
    return connect(this, &PropertyDocument::changed, receiver, std::move(slot));
}

OperationError PropertyDocument::prepareCommand(const DocumentCommand &command, bool forward) const
{
    const auto *step = dynamic_cast<const PropertyCommand *>(&command);
    if (!step) return error(ErrorCode::InvalidArgument, QStringLiteral("Step belongs to another document kind"));
    return checkCommitted(step->changes(), forward);
}

void PropertyDocument::publishCommand(const DocumentCommand &command, bool forward)
{
    const auto *step = dynamic_cast<const PropertyCommand *>(&command);
    Q_ASSERT(step);
    if (step) publishCommitted(step->changes(), forward);
}

DocumentCommandPtr PropertyDocument::composeCommands(const DocumentCommand &first,
                                                     const DocumentCommand &second) const
{
    const auto *a = dynamic_cast<const PropertyCommand *>(&first);
    const auto *b = dynamic_cast<const PropertyCommand *>(&second);
    if (!a || !b) return {};
    return std::make_shared<const PropertyCommand>(PropertyCommand::joined(a->changes(), b->changes()));
}

SnapshotHeader PropertyDocument::header() const
{
    return {QString::fromLatin1(SchemaId), m_documentId, {m_revision}, m_resetGeneration};
}

bool PropertyDocument::equalValue(const QVariant &a, const QVariant &b) const
{
    return a.metaType() == b.metaType() && a == b;
}

bool PropertyDocument::defineProperty(const PropertyDescriptor &entry)
{
    Q_ASSERT(thread() == QThread::currentThread());
    if (entry.id.isEmpty() || !entry.type.isValid() || !entry.defaultValue.isValid()
        || entry.defaultValue.metaType() != entry.type || m_descriptors.contains(entry.id)
        || !m_entities.isEmpty())
        return false;
    m_descriptors.insert(entry.id, entry);
    emit changed();
    return true;
}

std::optional<PropertyDescriptor> PropertyDocument::descriptor(const QString &id) const
{
    auto it = m_descriptors.constFind(id);
    if (it == m_descriptors.cend()) return std::nullopt;
    return it.value();
}

bool PropertyDocument::validEntityValues(const QVariantMap &values) const
{
    for (auto it = values.cbegin(); it != values.cend(); ++it) {
        const auto descriptor = m_descriptors.constFind(it.key());
        if (descriptor == m_descriptors.cend()
            || descriptor->type != it.value().metaType()) return false;
    }
    return true;
}

bool PropertyDocument::addEntity(EntityId id, const QVariantMap &values)
{
    Q_ASSERT(thread() == QThread::currentThread());
    const QString key = id.toString();
    if (!id.isValid() || m_entities.contains(key) || !validEntityValues(values)) return false;
    if (m_preview) discardPreview(m_preview->transactionId);
    EntityState state;
    for (auto it = m_descriptors.cbegin(); it != m_descriptors.cend(); ++it)
        state.values.insert(it.key(), values.value(it.key(), it->defaultValue));
    m_entities.insert(key, state);
    ++m_revision;
    invalidateHistory();
    emit historyInvalidated();
    emit changed();
    return true;
}

bool PropertyDocument::removeEntity(EntityId id)
{
    Q_ASSERT(thread() == QThread::currentThread());
    const QString key = id.toString();
    if (!m_entities.contains(key)) return false;
    if (m_preview) discardPreview(m_preview->transactionId);
    m_entities.remove(key);
    ++m_revision;
    emit entityRemoved(key);
    invalidateHistory();
    emit historyInvalidated();
    emit changed();
    return true;
}

bool PropertyDocument::contains(EntityId id) const
{
    return id.isValid() && m_entities.contains(id.toString());
}

bool PropertyDocument::resetModel(const QVector<QPair<EntityId, QVariantMap>> &entities)
{
    Q_ASSERT(thread() == QThread::currentThread());
    QHash<QString, EntityState> replacement;
    for (const auto &[id, values] : entities) {
        const QString key = id.toString();
        if (!id.isValid() || replacement.contains(key) || !validEntityValues(values)) return false;
        EntityState state;
        for (auto it = m_descriptors.cbegin(); it != m_descriptors.cend(); ++it)
            state.values.insert(it.key(), values.value(it.key(), it->defaultValue));
        replacement.insert(key, state);
    }
    if (m_preview) discardPreview(m_preview->transactionId);
    m_entities = std::move(replacement);
    ++m_revision;
    ++m_resetGeneration;
    emit reset();
    invalidateHistory();
    emit historyInvalidated();
    emit changed();
    return true;
}

QVariant PropertyDocument::committedValue(EntityId id, const QString &propertyId) const
{
    const auto entity = m_entities.constFind(id.toString());
    if (entity == m_entities.cend()) return {};
    return entity->values.value(propertyId);
}

QVariant PropertyDocument::value(EntityId id, const QString &propertyId) const
{
    if (m_preview)
        for (const auto &change : m_preview->changes)
            if (change.target == id && change.propertyId == propertyId) return change.after;
    return committedValue(id, propertyId);
}

QVariant PropertyDocument::valueFor(const QString &id, const QString &propertyId) const
{
    return value(EntityId::parse(id), propertyId);
}

bool PropertyDocument::containsId(const QString &id) const
{
    return contains(EntityId::parse(id));
}

bool PropertyDocument::setTargetWritable(EntityId id, const QString &propertyId, bool writable)
{
    Q_ASSERT(thread() == QThread::currentThread());
    auto entity = m_entities.find(id.toString());
    if (entity == m_entities.end() || !m_descriptors.contains(propertyId)) return false;
    if (m_preview) discardPreview(m_preview->transactionId);
    const bool old = !entity->readOnlyProperties.contains(propertyId);
    if (old == writable) return true;
    if (writable) entity->readOnlyProperties.remove(propertyId);
    else entity->readOnlyProperties.insert(propertyId);
    ++m_revision;
    emit propertyChanged(id.toString(), propertyId);
    invalidateHistory();
    emit historyInvalidated();
    emit changed();
    return true;
}

bool PropertyDocument::setTargetAnimated(EntityId id, const QString &propertyId, bool animated)
{
    Q_ASSERT(thread() == QThread::currentThread());
    auto entity = m_entities.find(id.toString());
    const auto descriptor = m_descriptors.constFind(propertyId);
    if (entity == m_entities.end() || descriptor == m_descriptors.cend()
        || (animated && !descriptor->animatable)) return false;
    if (m_preview) discardPreview(m_preview->transactionId);
    const bool old = entity->animatedProperties.contains(propertyId);
    if (old == animated) return true;
    if (animated) entity->animatedProperties.insert(propertyId);
    else entity->animatedProperties.remove(propertyId);
    ++m_revision;
    emit propertyChanged(id.toString(), propertyId);
    invalidateHistory();
    emit historyInvalidated();
    emit changed();
    return true;
}

bool PropertyDocument::isTargetWritable(EntityId id, const QString &propertyId) const
{
    const auto entity = m_entities.constFind(id.toString());
    const auto entry = m_descriptors.constFind(propertyId);
    return entity != m_entities.cend() && entry != m_descriptors.cend()
        && entry->writable && !entity->readOnlyProperties.contains(propertyId);
}

bool PropertyDocument::isTargetAnimated(EntityId id, const QString &propertyId) const
{
    const auto entity = m_entities.constFind(id.toString());
    return entity != m_entities.cend() && entity->animatedProperties.contains(propertyId);
}

OperationError PropertyDocument::validate(EntityId id, const QString &propertyId,
                                          const QVariant &candidate) const
{
    if (!contains(id)) return error(ErrorCode::Unavailable, QStringLiteral("Target no longer exists"), propertyId);
    const auto entry = m_descriptors.constFind(propertyId);
    if (entry == m_descriptors.cend())
        return error(ErrorCode::InvalidArgument, QStringLiteral("Unknown property"), propertyId);
    if (!isTargetWritable(id, propertyId))
        return error(ErrorCode::Unavailable, QStringLiteral("Property is read only"), propertyId);
    if (!candidate.isValid() || candidate.metaType() != entry->type)
        return error(ErrorCode::Validation, QStringLiteral("Value has the wrong type"), propertyId);
    return {};
}

OperationError PropertyDocument::applySynchronizedValue(EntityId id,
                                                         const QString &propertyId,
                                                         const QVariant &candidate)
{
    Q_ASSERT(thread() == QThread::currentThread());
    if (m_preview)
        return error(ErrorCode::Unavailable, QStringLiteral("An edit preview is active"));
    if (auto failure = validate(id, propertyId, candidate)) return failure;
    if (equalValue(committedValue(id, propertyId), candidate)) return {};
    // AGENT-CONTRACT (docs/collaboration.md): synchronized edits bypass
    // UndoService. An old inverse step must never replay over a remote value.
    m_entities[id.toString()].values.insert(propertyId, candidate);
    ++m_revision;
    invalidateHistory();
    emit historyInvalidated();
    emit propertyChanged(id.toString(), propertyId);
    emit changed();
    return {};
}

OperationError PropertyDocument::showPreview(const QUuid &transactionId,
                                             const QVector<PropertyChange> &changes)
{
    Q_ASSERT(thread() == QThread::currentThread());
    if (m_preview && m_preview->transactionId != transactionId)
        return error(ErrorCode::Unavailable, QStringLiteral("Another edit is active"));
    for (const auto &change : changes) {
        if (auto failure = validate(change.target, change.propertyId, change.after)) return failure;
        if (!equalValue(committedValue(change.target, change.propertyId), change.before))
            return error(ErrorCode::StaleRevision, QStringLiteral("Property changed during edit"), change.propertyId);
    }
    const QVector<PropertyChange> previous = m_preview ? m_preview->changes : QVector<PropertyChange>{};
    m_preview = Preview{transactionId, changes};
    for (const auto &change : previous) emit propertyChanged(change.target.toString(), change.propertyId);
    for (const auto &change : changes) emit propertyChanged(change.target.toString(), change.propertyId);
    emit changed();
    return {};
}

void PropertyDocument::discardPreview(const QUuid &transactionId)
{
    Q_ASSERT(thread() == QThread::currentThread());
    if (!m_preview || m_preview->transactionId != transactionId) return;
    const QVector<PropertyChange> changes = m_preview->changes;
    m_preview.reset();
    for (const auto &change : changes) emit propertyChanged(change.target.toString(), change.propertyId);
    emit changed();
}

OperationError PropertyDocument::checkCommitted(const QVector<PropertyChange> &changes,
                                                bool forward) const
{
    Q_ASSERT(thread() == QThread::currentThread());
    if (m_preview) return error(ErrorCode::Unavailable, QStringLiteral("An edit preview is active"));
    for (const auto &change : changes) {
        if (!contains(change.target) || !m_descriptors.contains(change.propertyId))
            return error(ErrorCode::Unavailable, QStringLiteral("Undo target no longer exists"), change.propertyId);
        const QVariant expected = forward ? change.before : change.after;
        if (!equalValue(committedValue(change.target, change.propertyId), expected))
            return error(ErrorCode::StaleRevision, QStringLiteral("Property changed since this edit"), change.propertyId);
    }
    return {};
}

void PropertyDocument::publishCommitted(const QVector<PropertyChange> &changes, bool forward)
{
    // AGENT-GUARD: publish the whole multi-target edit before notifying any
    // binding; observers must never see a half-applied transaction.
    for (const auto &change : changes)
        m_entities[change.target.toString()].values.insert(change.propertyId,
                                                            forward ? change.after : change.before);
    if (!changes.isEmpty()) ++m_revision;
    for (const auto &change : changes)
        emit propertyChanged(change.target.toString(), change.propertyId);
    emit changed();
}

OperationError PropertyDocument::applyCommitted(const QVector<PropertyChange> &changes, bool forward)
{
    if (auto failure = checkCommitted(changes, forward)) return failure;
    publishCommitted(changes, forward);
    return {};
}

} // namespace QindaTK
