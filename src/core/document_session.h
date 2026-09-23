// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "document_store.h"
#include "property_document.h"
#include "undo_service.h"
#include <QFileSystemWatcher>
#include <QFutureWatcher>
#include <QJsonObject>
#include <QPointer>
#include <QTimer>
#include <QtQml/qqmlregistration.h>
#include <functional>

namespace QindaTK {
class DocumentManager;
class RecoveryManager;

// GUI-thread lifecycle facade for a single authoritative reference document.
// Save inputs are immutable; only the file adapter runs on a worker thread.
class DocumentSession final : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(DocumentSession)
    QML_UNCREATABLE("Create through DocumentManager")
    Q_PROPERTY(QindaTK::PropertyDocument *document READ document CONSTANT)
    Q_PROPERTY(QindaTK::UndoService *undoService READ undoService CONSTANT)
    Q_PROPERTY(QString documentId READ documentId CONSTANT)
    Q_PROPERTY(QString uri READ uri NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QString revision READ revision NOTIFY changed)
    Q_PROPERTY(QString savedRevision READ savedRevision NOTIFY changed)
    Q_PROPERTY(bool dirty READ isDirty NOTIFY changed)
    Q_PROPERTY(bool loading READ isLoading NOTIFY changed)
    Q_PROPERTY(bool saving READ isSaving NOTIFY changed)
    Q_PROPERTY(bool readOnly READ isReadOnly NOTIFY changed)
    Q_PROPERTY(bool externalChanged READ externalChanged NOTIFY changed)
    Q_PROPERTY(bool recovered READ recovered NOTIFY changed)
    Q_PROPERTY(QString lastError READ lastError NOTIFY changed)
    Q_PROPERTY(int autosaveDelayMs READ autosaveDelayMs WRITE setAutosaveDelayMs NOTIFY changed)
public:
    DocumentSession(std::unique_ptr<PropertyDocument> document,
                    UndoService *undo, DocumentStorePtr store,
                    QObject *parent = nullptr);
    ~DocumentSession() override;
    PropertyDocument *document() const { return m_document; }
    UndoService *undoService() const { return m_undo; }
    QString documentId() const { return m_document->documentIdString(); }
    QString uri() const { return m_uri; }
    QString title() const;
    QString revision() const { return m_document->revisionString(); }
    QString savedRevision() const { return QString::number(m_savedRevision); }
    bool isDirty() const;
    bool isLoading() const { return false; }
    bool isSaving() const { return m_saving; }
    bool isReadOnly() const { return m_readOnly; }
    bool externalChanged() const { return m_externalChanged; }
    bool recovered() const { return m_recovered; }
    QString lastError() const { return m_lastError; }
    int autosaveDelayMs() const { return m_autosaveTimer.interval(); }
    void setAutosaveDelayMs(int milliseconds);
    const QJsonObject &extensions() const { return m_extensions; }
    void setExtensions(const QJsonObject &extensions);
    void setReadOnly(bool readOnly);
    void setRecoveryManager(RecoveryManager *manager);
    Q_INVOKABLE bool requestSave();
    Q_INVOKABLE bool requestSaveAs(const QString &path, bool overwriteExisting = false);
    Q_INVOKABLE void cancelSave();
    Q_INVOKABLE bool checkExternalChange();
    Q_INVOKABLE bool autosaveNow();
signals:
    void changed();
    void saveFinished(bool success, const QString &message, const QString &savedRevision);
private:
    friend class DocumentManager;
    void initializeLoaded(const QString &path, const QByteArray &sha256,
                          quint64 savedRevision, const QJsonObject &extensions,
                          bool recovered);
    bool startSave(const QString &path, FileExpectation expectation,
                   const QByteArray &expectedHash);
    void observePath();
    std::function<bool(const QString &)> m_pathAvailable;
    PropertyDocument *m_document = nullptr;
    QPointer<UndoService> m_undo;
    DocumentStorePtr m_store;
    QPointer<RecoveryManager> m_recovery;
    QFutureWatcher<SaveReceipt> *m_saveWatcher = nullptr;
    QFileSystemWatcher m_fileWatcher;
    QTimer m_autosaveTimer;
    CancellationSource m_saveCancellation;
    QJsonObject m_extensions;
    QString m_uri;
    QString m_lastError;
    QByteArray m_savedHash;
    quint64 m_savedRevision = 0;
    quint64 m_extensionGeneration = 0;
    bool m_hasSavedVersion = false;
    bool m_saving = false;
    bool m_readOnly = false;
    bool m_externalChanged = false;
    bool m_recovered = false;
    bool m_forceDirty = false;
};

} // namespace QindaTK
