// SPDX-License-Identifier: LGPL-3.0-or-later
#include "clipboard_service.h"

#include <QGuiApplication>
#include <QSet>
#include <QThread>
#include <QVariantList>
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

} // namespace QindaTK
