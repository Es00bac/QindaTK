// SPDX-License-Identifier: LGPL-3.0-or-later
#include "document_session.h"
#include "recovery_manager.h"
#include "reference_document_codec.h"
#include <QFileInfo>
#include <QThread>
#include <QtConcurrentRun>

namespace QindaTK {

DocumentSession::DocumentSession(std::unique_ptr<PropertyDocument> document,
                                 UndoService *undo, DocumentStorePtr store,
                                 QObject *parent)
    : QObject(parent), m_document(document.release()), m_undo(undo),
      m_store(std::move(store))
{
    Q_ASSERT(m_document && m_undo && m_store);
    Q_ASSERT(m_document->thread() == thread() && m_undo->thread() == thread());
    m_document->setParent(this);
    m_undo->addDocument(m_document);
    connect(m_document, &PropertyDocument::changed, this, [this] {
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
    connect(&m_autosaveTimer, &QTimer::timeout, this, [this] { autosaveNow(); });
}

DocumentSession::~DocumentSession()
{
    m_saveCancellation.cancel();
    if (m_undo) m_undo->removeDocument(m_document);
}

QString DocumentSession::title() const
{
    return m_uri.isEmpty() ? QStringLiteral("Untitled") : QFileInfo(m_uri).fileName();
}

bool DocumentSession::isDirty() const
{
    return m_recovered || m_forceDirty || !m_hasSavedVersion || !m_undo
        || !m_undo->isClean(m_document);
}

void DocumentSession::setExtensions(const QJsonObject &extensions)
{
    if (m_extensions == extensions) return;
    m_extensions = extensions;
    ++m_extensionGeneration;
    // Extension changes are document content even if no property edit exists.
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
    if (!recovered && m_hasSavedVersion) m_undo->markClean(m_document);
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

bool DocumentSession::requestSave()
{
    if (m_uri.isEmpty()) {
        m_lastError = QStringLiteral("Choose a path with Save As");
        emit changed();
        return false;
    }
    return startSave(m_uri, m_hasSavedVersion ? FileExpectation::MatchingHash
                                              : FileExpectation::Missing,
                     m_savedHash);
}

bool DocumentSession::requestSaveAs(const QString &path, bool overwriteExisting)
{
    if (path.isEmpty()) {
        m_lastError = QStringLiteral("Choose a Save As path");
        emit changed();
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
        m_lastError = m_saving ? QStringLiteral("Save already in progress")
                    : m_readOnly ? QStringLiteral("Document is read-only")
                                 : QStringLiteral("Document store unavailable");
        emit changed();
        return false;
    }
    if (m_pathAvailable && !m_pathAvailable(path)) {
        m_lastError = QStringLiteral("Another open document owns this path");
        emit changed();
        emit saveFinished(false, m_lastError, {});
        return false;
    }
    OperationError error;
    const SnapshotPtr snapshot = ReferenceDocumentCodec::capture(*m_document,
                                                                  m_extensions, &error);
    if (!snapshot) {
        m_lastError = error.message;
        emit changed();
        emit saveFinished(false, m_lastError, {});
        return false;
    }
    if (m_recovery) m_recovery->record(snapshot);
    m_saving = true;
    m_lastError.clear();
    m_saveCancellation = CancellationSource{};
    emit changed();
    SaveRequest request{snapshot, path, expectation, expectedHash,
                        m_saveCancellation.token()};
    const quint64 capturedExtensions = m_extensionGeneration;
    auto *watcher = new QFutureWatcher<SaveReceipt>(this);
    m_saveWatcher = watcher;
    connect(watcher, &QFutureWatcher<SaveReceipt>::finished, this,
            [this, watcher, capturedExtensions] {
        const SaveReceipt receipt = watcher->result();
        m_saveWatcher = nullptr;
        watcher->deleteLater();
        m_saving = false;
        if (receipt.error) {
            m_lastError = receipt.error.message;
            if (receipt.error.code == ErrorCode::Conflict) m_externalChanged = true;
            emit changed();
            emit saveFinished(false, m_lastError, receipt.saved.revision.toDecimal());
            return;
        }
        m_uri = receipt.path;
        m_savedHash = receipt.sha256;
        m_savedRevision = receipt.saved.revision.value;
        m_hasSavedVersion = true;
        m_externalChanged = false;
        m_lastError.clear();
        // AGENT-GUARD: completion for an older immutable snapshot must never
        // clear newer property or extension edits; tests hold the worker here.
        if (m_document->revision() == receipt.saved.revision
            && m_extensionGeneration == capturedExtensions) {
            m_recovered = false;
            m_forceDirty = false;
            if (m_undo) m_undo->markClean(m_document);
            if (m_recovery) m_recovery->discard(m_document->documentId());
        }
        observePath();
        emit changed();
        emit saveFinished(true, {}, receipt.saved.revision.toDecimal());
    });
    watcher->setFuture(QtConcurrent::run([store = m_store, request] {
        return store->save(request);
    }));
    return true;
}

void DocumentSession::cancelSave()
{
    m_saveCancellation.cancel();
}

bool DocumentSession::autosaveNow()
{
    Q_ASSERT(thread() == QThread::currentThread());
    if (!m_recovery || !isDirty()) return false;
    OperationError error;
    const auto snapshot = ReferenceDocumentCodec::capture(*m_document,
                                                           m_extensions, &error);
    if (!snapshot) {
        m_lastError = error.message;
        emit changed();
        return false;
    }
    if (const auto result = m_recovery->record(snapshot); result) {
        m_lastError = result.message;
        emit changed();
        return false;
    }
    return true;
}

} // namespace QindaTK
