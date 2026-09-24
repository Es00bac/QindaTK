// SPDX-License-Identifier: LGPL-3.0-or-later
#include "document_session.h"
#include "recovery_manager.h"
#include <QFileInfo>
#include <QThread>
#include <QtConcurrentRun>

namespace QindaTK {

DocumentSession::DocumentSession(std::unique_ptr<PropertyDocument> document,
                                 UndoService *undo, DocumentStorePtr store,
                                 QObject *parent)
    : DocumentSession(std::unique_ptr<DocumentModel>(document.release()), referenceDocumentCodec(),
                      undo, std::move(store), parent)
{
}

DocumentSession::DocumentSession(std::unique_ptr<DocumentModel> model, DocumentCodecPtr codec,
                                 UndoService *undo, DocumentStorePtr store, QObject *parent)
    : QObject(parent), m_model(model.release()), m_codec(std::move(codec)), m_undo(undo),
      m_store(std::move(store))
{
    Q_ASSERT(m_model && m_codec && m_undo && m_store);
    QObject *object = m_model->modelObject();
    Q_ASSERT(object->thread() == thread() && m_undo->thread() == thread());
    Q_ASSERT(m_model->modelHeader().schemaId == m_codec->schemaId());
    m_document = qobject_cast<PropertyDocument *>(object);
    m_documentId = m_model->modelId().toString();
    object->setParent(this);
    // AGENT-GUARD: one live history per model. A model that arrives with its
    // own history (a standalone canvas) moves it into the shared service.
    const bool attached = m_undo->adoptModel(m_model);
    Q_ASSERT(attached);
    Q_UNUSED(attached)
    m_model->connectModelChanged(this, [this] {
        emit changed();
        if (m_recovery && isDirty()) m_autosaveTimer.start();
    });
    connect(m_undo, &UndoService::changed, this, [this] { emit changed(); });
    connect(&m_fileWatcher, &QFileSystemWatcher::fileChanged, this, [this] {
        checkExternalChange();
        observePath();
    });
    m_autosaveTimer.setSingleShot(true);
    m_autosaveTimer.setInterval(2000);
    connect(&m_autosaveTimer, &QTimer::timeout, this, [this] { startBackgroundAutosave(); });
}

DocumentSession::~DocumentSession()
{
    m_saveCancellation.cancel();
    if (m_undo) m_undo->removeModel(m_model);
}

QString DocumentSession::title() const
{
    return m_uri.isEmpty() ? QStringLiteral("Untitled") : QFileInfo(m_uri).fileName();
}

QString DocumentSession::resetGeneration() const
{
    return QString::number(m_model->modelHeader().resetGeneration);
}

bool DocumentSession::isDirty() const
{
    return m_recovered || m_forceDirty || !m_hasSavedVersion || !m_undo
        || !m_undo->isModelClean(m_model);
}

void DocumentSession::fail(const QString &message, bool reportSave)
{
    m_lastError = message;
    emit changed();
    if (reportSave) emit saveFinished(false, message, {});
}

void DocumentSession::setExtensions(const QJsonObject &extensions)
{
    if (m_extensions == extensions) return;
    m_extensions = extensions;
    ++m_extensionGeneration;
    // Extension changes are document content even if no model edit exists.
    m_forceDirty = true;
    emit changed();
    if (m_recovery) m_autosaveTimer.start();
}

void DocumentSession::setReadOnly(bool readOnly)
{
    if (m_readOnly == readOnly) return;
    m_readOnly = readOnly;
    emit changed();
}

void DocumentSession::setRecoveryManager(RecoveryManager *manager)
{
    m_recovery = manager;
    if (manager && isDirty()) m_autosaveTimer.start();
}

void DocumentSession::setAutosaveDelayMs(int milliseconds)
{
    if (milliseconds < 100 || milliseconds > 600000
        || milliseconds == m_autosaveTimer.interval()) return;
    m_autosaveTimer.setInterval(milliseconds);
    emit changed();
}

void DocumentSession::initializeLoaded(const QString &path, const QByteArray &sha256,
                                       quint64 savedRevision,
                                       const QJsonObject &extensions, bool recovered)
{
    m_uri = path;
    m_savedHash = sha256;
    m_savedRevision = savedRevision;
    m_hasSavedVersion = !path.isEmpty() && sha256.size() == 32;
    m_extensions = extensions;
    m_extensionGeneration = 0;
    m_recovered = recovered;
    m_forceDirty = false;
    m_externalChanged = false;
    if (!recovered && m_hasSavedVersion) m_undo->markModelClean(m_model);
    observePath();
    emit changed();
}

void DocumentSession::observePath()
{
    const auto watched = m_fileWatcher.files();
    if (!watched.isEmpty()) m_fileWatcher.removePaths(watched);
    if (!m_uri.isEmpty() && QFileInfo::exists(m_uri)) m_fileWatcher.addPath(m_uri);
}

bool DocumentSession::checkExternalChange()
{
    if (m_uri.isEmpty() || !m_hasSavedVersion) return false;
    const auto receipt = m_store->load(m_uri);
    const bool changedOnDisk = !receipt || receipt.sha256 != m_savedHash;
    if (m_externalChanged != changedOnDisk) {
        m_externalChanged = changedOnDisk;
        emit changed();
    }
    return changedOnDisk;
}

DocumentCapturePtr DocumentSession::capture(OperationError *error) const
{
    return m_codec->capture(*m_model, m_extensions, error);
}

SnapshotPtr DocumentSession::captureSnapshot(OperationError *error) const
{
    const DocumentCapturePtr captured = capture(error);
    return captured ? encodeCapture(*m_codec, *captured, error) : SnapshotPtr{};
}

bool DocumentSession::requestSave()
{
    if (m_uri.isEmpty()) {
        fail(QStringLiteral("Choose a path with Save As"), false);
        return false;
    }
    return startSave(m_uri, m_hasSavedVersion ? FileExpectation::MatchingHash
                                              : FileExpectation::Missing,
                     m_savedHash);
}

bool DocumentSession::requestSaveAs(const QString &path, bool overwriteExisting)
{
    if (path.isEmpty()) {
        fail(QStringLiteral("Choose a Save As path"), false);
        return false;
    }
    const QString absolute = QFileInfo(path).absoluteFilePath();
    if (absolute == m_uri)
        return overwriteExisting ? startSave(absolute, FileExpectation::Any, {})
                                 : requestSave();
    return startSave(absolute, overwriteExisting ? FileExpectation::Any
                                                 : FileExpectation::Missing, {});
}

bool DocumentSession::startSave(const QString &path, FileExpectation expectation,
                                const QByteArray &expectedHash)
{
    Q_ASSERT(thread() == QThread::currentThread());
    if (m_saving || m_readOnly || !m_store) {
        fail(m_saving ? QStringLiteral("Save already in progress")
             : m_readOnly ? QStringLiteral("Document is read-only")
                          : QStringLiteral("Document store unavailable"), false);
        return false;
    }
    if (m_pathAvailable && !m_pathAvailable(path)) {
        fail(QStringLiteral("Another open document owns this path"), true);
        return false;
    }
    OperationError error;
    const DocumentCapturePtr captured = capture(&error);
    if (!captured) {
        fail(error.message, true);
        return false;
    }
    const SaveContext context{captured->header(), m_extensionGeneration,
                              m_undo ? m_undo->cleanMark(m_model) : UndoService::CleanMark{}};
    std::optional<RecoveryJournal> journal;
    quint64 epoch = 0;
    if (m_recovery) {
        journal = m_recovery->journal();
        epoch = journal->epoch(m_model->modelId());
    }
    m_saving = true;
    m_lastError.clear();
    m_saveCancellation = CancellationSource{};
    emit changed();
    auto *watcher = new QFutureWatcher<SaveOutcome>(this);
    m_saveWatcher = watcher;
    connect(watcher, &QFutureWatcher<SaveOutcome>::finished, this, [this, watcher, context] {
        const SaveOutcome outcome = watcher->result();
        m_saveWatcher = nullptr;
        watcher->deleteLater();
        finishSave(outcome, context);
    });
    // AGENT-CONTRACT (docs/persistence.md#snapshot-and-completion-rule): the
    // worker sees only the immutable capture, the store and a journal handle.
    // It encodes, appends the recovery frame, then replaces the file.
    watcher->setFuture(QtConcurrent::run([store = m_store, codec = m_codec, captured, journal,
                                          epoch, path, expectation, expectedHash,
                                          token = m_saveCancellation.token()] {
        SaveOutcome outcome;
        outcome.receipt.path = path;
        outcome.receipt.saved = captured->header();
        if (token.isCancelled()) {
            outcome.receipt.error = {ErrorCode::Cancelled, QStringLiteral("Save cancelled"), {}, {}};
            return outcome;
        }
        OperationError problem;
        const SnapshotPtr snapshot = encodeCapture(*codec, *captured, &problem);
        if (!snapshot) {
            outcome.receipt.error = problem;
            return outcome;
        }
        if (journal) {
            outcome.recoveryAttempted = true;
            outcome.recovery = journal->append(snapshot, epoch);
        }
        outcome.receipt = store->save({snapshot, path, expectation, expectedHash, token, codec});
        return outcome;
    }));
    return true;
}

void DocumentSession::finishSave(const SaveOutcome &outcome, const SaveContext &context)
{
    m_saving = false;
    if (outcome.recoveryAttempted && m_recovery) m_recovery->reportWrite(outcome.recovery);
    const SaveReceipt &receipt = outcome.receipt;
    if (receipt.error) {
        m_lastError = receipt.error.message;
        if (receipt.error.code == ErrorCode::Conflict) m_externalChanged = true;
        emit changed();
        emit saveFinished(false, m_lastError, receipt.saved.revision.toDecimal());
        return;
    }
    if (compareSnapshotHeaders(receipt.saved, context.captured)) {
        fail(QStringLiteral("Save receipt does not describe the captured document"), true);
        return;
    }
    m_uri = receipt.path;
    m_savedHash = receipt.sha256;
    m_savedRevision = receipt.saved.revision.value;
    m_hasSavedVersion = true;
    m_externalChanged = false;
    m_lastError.clear();
    // AGENT-GUARD: completion for an older immutable snapshot must never
    // clear newer model or extension edits; tests hold the worker here. A
    // late receipt marks clean exactly the history position it captured.
    const SnapshotHeader live = m_model->modelHeader();
    const bool modelCurrent = live.revision == context.captured.revision
        && live.resetGeneration == context.captured.resetGeneration;
    const bool extensionsCurrent = m_extensionGeneration == context.extensionGeneration;
    m_recovered = false;
    if (extensionsCurrent) m_forceDirty = false;
    if (m_undo) {
        if (modelCurrent) m_undo->markModelClean(m_model);
        else m_undo->markCleanAt(m_model, context.mark, context.captured.revision.value);
    }
    if (modelCurrent && extensionsCurrent && m_recovery) m_recovery->discard(m_model->modelId());
    observePath();
    emit changed();
    emit saveFinished(true, {}, receipt.saved.revision.toDecimal());
}

void DocumentSession::cancelSave()
{
    m_saveCancellation.cancel();
}

void DocumentSession::startBackgroundAutosave()
{
    Q_ASSERT(thread() == QThread::currentThread());
    if (!m_recovery || !isDirty()) return;
    if (m_autosaveWatcher) {
        m_autosavePending = true;
        return;
    }
    OperationError error;
    const DocumentCapturePtr captured = capture(&error);
    if (!captured) {
        fail(error.message, false);
        return;
    }
    const RecoveryJournal journal = m_recovery->journal();
    const quint64 epoch = journal.epoch(m_model->modelId());
    auto *watcher = new QFutureWatcher<OperationError>(this);
    m_autosaveWatcher = watcher;
    connect(watcher, &QFutureWatcher<OperationError>::finished, this, [this, watcher] {
        const OperationError result = watcher->result();
        m_autosaveWatcher = nullptr;
        watcher->deleteLater();
        if (m_recovery) m_recovery->reportWrite(result);
        if (m_autosavePending) {
            m_autosavePending = false;
            if (m_recovery && isDirty()) m_autosaveTimer.start();
        }
    });
    watcher->setFuture(QtConcurrent::run([codec = m_codec, captured, journal, epoch] {
        OperationError problem;
        const SnapshotPtr snapshot = encodeCapture(*codec, *captured, &problem);
        return snapshot ? journal.append(snapshot, epoch) : problem;
    }));
}

bool DocumentSession::autosaveNow()
{
    Q_ASSERT(thread() == QThread::currentThread());
    if (!m_recovery || !isDirty()) return false;
    OperationError error;
    const SnapshotPtr snapshot = captureSnapshot(&error);
    if (!snapshot) {
        fail(error.message, false);
        return false;
    }
    if (const auto result = m_recovery->record(snapshot)) {
        fail(result.message, false);
        return false;
    }
    return true;
}

} // namespace QindaTK
