// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "rich_text_document.h"
#include <QJsonArray>
#include <QPointer>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {
class DocumentSession;

// Persists a RichTextDocument inside a host DocumentSession: the rich text is
// one member of the session's extensions, so Save, Save As, autosave,
// recovery and stale-save protection are the session's own (WP-014/019).
// AGENT-CONTRACT (docs/rich-text.md#persistence): every committed change is
// written to the session synchronously, so a save requested at any moment
// captures it. The live encoding reuses per-paragraph JSON by revision and
// the document's format pool (valid qindatk.richtext/1, not canonical);
// RichTextCodec::encode is the canonical form for standalone files.
class RichTextSessionAdapter : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QindaTK::DocumentSession *session READ session WRITE setSession NOTIFY sessionChanged)
    Q_PROPERTY(QindaTK::RichTextDocument *document READ document WRITE setDocument NOTIFY documentChanged)
    Q_PROPERTY(QString extensionKey READ extensionKey WRITE setExtensionKey NOTIFY extensionKeyChanged)
    Q_PROPERTY(bool loaded READ loaded NOTIFY stateChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY stateChanged)
    Q_PROPERTY(QStringList warnings READ warnings NOTIFY stateChanged)
public:
    static QString defaultExtensionKey() { return QStringLiteral("org.qindatk.richtext"); }
    explicit RichTextSessionAdapter(QObject *parent = nullptr);
    DocumentSession *session() const { return m_session; }
    void setSession(DocumentSession *session);
    RichTextDocument *document() const { return m_document; }
    void setDocument(RichTextDocument *document);
    QString extensionKey() const { return m_key; }
    void setExtensionKey(const QString &key);
    bool loaded() const { return m_loaded; }
    QString lastError() const { return m_lastError; }
    QStringList warnings() const { return m_warnings; }
    QJsonObject liveJson();                // what flush() writes
    Q_INVOKABLE bool reload();             // session extension -> document
    Q_INVOKABLE bool flush();              // document -> session extension
signals:
    void sessionChanged();
    void documentChanged();
    void extensionKeyChanged();
    void stateChanged();
private:
    void attach();
    QJsonObject paragraphJson(const RichText::Paragraph &p);
    QJsonObject paragraphEntry(const RichText::Paragraph &p);
    QPointer<DocumentSession> m_session;
    QPointer<RichTextDocument> m_document;
    QString m_key = defaultExtensionKey();
    QHash<QUuid, QPair<quint64, QJsonObject>> m_paragraphs;
    QHash<QUuid, QPair<quint64, QJsonObject>> m_tables;
    QJsonArray m_formats;
    quint64 m_formatsGeneration = ~0ull;
    bool m_loaded = false;
    bool m_loading = false;
    QString m_lastError;
    QStringList m_warnings;
};

} // namespace QindaTK
