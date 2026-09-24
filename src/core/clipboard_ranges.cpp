// SPDX-License-Identifier: LGPL-3.0-or-later
#include "clipboard_service.h"

#include <QThread>

// Range items (docs/clipboard.md#ranges): a Range payload with a plain-text
// projection for other applications, and a reader for range-aware pastes.
namespace QindaTK {

bool ClipboardService::copyRange(const QString &documentId, const QString &revision,
                                 const QVariantMap &range, const QString &text)
{
    Q_ASSERT(thread() == QThread::currentThread());
    bool revisionOk = false;
    const quint64 revisionValue = revision.toULongLong(&revisionOk, 10);
    const DocumentId doc = DocumentId::parse(documentId);
    const QByteArray plain = text.toUtf8();
    if (!doc.isValid() || !revisionOk || QString::number(revisionValue) != revision || range.isEmpty()) {
        setError(fail(ErrorCode::InvalidArgument, QStringLiteral("Copy requires a canonical document ID, revision and range")));
        return false;
    }
    if (plain.size() > TransferPayloadCodec::MaximumTextBytes) {
        setError(fail(ErrorCode::Validation, QStringLiteral("The range text exceeds the 4 MiB limit")));
        return false;
    }
    if (!m_adapter) {
        setError(fail(ErrorCode::Unavailable, QStringLiteral("Clipboard adapter is unavailable")));
        return false;
    }
    // AGENT-NOTE: a range has no entity of its own; each copy gets a fresh ID.
    const TransferPayload payload{doc, RevisionId{revisionValue}, TransferOperation::Copy,
        {{EntityId::create(), TransferItemKind::Range, QJsonObject::fromVariantMap(range)}}};
    OperationError failure;
    const auto alternatives = TransferPayloadCodec::mimeAlternatives(payload, &failure);
    if (alternatives.isEmpty()) {
        setError(failure);
        return false;
    }
    // AGENT-CONTRACT: the internal MIME carries the typed range; text/plain is
    // the projection other applications paste (publishPayload offers text only
    // for single text items).
    auto *mime = new QMimeData;
    for (auto it = alternatives.cbegin(); it != alternatives.cend(); ++it) mime->setData(it.key(), it.value());
    mime->setData(QStringLiteral("text/plain"), plain);
    if (!m_adapter->setMimeData(mime)) {
        setError(fail(ErrorCode::Unavailable, QStringLiteral("Clipboard adapter rejected the payload")));
        return false;
    }
    m_lastError.clear();
    m_lossWarnings.clear();
    refresh();
    setError({});
    return true;
}

QVariantMap ClipboardService::rangePasteData() const
{
    const QMimeData *mime = m_adapter ? m_adapter->mimeData() : nullptr;
    if (!mime) return {{"kind", QString()}};
    const QString internal = QString::fromLatin1(TransferPayloadCodec::InternalMime);
    if (mime->hasFormat(internal)) {
        const auto payload = TransferPayloadCodec::decode(mime->data(internal));
        if (payload && payload->operation == TransferOperation::Copy && payload->items.size() == 1
            && payload->items.front().kind == TransferItemKind::Range)
            return {{"kind", QStringLiteral("range")}, {"range", payload->items.front().payload.toVariantMap()},
                    {"sourceDocumentId", payload->sourceDocumentId.toString()}, {"text", mime->text()}};
    }
    if (mime->hasText()) return {{"kind", QStringLiteral("text")}, {"text", mime->text()}};
    return {{"kind", QString()}};
}

} // namespace QindaTK
