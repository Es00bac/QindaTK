// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "reference_document_codec.h"
#include <QObject>
#include <functional>
#include <vector>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

struct RecoverySnapshot {
    SnapshotPtr snapshot;
    bool truncatedTail = false;
    int validEntries = 0;
};

// Autosave bytes are stored separately from the user's document file. A
// journal scan accepts only complete, hashed, schema-valid frames.
class RecoveryManager : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString directory READ directory WRITE setDirectory NOTIFY changed)
    Q_PROPERTY(int maxJournalEntries READ maxJournalEntries WRITE setMaxJournalEntries NOTIFY changed)
    Q_PROPERTY(QString maxJournalBytes READ maxJournalBytes WRITE setMaxJournalBytes NOTIFY changed)
    Q_PROPERTY(QString lastError READ lastError NOTIFY changed)
public:
    explicit RecoveryManager(QObject *parent = nullptr);
    QString directory() const { return m_directory; }
    void setDirectory(const QString &path);
    int maxJournalEntries() const { return m_maxEntries; }
    void setMaxJournalEntries(int count);
    QString maxJournalBytes() const { return QString::number(m_maxJournalBytes); }
    void setMaxJournalBytes(const QString &bytes);
    QString lastError() const { return m_lastError; }
    OperationError record(SnapshotPtr snapshot);
    std::optional<RecoverySnapshot> inspect(DocumentId id) const;
    std::optional<DecodedReferenceDocument> restore(DocumentId id,
                                                     OperationError *error = nullptr) const;
    bool discard(DocumentId id);
    // Payloads of a domain serializer (docs/persistence.md#domain-recovery):
    // for a valid payload a validator fills header.schemaId and documentId
    // (and revision/resetGeneration when its payload records them; frames
    // supply the revision otherwise). Reference documents need none; a domain
    // app reads its recovered bytes with inspect() instead of restore().
    using PayloadValidator = std::function<OperationError(const QByteArray &payload, SnapshotHeader *header)>;
    void addPayloadValidator(PayloadValidator validator);
    Q_INVOKABLE QVariantMap inspectId(const QString &documentId) const;
    Q_INVOKABLE bool discardId(const QString &documentId);
signals:
    void changed();
private:
    QString path(DocumentId id, const QString &suffix) const;
    bool readHeader(const QByteArray &payload, SnapshotHeader *header) const;
    std::vector<PayloadValidator> m_validators;
    OperationError checkpoint(DocumentId id, SnapshotPtr snapshot);
    QString m_directory;
    QString m_lastError;
    int m_maxEntries = 32;
    qsizetype m_maxJournalBytes = 64 * 1024 * 1024;
};

} // namespace QindaTK
