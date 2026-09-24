// SPDX-License-Identifier: LGPL-3.0-or-later
#include "workbench_host.h"

#include <QFileInfo>
#include <QJsonObject>
#include <QTimer>
#include <QThread>
#include <QSet>
#include <QUuid>
#include <qindatk/drag_drop_service.h>
#include <qindatk/transfer_payload.h>

namespace {
QString uuid() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
}

bool WorkbenchHost::openReferencePath(const QString &path)
{
    if (path.trimmed().isEmpty()) return report(QStringLiteral("Choose a document path"), false);
    DocumentSession *session = m_manager.openReferenceDocument(QFileInfo(path).absoluteFilePath());
    return report(session ? QStringLiteral("Opened %1").arg(session->title()) : m_manager.lastError(),
                  session != nullptr);
}

bool WorkbenchHost::saveWorkspace(const QString &path)
{
    if (path.trimmed().isEmpty()) return report(QStringLiteral("Choose a workspace path"), false);
    const QString windowId = uuid();
    QVariantList views;
    for (const QVariant &entry : m_manager.sessions()) {
        auto *session = entry.value<DocumentSession *>();
        if (!session) continue;
        for (const QString &viewId : m_manager.viewsFor(session->documentId()))
            views.append(QVariantMap{{QStringLiteral("viewId"), viewId},
                {QStringLiteral("documentId"), session->documentId()},
                {QStringLiteral("windowId"), windowId}, {QStringLiteral("state"), QVariantMap{}}});
    }
    const QVariantList windows{QVariantMap{{QStringLiteral("windowId"), windowId},
        {QStringLiteral("geometry"), QVariantMap{{QStringLiteral("x"), 0}, {QStringLiteral("y"), 0},
            {QStringLiteral("width"), 1280}, {QStringLiteral("height"), 800}}},
        {QStringLiteral("activeViewId"), views.isEmpty() ? QString{} : views.first().toMap().value(QStringLiteral("viewId"))},
        {QStringLiteral("maximized"), false}, {QStringLiteral("dockLayoutId"), QStringLiteral("workbench")}}};
    if (!m_workspace.capture(&m_manager, &m_dock, windows, views, windowId))
        return report(m_workspace.lastError(), false);
    m_workspace.setPath(QFileInfo(path).absoluteFilePath());
    const bool ok = m_workspace.save();
    return report(ok ? QStringLiteral("Workspace saved") : m_workspace.lastError(), ok);
}

bool WorkbenchHost::restoreWorkspace(const QString &path)
{
    if (path.trimmed().isEmpty()) return report(QStringLiteral("Choose a workspace path"), false);
    m_workspace.setPath(QFileInfo(path).absoluteFilePath());
    const bool ok = m_workspace.load() && m_workspace.restore(&m_manager, &m_dock);
    return report(ok ? QStringLiteral("Workspace restored") : m_workspace.lastError(), ok);
}

bool WorkbenchHost::restoreRecovery(const QString &documentId, const QString &originalPath)
{
    DocumentSession *old = m_manager.findSession(documentId);
    const bool wasBoard = old && old == m_board;
    const bool wasSheet = old && old == m_sheet;
    if (old) {
        if (wasBoard) m_board = nullptr;
        if (wasSheet) m_sheet = nullptr;
        if (!m_manager.closeDocument(documentId, DocumentManager::ClosePolicy::Discard))
        {
            if (wasBoard) m_board = old;
            if (wasSheet) m_sheet = old;
            return report(m_manager.lastError(), false);
        }
    }
    DocumentSession *restored = m_manager.restoreRecovered(documentId, originalPath);
    if (!restored) return report(m_manager.lastError(), false);
    if (restored->document()->containsId(QStringLiteral("00000000-0000-0000-0000-000000000101"))) {
        m_board = restored;
        bindProjection(restored, m_boardRowIds, &m_boardGrid, &m_boardAssets);
        registerNameHandler(restored, m_boardScope);
    } else if (restored->document()->containsId(QStringLiteral("00000000-0000-0000-0000-000000000201"))) {
        m_sheet = restored;
        bindProjection(restored, m_sheetRowIds, &m_sheetGrid, &m_sheetAssets);
        registerNameHandler(restored, m_sheetScope);
    }
    syncCommandScope();
    emit changed();
    return report(QStringLiteral("Recovered unsaved document changes"), true);
}

bool WorkbenchHost::copyAsset(const QString &rowId)
{
    DocumentSession *session = m_manager.activeSession();
    if (!session || !session->document()->containsId(rowId))
        return report(QStringLiteral("Select an asset in the active document"), false);
    QJsonObject object;
    for (const QString &property : {QStringLiteral("name"), QStringLiteral("kind"),
                                    QStringLiteral("owner"), QStringLiteral("state")})
        object.insert(property, QJsonValue::fromVariant(session->document()->valueFor(rowId, property)));
    object.insert(QStringLiteral("id"), rowId);
    TransferPayload payload{DocumentId::parse(session->documentId()),
        session->document()->header().revision, TransferOperation::Copy,
        {{EntityId::parse(rowId), TransferItemKind::Object, object}}};
    OperationError error;
    m_transferBytes = TransferPayloadCodec::encode(payload, &error);
    return report(m_transferBytes.isEmpty() ? error.message : QStringLiteral("Asset copied for transfer"),
                  !m_transferBytes.isEmpty());
}

bool WorkbenchHost::pasteAssetToOtherDocument()
{
    OperationError error;
    const auto payload = TransferPayloadCodec::decode(m_transferBytes, &error);
    DocumentSession *source = m_manager.findSession(payload ? payload->sourceDocumentId.toString() : QString{});
    DocumentSession *destination = source == m_board ? m_sheet : source == m_sheet ? m_board : nullptr;
    if (!payload || !destination || payload->items.size() != 1 || !source
        || source->document()->header().revision != payload->sourceRevision)
        return report(error.message.isEmpty() ? QStringLiteral("Transfer source changed or destination is unavailable") : error.message, false);
    const EntityId archiveId = archiveFor(destination);
    m_manager.activate(destination->documentId());
    PropertyCollectionDropTarget target(destination->document(), m_manager.undoService(), archiveId,
        QStringLiteral("transfers"), QStringLiteral("workbench-archive"), QStringLiteral("workbench-assets"));
    DragDropService transfer;
    const QString dragId = transfer.beginDrag(*payload, {TransferOperation::Copy},
        QStringLiteral("workbench-assets"), 0, source->document(), &error);
    if (dragId.isEmpty() || !transfer.previewDrop(&target, TransferOperation::Copy))
        return report(error.message.isEmpty() ? transfer.errorMap().value(QStringLiteral("message")).toString() : error.message, false);
    if (const OperationError committed = transfer.commitDrop(); committed)
        return report(committed.message, false);
    m_transferBytes.clear();
    return report(QStringLiteral("Asset copied into destination archive with a fresh stable ID"), true);
}

bool WorkbenchHost::startCloseDuringJob()
{
    DocumentSession *session = m_manager.activeSession();
    if (!session) return report(QStringLiteral("No active document for task"), false);
    const QString documentId = session->documentId();
    const SnapshotHeader expected = session->document()->header();
    JobSpec spec;
    spec.id = uuid(); spec.label = QStringLiteral("Delayed analysis"); spec.expected = expected;
    spec.document = session->document();
    spec.work = [](const CancellationToken &token, const JobProgress &progress) {
        QThread::msleep(300); progress(1.0);
        if (token.isCancelled()) return JobResult{{}, {ErrorCode::Cancelled, QStringLiteral("Canceled"), {}, {}}, false};
        return JobResult{{{QStringLiteral("summary"), QStringLiteral("analysis complete")}}, {}, false};
    };
    const QString jobId = m_jobs.enqueue(std::move(spec));
    if (jobId.isEmpty()) return report(QStringLiteral("Could not start delayed analysis"), false);
    m_jobDocuments.insert(jobId, documentId);
    if (session == m_board) m_board = nullptr;
    if (session == m_sheet) m_sheet = nullptr;
    const bool closed = m_manager.closeDocument(documentId, DocumentManager::ClosePolicy::Discard);
    if (!closed) return report(m_manager.lastError(), false);
    m_jobs.invalidateDocument(documentId);
    return report(QStringLiteral("Closed document while analysis was running"), true);
}

void WorkbenchHost::invalidateClosedJobs()
{
    QSet<QString> open;
    for (const QVariant &entry : m_manager.sessions()) {
        if (auto *session = entry.value<DocumentSession *>()) open.insert(session->documentId());
    }
    for (auto it = m_jobDocuments.cbegin(); it != m_jobDocuments.cend(); ++it)
        if (!open.contains(it.value())) m_jobs.invalidateDocument(it.value());
}

void WorkbenchHost::finishAsyncJob(const QString &jobId, JobModel::JobState state,
                                   const QVariantMap &, const QString &)
{
    const QString documentId = m_jobDocuments.take(jobId);
    if (state == JobModel::Succeeded && m_manager.findSession(documentId)
        && m_manager.activeSession() && m_manager.activeSession()->documentId() == documentId)
        m_lastJobMessage = QStringLiteral("Applied analysis result to active document");
    else
        m_lastJobMessage = QStringLiteral("Ignored stale analysis result for closed or inactive document");
    emit changed();
}
