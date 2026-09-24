// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "recovery_journal.h"
#include "reference_document_codec.h"
#include <QObject>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// Autosave bytes are stored separately from the user's document file. A
// journal scan accepts only complete, hashed, identity-checked frames; any
// registered DocumentCodec's schema can be recorded and restored. Inspecting
// or restoring never modifies recovery files.
class RecoveryManager : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString directory READ directory WRITE setDirectory NOTIFY changed)
    Q_PROPERTY(int maxJournalEntries READ maxJournalEntries WRITE setMaxJournalEntries NOTIFY changed)
    Q_PROPERTY(QString maxJournalBytes READ maxJournalBytes WRITE setMaxJournalBytes NOTIFY changed)
    Q_PROPERTY(QString lastError READ lastError NOTIFY changed)
public:
    struct Restored {
        DecodedDocumentPtr decoded;
        DocumentCodecPtr codec;
        SnapshotPtr snapshot;
    };
    explicit RecoveryManager(QObject *parent = nullptr);
    QString directory() const { return m_directory; }
    void setDirectory(const QString &path);
    int maxJournalEntries() const { return m_maxEntries; }
    void setMaxJournalEntries(int count);
    QString maxJournalBytes() const { return QString::number(m_maxJournalBytes); }
    void setMaxJournalBytes(const QString &bytes);
    QString lastError() const { return m_lastError; }
    std::shared_ptr<DocumentCodecRegistry> codecRegistry() const { return m_codecs; }
    void setCodecRegistry(std::shared_ptr<DocumentCodecRegistry> registry);
    // Thread-safe handle on the current directory and limits for workers.
    RecoveryJournal journal() const;
    OperationError record(SnapshotPtr snapshot);
    std::optional<RecoverySnapshot> inspect(DocumentId id) const;
    // Reference documents only (source compatible); restoreDocument is generic.
    std::optional<DecodedReferenceDocument> restore(DocumentId id,
                                                     OperationError *error = nullptr) const;
    std::optional<Restored> restoreDocument(DocumentId id, OperationError *error = nullptr) const;
    bool discard(DocumentId id);
    // Reports the result of a journal append made by a worker.
    void reportWrite(const OperationError &error);
    Q_INVOKABLE QVariantMap inspectId(const QString &documentId) const;
    Q_INVOKABLE bool discardId(const QString &documentId);
signals:
    void changed();
private:
    QString m_directory;
    QString m_lastError;
    int m_maxEntries = 32;
    qsizetype m_maxJournalBytes = 64 * 1024 * 1024;
    std::shared_ptr<DocumentCodecRegistry> m_codecs = DocumentCodecRegistry::shared();
    std::shared_ptr<RecoveryJournal::Shared> m_shared = RecoveryJournal::createShared();
};

} // namespace QindaTK
