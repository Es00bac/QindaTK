// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "rich_text_document.h"
#include <QPointer>

class QMimeData;

namespace QindaTK {
class ClipboardAdapter;

// Rich-text clipboard publishing and reading through the WP-016 transfer
// format: a TransferPayload with one Range item whose payload carries the
// qindatk.richtext.fragment JSON and its plain text. Unlike
// ClipboardService::copyPayload (text/plain only for one Text item), it also
// publishes text/plain and text/html so other applications can paste.
// AGENT-CONTRACT (docs/rich-text.md#clipboard): the internal MIME stays
// TransferPayloadCodec::InternalMime, so ClipboardService::canPaste sees it.
class RichTextClipboard {
public:
    struct Content {
        std::optional<RichText::Fragment> fragment; // internal or HTML content
        QString text;                               // plain text fallback
        bool isEmpty() const { return !fragment && text.isEmpty(); }
    };
    RichTextClipboard();
    ~RichTextClipboard();
    void setAdapter(ClipboardAdapter *adapter);      // host-owned; nullptr: system clipboard
    bool copy(const RichTextDocument &document, const RichText::Fragment &fragment,
              const QString &plainText);
    Content read() const;
    QString lastError() const { return m_lastError; }
private:
    const QMimeData *mimeData() const;
    bool setMimeData(QMimeData *data);
    QPointer<ClipboardAdapter> m_adapter;
    QString m_lastError;
};

} // namespace QindaTK
