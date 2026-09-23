// SPDX-License-Identifier: LGPL-3.0-or-later
#include "drag_drop_service.h"
#include "edit_transaction.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QSet>
#include <QUuid>
#include <cmath>
#include <utility>

namespace QindaTK {
namespace {
OperationError makeError(ErrorCode code, const QString &message)
{
    return {code, message, {}, {}};
}

bool unresolvedReferenceKey(const QString &key)
{
    const QString normalized = key.toCaseFolded();
    return normalized == QStringLiteral("missingresource")
        || normalized == QStringLiteral("resource")
        || normalized == QStringLiteral("resources")
        || normalized == QStringLiteral("resourceid")
        || normalized == QStringLiteral("resourceids")
        || normalized == QStringLiteral("resourceuri")
        || normalized == QStringLiteral("assetid")
        || normalized == QStringLiteral("asseturi")
        || normalized == QStringLiteral("uri")
        || normalized == QStringLiteral("references")
        || normalized == QStringLiteral("referenceid")
        || normalized == QStringLiteral("referenceids")
        || normalized == QStringLiteral("documentid");
}

bool hasUnresolvedReference(const QJsonValue &value, bool root, QString *reason)
{
    if (value.isObject()) {
        const QJsonObject object = value.toObject();
        for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
            if (unresolvedReferenceKey(it.key())) {
                *reason = QStringLiteral("Object rows with resource or reference fields need a domain resolver.");
                return true;
            }
            if (root && it.key() == QStringLiteral("id")) continue;
            if (hasUnresolvedReference(it.value(), false, reason)) return true;
        }
    } else if (value.isArray()) {
        for (const QJsonValue &child : value.toArray())
            if (hasUnresolvedReference(child, false, reason)) return true;
    } else if (value.isString()) {
        const QString text = value.toString();
        const QUuid id(text);
        if (!id.isNull() && id.toString(QUuid::WithoutBraces) == text) {
            *reason = QStringLiteral("Object row contains an entity reference this collection adapter cannot resolve.");
            return true;
        }
    }
    return false;
}

bool sameSnapshot(const SnapshotHeader &a, const SnapshotHeader &b)
{
    return a.schemaId == b.schemaId && a.documentId == b.documentId
        && a.revision == b.revision && a.resetGeneration == b.resetGeneration;
}
}

PropertyCollectionDropTarget::PropertyCollectionDropTarget(
    PropertyDocument *document, UndoService *undo, EntityId collectionEntity,
    QString propertyId, QString targetId, QString viewId, QObject *parent)
    : DropTarget(parent), m_document(document), m_undo(undo),
      m_collectionEntity(collectionEntity), m_propertyId(std::move(propertyId)),
      m_targetId(std::move(targetId)), m_viewId(std::move(viewId))
{
}

SnapshotHeader PropertyCollectionDropTarget::snapshot() const
{
    return m_document ? m_document->header() : SnapshotHeader{};
}

DropPreview PropertyCollectionDropTarget::previewDrop(
    const QString &sessionId, const TransferPayload &payload,
    TransferOperation operation, const QVariantMap &location)
{
    if (sessionId.isEmpty() || m_plans.contains(sessionId))
        return {false, {}, QStringLiteral("Drop session is missing or already has a plan."),
                ErrorCode::Conflict, {}, false};
    if (!m_document || !m_undo || !m_collectionEntity.isValid()
        || !m_document->contains(m_collectionEntity) || m_propertyId.isEmpty())
        return {false, {}, QStringLiteral("Collection document, undo service, or entity is unavailable."),
                ErrorCode::Unavailable, {}, false};
    if (operation != TransferOperation::Copy)
        return {false, {}, QStringLiteral("Reference collection adapter supports Copy only."),
                ErrorCode::Unsupported, {}, false};
    if (payload.items.isEmpty())
        return {false, {}, QStringLiteral("Transfer contains no objects."),
                ErrorCode::InvalidArgument, {}, false};
    const auto descriptor = m_document->descriptor(m_propertyId);
    if (!descriptor || descriptor->type != QMetaType::fromType<QVariantList>()
        || !descriptor->writable
        || !m_document->isTargetWritable(m_collectionEntity, m_propertyId))
        return {false, {}, QStringLiteral("Target property is not a writable QVariantList collection."),
                ErrorCode::Unsupported, {}, false};

    QVariantList candidate = m_document->committedValue(m_collectionEntity,
                                                          m_propertyId).toList();
    QSet<QString> existingIds;
    for (const QVariant &rowValue : std::as_const(candidate)) {
        const QVariantMap row = rowValue.toMap();
        const QString idText = row.value(QStringLiteral("id")).toString();
        const EntityId id = EntityId::parse(idText);
        const QString canonicalId = id.toString();
        if (row.isEmpty() || !id.isValid() || existingIds.contains(canonicalId))
            return {false, {}, QStringLiteral("Target collection has a missing or duplicate row ID."),
                    ErrorCode::Validation, {}, false};
        existingIds.insert(canonicalId);
    }

    qlonglong index = candidate.size();
    const QVariant indexValue = location.value(QStringLiteral("index"));
    if (indexValue.isValid()) {
        bool ok = false;
        if (indexValue.metaType().id() == QMetaType::Double
            || indexValue.metaType().id() == QMetaType::Float) {
            const double number = indexValue.toDouble(&ok);
            if (!std::isfinite(number) || std::floor(number) != number
                || number < 0.0 || number > static_cast<double>(candidate.size()))
                ok = false;
            else
                index = static_cast<qlonglong>(number);
        } else {
            index = indexValue.toLongLong(&ok);
            ok = ok && index >= 0 && index <= candidate.size();
        }
        if (!ok)
            return {false, {}, QStringLiteral("Drop index is outside the collection."),
                    ErrorCode::Validation, {}, false};
    }

    OperationError codecError;
    if (TransferPayloadCodec::encode(payload, &codecError).isEmpty())
        return {false, {}, codecError.message, codecError.code, {}, false};
    QVector<QVariantMap> rows;
    rows.reserve(payload.items.size());
    QSet<QString> incomingIds;
    for (const TransferItem &item : payload.items) {
        if (item.kind != TransferItemKind::Object)
            return {false, {}, QStringLiteral("Collection adapter accepts object items only."),
                    ErrorCode::Unsupported, {}, false};
        const QString sourceId = item.entityId.toString();
        QVariantMap row = item.payload.toVariantMap();
        const QString embeddedId = row.value(QStringLiteral("id")).toString();
        const EntityId embeddedEntity = EntityId::parse(embeddedId);
        if (!embeddedEntity.isValid() || embeddedEntity != item.entityId
            || incomingIds.contains(sourceId))
            return {false, {}, QStringLiteral("Object row IDs are missing, duplicated, or inconsistent with transfer identities."),
                    ErrorCode::Validation, {}, false};
        QString unresolved;
        if (hasUnresolvedReference(QJsonValue::fromVariant(row), true, &unresolved))
            return {false, {}, unresolved, ErrorCode::Unsupported, {}, false};
        incomingIds.insert(sourceId);
        row.insert(QStringLiteral("id"), EntityId::create().toString());
        rows.append(std::move(row));
    }

    for (qsizetype offset = 0; offset < rows.size(); ++offset)
        candidate.insert(static_cast<qsizetype>(index) + offset, rows.at(offset));
    if (m_plans.size() >= 64)
        return {false, {}, QStringLiteral("Too many unfinished collection previews."),
                ErrorCode::Unavailable, {}, false};
    Plan plan;
    plan.token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    plan.snapshot = m_document->header();
    plan.candidate = std::move(candidate);
    const QString token = plan.token;
    m_plans.insert(sessionId, std::move(plan));
    return {true, QStringLiteral("Copy %1 object(s)").arg(rows.size()), {},
            ErrorCode::None, token, false};
}

OperationError PropertyCollectionDropTarget::commitDrop(
    const QString &sessionId, const QString &previewToken)
{
    const auto it = m_plans.find(sessionId);
    if (it == m_plans.end() || it->token != previewToken)
        return makeError(ErrorCode::Conflict,
                         QStringLiteral("Collection preview token is missing or already consumed."));
    const Plan plan = std::move(it.value());
    m_plans.erase(it);
    if (!m_document || !m_undo || !sameSnapshot(m_document->header(), plan.snapshot))
        return makeError(ErrorCode::StaleRevision,
                         QStringLiteral("Collection changed after the drop preview."));
    EditTransaction edit;
    edit.setDocument(m_document);
    edit.setUndoService(m_undo);
    if (!edit.begin({m_collectionEntity}, m_propertyId, QStringLiteral("Drop objects")))
        return edit.error();
    if (const OperationError error = edit.updateValue(plan.candidate); error)
        return error;
    return edit.commitEdit();
}

void PropertyCollectionDropTarget::cancelDrop(const QString &sessionId,
                                              const QString &previewToken)
{
    const auto it = m_plans.find(sessionId);
    if (it == m_plans.end()) return;
    if (previewToken.isEmpty() || it->token == previewToken)
        m_plans.erase(it);
}
} // namespace QindaTK
