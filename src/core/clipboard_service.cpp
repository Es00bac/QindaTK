// SPDX-License-Identifier: LGPL-3.0-or-later
#include "clipboard_service.h"

#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonValue>
#include <QSet>
#include <QThread>
#include <QVariantList>
#include <QUuid>
#include <algorithm>
#include <utility>

namespace QindaTK {
namespace {

class SystemClipboardAdapter final : public ClipboardAdapter {
public:
    explicit SystemClipboardAdapter(QObject *parent = nullptr)
        : ClipboardAdapter(parent), m_clipboard(QGuiApplication::clipboard())
    {
        if (m_clipboard)
            connect(m_clipboard, &QClipboard::changed, this,
                    [this] { emit changed(); });
    }
    const QMimeData *mimeData() const override
    { return m_clipboard ? m_clipboard->mimeData() : nullptr; }
    bool setMimeData(QMimeData *data) override
    {
        if (!m_clipboard || !data) { delete data; return false; }
        m_clipboard->setMimeData(data, QClipboard::Clipboard);
        return true;
    }
private:
    QClipboard *m_clipboard = nullptr;
};

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

ClipboardService::ClipboardService(QObject *parent) : QObject(parent)
{
    m_ownedAdapter = std::make_unique<SystemClipboardAdapter>();
    setAdapter(m_ownedAdapter.get());
}

ClipboardService::~ClipboardService() = default;

void ClipboardService::setAdapter(ClipboardAdapter *adapter)
{
    Q_ASSERT(thread() == QThread::currentThread());
    if (m_adapter == adapter) return;
    if (m_adapter) disconnect(m_adapter, nullptr, this, nullptr);
    if (adapter != m_ownedAdapter.get()) m_ownedAdapter.reset();
    m_adapter = adapter;
    if (m_adapter)
        connect(m_adapter, &ClipboardAdapter::changed, this,
                &ClipboardService::refresh);
    refresh();
}

OperationError ClipboardService::fail(ErrorCode code, const QString &message,
                                       const QString &field) const
{ return error(code, message, field); }

void ClipboardService::setError(const OperationError &failure)
{
    m_lastError = failure.message;
    if (failure) emit rejected(failure.message, static_cast<int>(failure.code));
    emit changed();
}

void ClipboardService::refresh()
{
    QStringList formats;
    const QMimeData *mime = m_adapter ? m_adapter->mimeData() : nullptr;
    if (mime) formats = mime->formats();
    std::sort(formats.begin(), formats.end());
    bool supported = false;
    std::optional<TransferPayload> internal;
    if (mime && mime->hasFormat(QString::fromLatin1(TransferPayloadCodec::InternalMime))) {
        OperationError decodeError;
        internal = TransferPayloadCodec::decode(
            mime->data(QString::fromLatin1(TransferPayloadCodec::InternalMime)), &decodeError);
        supported = internal && internal->operation == TransferOperation::Copy;
    } else if (mime) {
        supported = mime->hasFormat(QStringLiteral("application/vnd.qindatk.canvas-paths"))
            || mime->hasFormat(QStringLiteral("text/plain"))
            || mime->hasFormat(QStringLiteral("image/png"));
    }
    const QByteArray internalBytes = mime && mime->hasFormat(
        QString::fromLatin1(TransferPayloadCodec::InternalMime))
        ? mime->data(QString::fromLatin1(TransferPayloadCodec::InternalMime)) : QByteArray{};
    bool cutChanged = false;
    if (!m_cutEnvelope.isEmpty()
        && (!mime || !mime->hasFormat(QString::fromLatin1(TransferPayloadCodec::InternalMime))
            || internalBytes != m_cutEnvelope)) {
        cutChanged = clearPendingCut();
    }
    if (internal && internal->operation == TransferOperation::Move)
        supported = cutPending() && internalBytes == m_cutEnvelope
            && internal->sourceDocumentId.toString() == m_cutSourceDocument;
    if (formats == m_formats && supported == m_canPaste && !cutChanged) return;
    m_formats = formats;
    m_canPaste = supported;
    emit changed();
}

bool ClipboardService::clearPendingCut()
{
    const bool hadCut = !m_cutEnvelope.isEmpty();
    m_cutEnvelope.clear();
    m_cutSourceDocument.clear();
    m_cutCollection.clear();
    m_cutProperty.clear();
    m_cutRevision = 0;
    m_cutDocument = nullptr;
    return hadCut;
}

OperationError ClipboardService::publishPayload(const TransferPayload &payload)
{
    if (!m_adapter)
        return fail(ErrorCode::Unavailable, QStringLiteral("Clipboard adapter is unavailable"));
    OperationError failure;
    const auto alternatives = TransferPayloadCodec::mimeAlternatives(payload, &failure);
    if (alternatives.isEmpty()) return failure;
    auto *mime = new QMimeData;
    for (auto it = alternatives.cbegin(); it != alternatives.cend(); ++it)
        mime->setData(it.key(), it.value());
    if (!m_adapter->setMimeData(mime)) {
        return fail(ErrorCode::Unavailable, QStringLiteral("Clipboard adapter rejected the payload"));
    }
    m_lastError.clear();
    m_lossWarnings.clear();
    refresh();
    return {};
}

OperationError ClipboardService::copyPayload(const TransferPayload &payload)
{
    Q_ASSERT(thread() == QThread::currentThread());
    const auto result = publishPayload(payload);
    setError(result);
    return result;
}

bool ClipboardService::copyText(const QString &text, const QString &documentId,
                                 const QString &entityId, const QString &revision)
{
    bool revisionOk = false;
    const quint64 revisionValue = revision.toULongLong(&revisionOk, 10);
    const DocumentId doc = DocumentId::parse(documentId);
    const EntityId entity = EntityId::parse(entityId);
    if (!doc.isValid() || !entity.isValid() || !revisionOk
        || QString::number(revisionValue) != revision) {
        setError(fail(ErrorCode::InvalidArgument, QStringLiteral("Copy requires canonical document/entity IDs and revision")));
        return false;
    }
    const TransferPayload payload{doc, RevisionId{revisionValue}, TransferOperation::Copy,
        {{entity, TransferItemKind::Text, {{QStringLiteral("text"), text}}}}};
    return !copyPayload(payload);
}

bool ClipboardService::copyImage(const QByteArray &png, const QString &documentId,
                                  const QString &entityId, const QString &revision)
{
    bool revisionOk = false;
    const quint64 revisionValue = revision.toULongLong(&revisionOk, 10);
    const DocumentId doc = DocumentId::parse(documentId);
    const EntityId entity = EntityId::parse(entityId);
    if (!doc.isValid() || !entity.isValid() || !revisionOk
        || QString::number(revisionValue) != revision) {
        setError(fail(ErrorCode::InvalidArgument, QStringLiteral("Copy requires canonical document/entity IDs and revision")));
        return false;
    }
    const TransferPayload payload{doc, RevisionId{revisionValue}, TransferOperation::Copy,
        {{entity, TransferItemKind::Image,
          {{QStringLiteral("pngBase64"), QString::fromLatin1(png.toBase64())}}}}};
    return !copyPayload(payload);
}

OperationError ClipboardService::copyObjects(PropertyDocument *document,
                                              EntityId collection,
                                              const QString &propertyId,
                                              const QVector<EntityId> &objectIds,
                                              TransferOperation operation)
{
    Q_ASSERT(thread() == QThread::currentThread());
    const auto reject = [this](ErrorCode code, const QString &message) {
        const auto failure = fail(code, message);
        setError(failure);
        return failure;
    };
    if (!document || !document->contains(collection) || objectIds.isEmpty()
        || !document->descriptor(propertyId)
        || document->descriptor(propertyId)->type.id() != QMetaType::QVariantList) {
        return reject(ErrorCode::InvalidArgument, QStringLiteral("Object copy requires an existing QVariantList collection"));
    }
    const QVariantList objects = document->committedValue(collection, propertyId).toList();
    QSet<QString> requested;
    TransferPayload payload{document->documentId(), document->revision(), operation, {}};
    for (const EntityId &id : objectIds) {
        if (!id.isValid() || requested.contains(id.toString()))
            return reject(ErrorCode::Validation, QStringLiteral("Object IDs must be valid and unique"));
        requested.insert(id.toString());
        bool found = false;
        for (const QVariant &value : objects) {
            const QVariantMap map = value.toMap();
            if (map.value(QStringLiteral("id")).toString() != id.toString()) continue;
            QJsonObject encoded = QJsonObject::fromVariantMap(map);
            payload.items.append({id, TransferItemKind::Object, encoded});
            found = true;
            break;
        }
        if (!found) return reject(ErrorCode::Unavailable, QStringLiteral("An object selected for copy is no longer in the collection"));
    }
    const auto result = publishPayload(payload);
    setError(result);
    return result;
}

OperationError ClipboardService::cutObjects(PropertyDocument *document,
                                             EntityId collection,
                                             const QString &propertyId,
                                             const QVector<EntityId> &objectIds)
{
    const auto result = copyObjects(document, collection, propertyId, objectIds,
                                    TransferOperation::Move);
    if (result) { setError(result); return result; }
    const QMimeData *mime = m_adapter ? m_adapter->mimeData() : nullptr;
    m_cutEnvelope = mime ? mime->data(QString::fromLatin1(TransferPayloadCodec::InternalMime)) : QByteArray{};
    m_cutSourceDocument = document->documentIdString();
    m_cutCollection = collection.toString();
    m_cutProperty = propertyId;
    m_cutRevision = document->revision().value;
    m_cutDocument = document;
    connect(document, &QObject::destroyed, this, [this] { refresh(); });
    connect(document, &PropertyDocument::changed, this, &ClipboardService::refresh);
    refresh();
    return {};
}

bool ClipboardService::copyObjectIds(PropertyDocument *document,
                                     const QString &collectionId,
                                     const QString &propertyId,
                                     const QStringList &objectIds)
{
    QVector<EntityId> ids;
    ids.reserve(objectIds.size());
    for (const QString &text : objectIds) {
        const EntityId id = EntityId::parse(text);
        if (!id.isValid() || id.toString() != text) {
            setError(fail(ErrorCode::InvalidArgument, QStringLiteral("Object IDs must be canonical UUIDs")));
            return false;
        }
        ids.append(id);
    }
    return !copyObjects(document, EntityId::parse(collectionId), propertyId, ids);
}

bool ClipboardService::cutObjectIds(PropertyDocument *document,
                                    const QString &collectionId,
                                    const QString &propertyId,
                                    const QStringList &objectIds)
{
    QVector<EntityId> ids;
    ids.reserve(objectIds.size());
    for (const QString &text : objectIds) {
        const EntityId id = EntityId::parse(text);
        if (!id.isValid() || id.toString() != text) {
            setError(fail(ErrorCode::InvalidArgument, QStringLiteral("Object IDs must be canonical UUIDs")));
            return false;
        }
        ids.append(id);
    }
    return !cutObjects(document, EntityId::parse(collectionId), propertyId, ids);
}

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
