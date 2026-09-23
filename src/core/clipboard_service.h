// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "edit_transaction.h"
#include "transfer_payload.h"
#include <QClipboard>
#include <QMimeData>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QtQml/qqmlregistration.h>
#include <memory>

namespace QindaTK {

// Injectable GUI-thread seam. Implementations own data passed to setMimeData.
class ClipboardAdapter : public QObject {
    Q_OBJECT
public:
    explicit ClipboardAdapter(QObject *parent = nullptr) : QObject(parent) {}
    ~ClipboardAdapter() override = default;
    virtual const QMimeData *mimeData() const = 0;
    virtual bool setMimeData(QMimeData *data) = 0;
signals:
    void changed();
};

class ClipboardService : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QStringList availableFormats READ availableFormats NOTIFY changed)
    Q_PROPERTY(bool canPaste READ canPaste NOTIFY changed)
    Q_PROPERTY(bool cutPending READ cutPending NOTIFY changed)
    Q_PROPERTY(QString lastError READ lastError NOTIFY changed)
    Q_PROPERTY(QStringList lossWarnings READ lossWarnings NOTIFY changed)
public:
    explicit ClipboardService(QObject *parent = nullptr);
    ~ClipboardService() override;

    QStringList availableFormats() const { return m_formats; }
    bool canPaste() const { return m_canPaste; }
    bool cutPending() const
    { return !m_cutEnvelope.isEmpty() && m_cutDocument
        && m_cutDocument->revision().value == m_cutRevision; }
    QString lastError() const { return m_lastError; }
    QStringList lossWarnings() const { return m_lossWarnings; }
    void setAdapter(ClipboardAdapter *adapter);

    OperationError copyPayload(const TransferPayload &payload);
    OperationError copyObjects(PropertyDocument *document, EntityId collection,
                               const QString &propertyId,
                               const QVector<EntityId> &objectIds,
                               TransferOperation operation = TransferOperation::Copy);
    // Cut records a same-document pending Move. Source values remain intact
    // until paste succeeds; only one collection-property transaction is used.
    OperationError cutObjects(PropertyDocument *document, EntityId collection,
                              const QString &propertyId,
                              const QVector<EntityId> &objectIds);
    Q_INVOKABLE bool copyObjectIds(PropertyDocument *document,
                                   const QString &collectionId,
                                   const QString &propertyId,
                                   const QStringList &objectIds);
    Q_INVOKABLE bool cutObjectIds(PropertyDocument *document,
                                  const QString &collectionId,
                                  const QString &propertyId,
                                  const QStringList &objectIds);
    QVariantMap inspectPaste(PropertyDocument *document, EntityId target,
                             const QString &propertyId) const;
    OperationError paste(PropertyDocument *document, UndoService *undo,
                         EntityId target, const QString &propertyId,
                         qsizetype insertionIndex = -1);

    Q_INVOKABLE QVariantMap inspectPaste(PropertyDocument *document,
                                         const QString &targetId,
                                         const QString &propertyId) const
    { return inspectPaste(document, EntityId::parse(targetId), propertyId); }
    Q_INVOKABLE bool paste(PropertyDocument *document, UndoService *undo,
                           const QString &targetId, const QString &propertyId,
                           qlonglong insertionIndex = -1)
    { return !paste(document, undo, EntityId::parse(targetId), propertyId,
                    static_cast<qsizetype>(insertionIndex)); }
    Q_INVOKABLE bool copyText(const QString &text, const QString &documentId,
                              const QString &entityId, const QString &revision);
    Q_INVOKABLE bool copyImage(const QByteArray &png, const QString &documentId,
                               const QString &entityId, const QString &revision);

signals:
    void changed();
    void rejected(const QString &message, int code);

private:
    struct PreparedPaste {
        QVariant value;
        TransferPayload payload;
        QString format;
        QStringList warnings;
    };
    OperationError prepare(PropertyDocument *document, EntityId target,
                           const QString &propertyId, PreparedPaste *result,
                           qsizetype insertionIndex = -1) const;
    OperationError publishPayload(const TransferPayload &payload);
    void refresh();
    bool clearPendingCut();
    OperationError fail(ErrorCode code, const QString &message,
                        const QString &field = {}) const;
    void setError(const OperationError &error);

    std::unique_ptr<ClipboardAdapter> m_ownedAdapter;
    QPointer<ClipboardAdapter> m_adapter;
    QStringList m_formats;
    bool m_canPaste = false;
    QString m_lastError;
    QStringList m_lossWarnings;
    QString m_cutSourceDocument;
    QString m_cutCollection;
    QString m_cutProperty;
    quint64 m_cutRevision = 0;
    QPointer<PropertyDocument> m_cutDocument;
    QByteArray m_cutEnvelope;
};

} // namespace QindaTK
