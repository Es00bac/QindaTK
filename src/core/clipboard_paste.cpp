// SPDX-License-Identifier: LGPL-3.0-or-later
#include "clipboard_service.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QSet>
#include <QThread>
#include <QVariantList>
#include <QUuid>
#include <algorithm>
#include <utility>

// Paste path of ClipboardService: reading the clipboard's payload, checking it
// against the target property and committing it through an EditTransaction.
namespace QindaTK {
namespace {

OperationError error(ErrorCode code, const QString &message,
                     const QString &field = {})
{ return {code, message, field, {}}; }

bool resourceKey(const QString &key)
{
    const QString normalized = key.toCaseFolded();
    return normalized == QStringLiteral("missingresource")
        || normalized == QStringLiteral("resource")
        || normalized == QStringLiteral("resources")
        || normalized == QStringLiteral("resourceid")
        || normalized == QStringLiteral("resourceuri")
        || normalized == QStringLiteral("assetid")
        || normalized == QStringLiteral("asseturi")
        || normalized == QStringLiteral("uri")
        || normalized == QStringLiteral("references")
        || normalized == QStringLiteral("referenceids")
        || normalized == QStringLiteral("documentid");
}

bool containsUnresolvedReference(const QJsonValue &value, bool root,
                                 QString *missing)
{
    if (value.isObject()) {
        const QJsonObject object = value.toObject();
        for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
            if (resourceKey(it.key())) {
                if ((it.key().compare(QStringLiteral("missingResource"),
                                      Qt::CaseInsensitive) == 0)
                    || (it.value().isObject()
                        && it.value().toObject().value(QStringLiteral("available")) == false))
                    *missing = QStringLiteral("The clipboard object refers to a missing embedded resource");
                else
                    *missing = QStringLiteral("The clipboard object contains a resource or reference that this adapter cannot resolve");
                return true;
            }
            if (root && it.key() == QStringLiteral("id")) continue;
            if (containsUnresolvedReference(it.value(), false, missing)) return true;
        }
    } else if (value.isArray()) {
        for (const auto &child : value.toArray())
            if (containsUnresolvedReference(child, false, missing)) return true;
    } else if (value.isString()) {
        const QString text = value.toString();
        const QUuid id(text);
        if (!id.isNull() && id.toString(QUuid::WithoutBraces) == text) {
            *missing = QStringLiteral("The clipboard object contains an unresolved entity reference");
            return true;
        }
    }
    return false;
}

std::optional<TransferPayload> externalPayload(const QMimeData *mime,
                                                QString *format,
                                                OperationError *failure)
{
    if (!mime) {
        *failure = error(ErrorCode::Unavailable, QStringLiteral("Clipboard is empty"));
        return std::nullopt;
    }
    if (mime->hasFormat(QString::fromLatin1(TransferPayloadCodec::InternalMime))) {
        *format = QString::fromLatin1(TransferPayloadCodec::InternalMime);
        return TransferPayloadCodec::decode(
            mime->data(*format), failure);
    }
    TransferPayload payload{DocumentId::create(), RevisionId{}, TransferOperation::Copy, {}};
    if (mime->hasFormat(QStringLiteral("text/plain"))) {
        *format = QStringLiteral("text/plain");
        const QByteArray bytes = mime->data(*format);
        if (bytes.size() > TransferPayloadCodec::MaximumTextBytes) {
            *failure = error(ErrorCode::Validation, QStringLiteral("Clipboard text exceeds 4 MiB"));
            return std::nullopt;
        }
        const QString text = QString::fromUtf8(bytes);
        if (text.toUtf8() != bytes) {
            *failure = error(ErrorCode::Validation, QStringLiteral("Clipboard text is not valid UTF-8"));
            return std::nullopt;
        }
        payload.items.append({EntityId::create(), TransferItemKind::Text,
                              {{QStringLiteral("text"), text}}});
        return payload;
    }
    if (mime->hasFormat(QStringLiteral("image/png"))) {
        *format = QStringLiteral("image/png");
        const QByteArray png = mime->data(*format);
        payload.items.append({EntityId::create(), TransferItemKind::Image,
                              {{QStringLiteral("pngBase64"),
                                QString::fromLatin1(png.toBase64())}}});
        OperationError validation;
        if (TransferPayloadCodec::encode(payload, &validation).isEmpty()) {
            *failure = validation;
            return std::nullopt;
        }
        return payload;
    }
    *failure = error(ErrorCode::Unsupported, QStringLiteral("Clipboard has no supported transfer format"));
    return std::nullopt;
}

} // namespace

OperationError ClipboardService::prepare(PropertyDocument *document, EntityId target,
                                          const QString &propertyId,
                                          PreparedPaste *result,
                                          qsizetype insertionIndex) const
{
    if (!document || !document->contains(target))
        return fail(ErrorCode::Unavailable, QStringLiteral("Paste target does not exist"));
    const auto descriptor = document->descriptor(propertyId);
    if (!descriptor || !descriptor->writable || !document->isTargetWritable(target, propertyId))
        return fail(ErrorCode::Unavailable, QStringLiteral("Paste target property is missing or read only"), propertyId);
    QString format;
    OperationError decodeError;
    const QMimeData *mime = m_adapter ? m_adapter->mimeData() : nullptr;
    auto payload = externalPayload(mime, &format, &decodeError);
    if (!payload) return decodeError;
    if (payload->operation == TransferOperation::Link)
        return fail(ErrorCode::Unsupported, QStringLiteral("Link paste requires a domain link adapter"));
    if (payload->operation == TransferOperation::Move
        && (m_cutEnvelope.isEmpty()
            || m_cutEnvelope != mime->data(QString::fromLatin1(TransferPayloadCodec::InternalMime))))
        return fail(ErrorCode::Unsupported, QStringLiteral("Move payload has no live same-document cut transaction"));
    result->payload = *payload;
    result->format = format;
    result->warnings.clear();
    if (format != QString::fromLatin1(TransferPayloadCodec::InternalMime)) {
        result->warnings.append(QStringLiteral("External clipboard data has no QindaTK document revision or entity metadata."));
    }
    if (payload->items.size() == 1 && payload->items.front().kind == TransferItemKind::Text) {
        if (descriptor->type.id() != QMetaType::QString)
            return fail(ErrorCode::Unsupported, QStringLiteral("Text paste requires a QString property"), propertyId);
        result->value = payload->items.front().payload.value(QStringLiteral("text")).toString();
    } else if (payload->items.size() == 1 && payload->items.front().kind == TransferItemKind::Image) {
        if (descriptor->type.id() != QMetaType::QByteArray)
            return fail(ErrorCode::Unsupported, QStringLiteral("PNG paste requires a QByteArray property"), propertyId);
        const auto decoded = QByteArray::fromBase64Encoding(
            payload->items.front().payload.value(QStringLiteral("pngBase64")).toString().toLatin1(),
            QByteArray::AbortOnBase64DecodingErrors);
        if (!decoded) return fail(ErrorCode::Validation, QStringLiteral("Clipboard PNG encoding is malformed"));
        result->value = decoded.decoded;
    } else {
        if (descriptor->type.id() != QMetaType::QVariantList)
            return fail(ErrorCode::Unsupported, QStringLiteral("Typed object paste requires a QVariantList collection"), propertyId);
        QVariantList objects = document->committedValue(target, propertyId).toList();
        QVariantList moving;
        for (const TransferItem &item : payload->items) {
            if (item.kind != TransferItemKind::Object)
                return fail(ErrorCode::Unsupported, QStringLiteral("Mixed or range clipboard data is not supported by this collection"));
            QString unresolved;
            if (payload->operation != TransferOperation::Move
                && containsUnresolvedReference(item.payload, true, &unresolved))
                return fail(unresolved.contains(QStringLiteral("missing"), Qt::CaseInsensitive)
                                ? ErrorCode::Unavailable : ErrorCode::Unsupported,
                            unresolved, item.entityId.toString());
            QVariantMap map = item.payload.toVariantMap();
            if (payload->operation == TransferOperation::Move) {
                if (document->revision().value != m_cutRevision)
                    return fail(ErrorCode::StaleRevision, QStringLiteral("Document changed after cut; paste the cut payload again after copying fresh state"));
                if (payload->sourceDocumentId != document->documentId()
                    || m_cutSourceDocument != document->documentIdString()
                    || m_cutCollection != target.toString() || m_cutProperty != propertyId)
                    return fail(ErrorCode::Unsupported, QStringLiteral("Cross-document and cross-collection moves are unsupported"));
                moving.append(map);
            } else {
                map.insert(QStringLiteral("id"), EntityId::create().toString());
                moving.append(map);
            }
        }
        if (payload->operation == TransferOperation::Move) {
            QSet<QString> selected;
            for (const auto &item : payload->items) selected.insert(item.entityId.toString());
            QVariantList remaining;
            for (const QVariant &value : std::as_const(objects))
                if (!selected.contains(value.toMap().value(QStringLiteral("id")).toString())) remaining.append(value);
            if (remaining.size() + moving.size() != objects.size())
                return fail(ErrorCode::Unavailable, QStringLiteral("Cut objects are no longer present in the source collection"));
            const qsizetype index = insertionIndex < 0 ? remaining.size()
                : std::clamp(insertionIndex, qsizetype(0), qsizetype(remaining.size()));
            for (qsizetype i = 0; i < moving.size(); ++i) remaining.insert(index + i, moving[i]);
            objects = std::move(remaining);
        } else {
            const qsizetype index = insertionIndex < 0 ? objects.size()
                : std::clamp(insertionIndex, qsizetype(0), qsizetype(objects.size()));
            for (qsizetype i = 0; i < moving.size(); ++i) objects.insert(index + i, moving[i]);
        }
        result->value = objects;
    }
    return {};
}

QVariantMap ClipboardService::inspectPaste(PropertyDocument *document, EntityId target,
                                           const QString &propertyId) const
{
    PreparedPaste prepared;
    const OperationError failure = prepare(document, target, propertyId, &prepared);
    QString state = QStringLiteral("ready");
    if (failure.code == ErrorCode::Unsupported) state = QStringLiteral("unsupported");
    else if (failure.code == ErrorCode::Unavailable) state = QStringLiteral("missing");
    else if (failure.code == ErrorCode::StaleRevision) state = QStringLiteral("stale");
    else if (failure) state = QStringLiteral("malformed");
    return {{QStringLiteral("canPaste"), !failure},
            {QStringLiteral("state"), state},
            {QStringLiteral("format"), prepared.format},
            {QStringLiteral("lossWarnings"), prepared.warnings},
            {QStringLiteral("message"), failure.message}};
}

OperationError ClipboardService::paste(PropertyDocument *document, UndoService *undo,
                                        EntityId target, const QString &propertyId,
                                        qsizetype insertionIndex)
{
    Q_ASSERT(thread() == QThread::currentThread());
    PreparedPaste prepared;
    if (auto failure = prepare(document, target, propertyId, &prepared, insertionIndex)) {
        if (failure.code == ErrorCode::StaleRevision) {
            clearPendingCut();
            refresh();
        }
        setError(failure);
        return failure;
    }
    EditTransaction transaction;
    transaction.setDocument(document);
    transaction.setUndoService(undo);
    if (!transaction.begin({target}, propertyId,
                           prepared.payload.operation == TransferOperation::Move
                               ? QStringLiteral("Move clipboard objects")
                               : QStringLiteral("Paste from clipboard"))) {
        const auto failure = transaction.error();
        setError(failure);
        return failure;
    }
    if (auto failure = transaction.updateValue(prepared.value)) {
        setError(failure);
        return failure;
    }
    if (auto failure = transaction.commitEdit()) {
        setError(failure);
        return failure;
    }
    if (prepared.payload.operation == TransferOperation::Move) {
        clearPendingCut();
        refresh();
    }
    m_lastError.clear();
    m_lossWarnings = prepared.warnings;
    emit changed();
    return {};
}

} // namespace QindaTK
