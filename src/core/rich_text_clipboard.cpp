// SPDX-License-Identifier: LGPL-3.0-or-later
#include "rich_text_clipboard.h"
#include "clipboard_service.h"
#include "rich_text_codec.h"
#include "rich_text_html.h"
#include "transfer_payload.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QMimeData>

namespace QindaTK {
using namespace RichText;

RichTextClipboard::RichTextClipboard() = default;
RichTextClipboard::~RichTextClipboard() = default;

void RichTextClipboard::setAdapter(ClipboardAdapter *adapter)
{
    m_adapter = adapter;
}

const QMimeData *RichTextClipboard::mimeData() const
{
    if (m_adapter) return m_adapter->mimeData();
    QClipboard *clipboard = QGuiApplication::clipboard();
    return clipboard ? clipboard->mimeData(QClipboard::Clipboard) : nullptr;
}

bool RichTextClipboard::setMimeData(QMimeData *data)
{
    if (m_adapter) return m_adapter->setMimeData(data);
    QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard) {
        delete data;
        return false;
    }
    clipboard->setMimeData(data, QClipboard::Clipboard);
    return true;
}

bool RichTextClipboard::copy(const RichTextDocument &document, const Fragment &fragment,
                             const QString &plainText)
{
    m_lastError.clear();
    if (fragment.blocks.isEmpty()) {
        m_lastError = QStringLiteral("Nothing to copy");
        return false;
    }
    TransferItem item;
    item.entityId = {fragment.blocks.first().id()};
    item.kind = TransferItemKind::Range;
    item.payload = {{QStringLiteral("schema"), RichTextCodec::fragmentSchemaId()},
                    {QStringLiteral("fragment"), RichTextCodec::fragmentToJson(fragment)},
                    {QStringLiteral("text"), plainText}};
    TransferPayload payload;
    payload.sourceDocumentId = document.documentId();
    payload.sourceRevision = {document.revision()};
    payload.operation = TransferOperation::Copy;
    payload.items = {item};
    OperationError error;
    const QByteArray internal = TransferPayloadCodec::encode(payload, &error);
    auto *mime = new QMimeData;
    // A range too large for the transfer limit still copies as text/HTML.
    if (!internal.isEmpty()) mime->setData(QString::fromLatin1(TransferPayloadCodec::InternalMime), internal);
    mime->setText(plainText);
    mime->setHtml(RichTextHtml::fromFragment(fragment));
    if (!setMimeData(mime)) {
        m_lastError = QStringLiteral("Clipboard unavailable");
        return false;
    }
    return true;
}

RichTextClipboard::Content RichTextClipboard::read() const
{
    Content content;
    const QMimeData *mime = mimeData();
    if (!mime) return content;
    const QString internalMime = QString::fromLatin1(TransferPayloadCodec::InternalMime);
    if (mime->hasFormat(internalMime)) {
        if (const auto payload = TransferPayloadCodec::decode(mime->data(internalMime))) {
            for (const TransferItem &item : payload->items) {
                if (item.kind == TransferItemKind::Range
                    && item.payload.value(QStringLiteral("schema")).toString() == RichTextCodec::fragmentSchemaId()) {
                    content.fragment = RichTextCodec::fragmentFromJson(item.payload.value(QStringLiteral("fragment")).toObject());
                    content.text = item.payload.value(QStringLiteral("text")).toString();
                    if (content.fragment) return content;
                }
                if (item.kind == TransferItemKind::Text)
                    content.text = item.payload.value(QStringLiteral("text")).toString();
            }
        }
    }
    if (mime->hasHtml()) {
        Fragment fragment = RichTextHtml::toFragment(mime->html());
        if (!fragment.blocks.isEmpty()) content.fragment = std::move(fragment);
    }
    if (content.text.isEmpty() && mime->hasText()) content.text = mime->text();
    return content;
}

} // namespace QindaTK
