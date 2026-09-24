// SPDX-License-Identifier: LGPL-3.0-or-later
#include "workbook_session.h"
#include "workbook_codec_p.h"
#include "workbook_format.h"

#include "format_io.h"

#include <QFile>
#include <QFileInfo>
#include <QtConcurrentRun>

namespace QindaTK {

SaveReceipt WorkbookDocumentStore::save(const SaveRequest &request)
{
    SaveReceipt receipt;
    receipt.path = request.path;
    if (request.snapshot) receipt.saved = request.snapshot->header;
    const auto invalid = [&](const QString &message) {
        receipt.error = {ErrorCode::InvalidArgument, message, request.path, {}};
        return receipt;
    };
    if (!request.snapshot || request.snapshot->header.schemaId != QLatin1StringView(WorkbookFormat::FormatId)
        || request.snapshot->payload.isEmpty() || request.snapshot->payload.size() > Workbook::MaxFileBytes
        || request.path.isEmpty() || (request.expectation == FileExpectation::MatchingHash
                                      && request.expectedSha256.size() != 32))
        return invalid(QStringLiteral("Invalid save request"));
    QString error;
    const auto decoded = Calc::decodeWorkbook(request.snapshot->payload, &error);
    if (!decoded || DocumentId::parse(decoded->documentId) != receipt.saved.documentId)
        return invalid(QStringLiteral("Snapshot header or payload is invalid"));
    QVariantMap options; // FileExpectation::Missing: an existing file is a conflict
    QByteArray baseline = request.expectedSha256;
    if (request.expectation == FileExpectation::Any && QFileInfo::exists(request.path)) {
        QFile current(request.path);
        if (!current.open(QIODevice::ReadOnly)) {
            receipt.error = {ErrorCode::BackendFailure, current.errorString(), request.path, {}};
            return receipt;
        }
        baseline = FormatIO::hash(current.readAll());
    }
    if (!baseline.isEmpty())
        options = {{"conflictPolicy", QStringLiteral("replaceIfHash")}, {"expectedSha256", QString::fromLatin1(baseline.toHex())}};
    QByteArray sha256;
    if (const OperationError failure = FormatIO::writeAtomic(request.path, request.snapshot->payload, options,
                                                             request.cancellation, [](double) {}, &sha256))
        receipt.error = failure;
    else
        receipt.sha256 = sha256;
    return receipt;
}
LoadReceipt WorkbookDocumentStore::load(const QString &path) { return AtomicDocumentStore().load(path); }

WorkbookSession::WorkbookSession(QObject *parent) : QObject(parent), m_store(std::make_shared<WorkbookDocumentStore>())
{
    m_autosave.setSingleShot(true);
    m_autosave.setInterval(0);
    connect(&m_autosave, &QTimer::timeout, this, [this]() { recordRecovery(); });
}
WorkbookSession::~WorkbookSession()
{
    // A running save keeps its own copies; its completion must not reach us.
    if (m_watcher) {
        m_cancel.cancel();
        m_watcher->disconnect(this);
    }
}

void WorkbookSession::setWorkbook(Workbook *workbook)
{
    if (m_workbook == workbook) return;
    for (const auto &connection : std::as_const(m_connections)) disconnect(connection);
    m_connections.clear();
    m_workbook = workbook;
    if (m_workbook) {
        m_connections << connect(m_workbook, &Workbook::historyChanged, this, [this]() {
            if (m_recovery && isDirty() && m_autosave.interval() > 0 && !m_autosave.isActive()) m_autosave.start();
            emit changed();
        }) << connect(m_workbook, &Workbook::resetOccurred, this, &WorkbookSession::changed);
    }
    emit workbookChanged();
    emit changed();
}

void WorkbookSession::setRecovery(RecoveryManager *recovery)
{
    if (m_recovery == recovery) return;
    m_recovery = recovery;
    if (m_recovery && !m_recovery->property("qindatkWorkbookValidator").toBool()) {
        // Workbook payloads are native files: valid when they decode completely.
        m_recovery->addPayloadValidator([](const QByteArray &payload, SnapshotHeader *header) -> OperationError {
            QString error;
            const auto decoded = Calc::decodeWorkbook(payload, &error);
            if (!decoded) return {ErrorCode::Validation, error, {}, QStringLiteral("recovery")};
            header->schemaId = QString::fromLatin1(WorkbookFormat::FormatId);
            header->documentId = DocumentId::parse(decoded->documentId);
            return {};
        });
        m_recovery->setProperty("qindatkWorkbookValidator", true);
    }
    emit changed();
}

void WorkbookSession::setAutosaveInterval(int milliseconds)
{
    milliseconds = std::max(0, milliseconds);
    if (m_autosave.interval() == milliseconds) return;
    m_autosave.setInterval(milliseconds);
    if (milliseconds == 0) m_autosave.stop();
    emit changed();
}
void WorkbookSession::setStore(DocumentStorePtr store) { if (store) m_store = std::move(store); }

QString WorkbookSession::title() const
{
    return m_url.isEmpty() ? tr("Untitled workbook") : QFileInfo(m_url.toLocalFile()).fileName();
}
bool WorkbookSession::fail(const QString &message)
{
    m_lastError = message;
    emit changed();
    return false;
}

bool WorkbookSession::open(const QUrl &url)
{
    if (!m_workbook) return fail(tr("No workbook"));
    if (isSaving()) return fail(tr("Wait for the save to finish"));
    if (!url.isLocalFile()) return fail(tr("Only local files can be opened"));
    const LoadReceipt loaded = m_store->load(url.toLocalFile());
    if (!loaded) return fail(loaded.error.message);
    if (!m_workbook->restore(loaded.bytes)) return fail(m_workbook->lastError());
    m_url = url;
    m_sha256 = loaded.sha256;
    m_lastError.clear();
    emit changed();
    return true;
}

bool WorkbookSession::requestSave()
{
    if (m_url.isEmpty()) return fail(tr("Choose where to save first (Save As)"));
    return startSave(m_url.toLocalFile(), m_sha256.isEmpty() ? FileExpectation::Missing : FileExpectation::MatchingHash,
                     m_sha256);
}

bool WorkbookSession::requestSaveAs(const QUrl &url, bool overwriteExisting)
{
    if (!url.isLocalFile()) return fail(tr("Only local files can be saved"));
    if (url == m_url && !m_sha256.isEmpty()) return requestSave();
    return startSave(url.toLocalFile(), overwriteExisting ? FileExpectation::Any : FileExpectation::Missing, {});
}

bool WorkbookSession::startSave(const QString &path, FileExpectation expectation, const QByteArray &expectedSha256)
{
    if (!m_workbook) return fail(tr("No workbook"));
    if (isSaving()) return fail(tr("A save is already running"));
    if (m_workbook->transactionActive()) return fail(tr("Finish the active workbook transaction first"));
    // AGENT-GUARD: capture on the GUI thread; the worker gets only copies.
    const SnapshotPtr snapshot = m_workbook->snapshot();
    if (snapshot->payload.isEmpty()) return fail(tr("The workbook could not be captured"));
    m_cancel = CancellationSource();
    const SaveRequest request{snapshot, path, expectation, expectedSha256, m_cancel.token()};
    const SnapshotHeader captured = snapshot->header;
    if (m_recovery) m_recovery->record(snapshot); // the state being saved is recoverable until it lands
    m_watcher = new QFutureWatcher<SaveReceipt>(this);
    connect(m_watcher, &QFutureWatcherBase::finished, this, [this, captured]() {
        const SaveReceipt receipt = m_watcher->result();
        m_watcher->deleteLater();
        m_watcher = nullptr;
        finishSave(captured, receipt);
    });
    m_watcher->setFuture(QtConcurrent::run([store = m_store, request] { return store->save(request); }));
    m_lastError.clear();
    emit changed();
    return true;
}

void WorkbookSession::finishSave(const SnapshotHeader &captured, const SaveReceipt &receipt)
{
    if (!receipt) {
        m_lastError = receipt.error.message;
        emit changed();
        emit saveFinished(false, m_lastError, captured.revision.toDecimal());
        if (m_closing) {
            m_closing = false;
            emit closeBlocked(m_lastError);
            emit changed();
        }
        return;
    }
    m_url = QUrl::fromLocalFile(receipt.path);
    m_sha256 = receipt.sha256;
    // Only the captured state is saved: later edits (or a reset) stay dirty.
    const bool current = m_workbook && m_workbook->revision() == captured.revision.toDecimal()
                         && m_workbook->resetGeneration() == QString::number(captured.resetGeneration);
    if (current) {
        m_workbook->markSaved();
        if (m_recovery) m_recovery->discard(captured.documentId);
    }
    m_lastError.clear();
    emit changed();
    emit saveFinished(true, {}, receipt.saved.revision.toDecimal());
    if (m_closing) {
        m_closing = false;
        if (isDirty()) emit closeBlocked(tr("The workbook changed while it was being saved"));
        else emit closed();
        emit changed();
    }
}

bool WorkbookSession::cancelSave()
{
    if (!isSaving()) return false;
    m_cancel.cancel();
    return true;
}

bool WorkbookSession::requestClose()
{
    if (isSaving()) return fail(tr("Wait for the save to finish"));
    if (!isDirty()) {
        emit closed();
        return true;
    }
    emit closeRequested();
    return false;
}

bool WorkbookSession::respondToClose(const QString &choice)
{
    if (choice == u"cancel") return false;
    if (choice == u"discard") {
        discardRecovery();
        emit closed();
        return true;
    }
    if (choice != u"save") return fail(tr("Answer save, discard or cancel"));
    m_closing = true;
    const bool started = m_url.isEmpty() ? fail(tr("Choose where to save first (Save As)")) : requestSave();
    if (!started) {
        m_closing = false;
        emit closeBlocked(m_lastError);
    }
    emit changed();
    return started;
}

bool WorkbookSession::recordRecovery()
{
    if (!m_recovery || !m_workbook || !isDirty() || m_workbook->transactionActive()) return false;
    const OperationError error = m_recovery->record(m_workbook->snapshot());
    if (error) return fail(error.message);
    return true;
}

QVariantMap WorkbookSession::inspectRecovery() const
{
    return m_recovery && m_workbook ? m_recovery->inspectId(m_workbook->documentId()) : QVariantMap();
}

bool WorkbookSession::restoreRecovery()
{
    if (!m_recovery || !m_workbook) return fail(tr("No recovery manager"));
    const auto recovered = m_recovery->inspect(DocumentId::parse(m_workbook->documentId()));
    if (!recovered) return fail(tr("No valid recovery revision"));
    const QUrl url = m_url;
    const QByteArray sha = m_sha256;
    if (!m_workbook->restore(recovered->snapshot->payload)) return fail(m_workbook->lastError());
    m_url = url; // still the same file; the recovered state is not saved there yet
    m_sha256 = sha;
    m_workbook->markUnsaved();
    m_lastError.clear();
    emit changed();
    return true;
}

bool WorkbookSession::discardRecovery()
{
    return m_recovery && m_workbook && m_recovery->discard(DocumentId::parse(m_workbook->documentId()));
}

} // namespace QindaTK
