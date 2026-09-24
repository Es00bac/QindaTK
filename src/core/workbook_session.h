// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "document_store.h"
#include "recovery_manager.h"
#include "workbook.h"
#include <QFutureWatcher>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// The DocumentStore for workbook snapshots (docs/persistence.md: a domain
// app brings its own store): validates the native payload against its
// header and writes it atomically with the SHA-256 baseline of FormatIO
// (AtomicDocumentStore validates reference documents only). Worker-safe.
class WorkbookDocumentStore final : public DocumentStore {
public:
    SaveReceipt save(const SaveRequest &request) override;
    LoadReceipt load(const QString &path) override;
};

// AGENT-CONTRACT (docs/workbook.md#sessions, docs/persistence.md): the
// document lifecycle of one Tk.Workbook with the shared persistence rules.
// Saves capture an immutable snapshot on the GUI thread and write it through
// a DocumentStore on a worker (SHA-256 baseline, atomic replacement); the
// workbook is marked saved only if its revision and reset generation still
// match the snapshot, so edits made during a save stay dirty. A failed or
// conflicting save keeps it dirty and blocks a close that asked to save.
// Autosave frames go to the shared RecoveryManager (validated workbook
// payloads); restoreRecovery reopens the newest valid revision as unsaved.
// History stays in the Workbook, an UndoParticipant for UndoService.
class WorkbookSession : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QindaTK::Workbook *workbook READ workbook WRITE setWorkbook NOTIFY workbookChanged)
    Q_PROPERTY(QindaTK::RecoveryManager *recovery READ recovery WRITE setRecovery NOTIFY changed)
    Q_PROPERTY(QUrl url READ url NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(bool dirty READ isDirty NOTIFY changed)
    Q_PROPERTY(bool saving READ isSaving NOTIFY changed)
    Q_PROPERTY(bool closing READ isClosing NOTIFY changed)
    Q_PROPERTY(QString lastError READ lastError NOTIFY changed)
    // Milliseconds between autosave frames while dirty; 0 turns autosave off.
    Q_PROPERTY(int autosaveInterval READ autosaveInterval WRITE setAutosaveInterval NOTIFY changed)
public:
    explicit WorkbookSession(QObject *parent = nullptr);
    ~WorkbookSession() override;

    Workbook *workbook() const { return m_workbook; }
    void setWorkbook(Workbook *workbook);
    RecoveryManager *recovery() const { return m_recovery; }
    void setRecovery(RecoveryManager *recovery);
    QUrl url() const { return m_url; }
    QString title() const;
    bool isDirty() const { return m_workbook && m_workbook->isModified(); }
    bool isSaving() const { return m_watcher != nullptr; }
    bool isClosing() const { return m_closing; }
    QString lastError() const { return m_lastError; }
    int autosaveInterval() const { return m_autosave.interval(); }
    void setAutosaveInterval(int milliseconds);
    // Tests inject stores with failures or barriers.
    void setStore(DocumentStorePtr store);

    // Reads a native workbook file (all-or-nothing) and makes it the baseline.
    Q_INVOKABLE bool open(const QUrl &url);
    Q_INVOKABLE bool requestSave();
    // A new destination; an existing file is replaced only when overwriteExisting.
    Q_INVOKABLE bool requestSaveAs(const QUrl &url, bool overwriteExisting = false);
    Q_INVOKABLE bool cancelSave();
    // Clean: closed() now. Dirty: closeRequested(); answer with respondToClose.
    Q_INVOKABLE bool requestClose();
    // "save": closes after a successful save only; "discard": closes and drops
    // recovery data; "cancel": keeps the document open.
    Q_INVOKABLE bool respondToClose(const QString &choice);
    Q_INVOKABLE bool recordRecovery();
    Q_INVOKABLE QVariantMap inspectRecovery() const;
    Q_INVOKABLE bool restoreRecovery();
    Q_INVOKABLE bool discardRecovery();

signals:
    void workbookChanged();
    void changed();
    void saveFinished(bool ok, const QString &error, const QString &revision);
    void closeRequested();
    void closeBlocked(const QString &reason);
    void closed();

private:
    bool startSave(const QString &path, FileExpectation expectation, const QByteArray &expectedSha256);
    void finishSave(const SnapshotHeader &captured, const SaveReceipt &receipt);
    bool fail(const QString &message);

    QPointer<Workbook> m_workbook;
    QPointer<RecoveryManager> m_recovery;
    DocumentStorePtr m_store;
    QUrl m_url;
    QByteArray m_sha256;
    QString m_lastError;
    QFutureWatcher<SaveReceipt> *m_watcher = nullptr;
    CancellationSource m_cancel;
    QTimer m_autosave;
    QList<QMetaObject::Connection> m_connections;
    bool m_closing = false;
};

} // namespace QindaTK
