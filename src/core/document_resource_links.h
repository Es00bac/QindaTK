// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "edit_transaction.h"
#include "resource_resolver.h"

#include <QPointer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// Joins a document's logical resource manifest to the runtime URI resolver.
// The document owns URI values and undo history; the manifest owns expected
// hashes. Consumer entities retain only the shared logical resource ID.
class DocumentResourceLinks : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QindaTK::PropertyDocument *document READ document WRITE setDocument NOTIFY changed)
    Q_PROPERTY(QindaTK::UndoService *undoService READ undoService WRITE setUndoService NOTIFY changed)
    Q_PROPERTY(QindaTK::ResourceResolver *resolver READ resolver WRITE setResolver NOTIFY changed)
    Q_PROPERTY(QVariantList manifest READ manifest WRITE setManifest NOTIFY changed)
    Q_PROPERTY(QString lastError READ lastError NOTIFY changed)
public:
    explicit DocumentResourceLinks(QObject *parent = nullptr);
    PropertyDocument *document() const { return m_document; }
    UndoService *undoService() const { return m_undo; }
    ResourceResolver *resolver() const { return m_resolver; }
    QVariantList manifest() const;
    QString lastError() const { return m_lastError; }

    void setDocument(PropertyDocument *document);
    void setUndoService(UndoService *undo);
    void setResolver(ResourceResolver *resolver);
    void setManifest(const QVariantList &manifest);

    // expectedRevision is the PropertyDocument revision captured by the UI.
    // A successful relink creates exactly one undo entry for the resource URI.
    Q_INVOKABLE bool relink(const QString &resourceId, const QString &newUri,
                            const QString &expectedRevision);
    Q_INVOKABLE QVariantMap resource(const QString &resourceId) const;

signals:
    void changed();
    void relinked(const QString &resourceId, const QString &uri);
    void rejected(const QString &message, int code);

private:
    struct Link {
        QString resourceId;
        EntityId entityId;
        QString uriPropertyId;
        QByteArray expectedSha256;
    };
    bool synchronizeResolver();
    bool synchronizeOne(const Link &link);
    void onPropertyChanged(const QString &entityId, const QString &propertyId);
    bool fail(ErrorCode code, const QString &message);
    const Link *find(const QString &resourceId) const;
    QPointer<PropertyDocument> m_document;
    QPointer<UndoService> m_undo;
    QPointer<ResourceResolver> m_resolver;
    QVector<Link> m_links;
    QString m_lastError;
};

} // namespace QindaTK
