// SPDX-License-Identifier: LGPL-3.0-or-later
#include "document_manager.h"
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QSet>

namespace QindaTK {

DocumentManager::DocumentManager(QObject *parent)
    : QObject(parent), m_undo(new UndoService(this)),
      m_recovery(new RecoveryManager(this)),
      m_store(std::make_shared<AtomicDocumentStore>())
{
    m_recovery->setDirectory(QDir(QStandardPaths::writableLocation(
        QStandardPaths::AppDataLocation)).filePath(QStringLiteral("recovery")));
}

QVariantList DocumentManager::sessions() const
{
    QVariantList result;
    for (auto *session : m_sessions) result.append(QVariant::fromValue(session));
    return result;
}

void DocumentManager::setStore(DocumentStorePtr store)
{
    if (!m_sessions.isEmpty() || !store) return;
    m_store = std::move(store);
}

QString DocumentManager::canonicalPath(const QString &path)
{
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    return QDir::cleanPath(canonical.isEmpty() ? info.absoluteFilePath() : canonical);
}

DocumentSession *DocumentManager::findSession(const QString &documentId) const
{
    const DocumentId id = DocumentId::parse(documentId);
    if (!id.isValid()) return nullptr;
    for (auto *session : m_sessions)
        if (session->document()->documentId() == id) return session;
    return nullptr;
}

DocumentSession *DocumentManager::adopt(std::unique_ptr<PropertyDocument> document)
{
    if (!document || findSession(document->documentIdString())) return nullptr;
    auto *session = new DocumentSession(std::move(document), m_undo, m_store, this);
    session->m_pathAvailable = [this, session](const QString &path) {
        const QString candidate = canonicalPath(path);
        for (auto *other : m_sessions)
            if (other != session && !other->uri().isEmpty()
                && canonicalPath(other->uri()) == candidate) return false;
        return true;
    };
    session->setRecoveryManager(m_recovery);
    m_sessions.append(session);
    const QString viewId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_views.insert(viewId, session->documentId());
    m_viewOrder.append(viewId);
    connect(session, &DocumentSession::changed, this, &DocumentManager::changed);
    activate(session->documentId());
    emit changed();
    return session;
}

DocumentSession *DocumentManager::create(std::unique_ptr<PropertyDocument> document)
{
    return adopt(std::move(document));
}

DocumentSession *DocumentManager::createReferenceDocument()
{
    return adopt(std::make_unique<PropertyDocument>());
}

DocumentSession *DocumentManager::openReferenceDocument(const QString &path)
{
    if (path.isEmpty()) return nullptr;
    const QString canonical = canonicalPath(path);
    for (auto *session : m_sessions)
        if (!session->uri().isEmpty() && canonicalPath(session->uri()) == canonical) {
            activate(session->documentId());
            return session;
        }
    const auto loaded = m_store->load(canonical);
    if (!loaded) {
        m_lastError = loaded.error.message;
        emit changed();
        return nullptr;
    }
    OperationError error;
    auto decoded = ReferenceDocumentCodec::decode(loaded.bytes, &error);
    if (!decoded || findSession(decoded->document->documentIdString())) {
        m_lastError = decoded ? QStringLiteral("Document identity is already open")
                              : error.message;
        emit changed();
        return nullptr;
    }
    const QJsonObject extensions = decoded->extensions;
    auto *session = adopt(std::move(decoded->document));
    session->initializeLoaded(canonical, loaded.sha256,
                              session->document()->revision().value,
                              extensions, false);
    m_lastError.clear();
    emit changed();
    return session;
}

DocumentSession *DocumentManager::restoreRecovered(const QString &documentId,
                                                    const QString &originalPath)
{
    const DocumentId id = DocumentId::parse(documentId);
    if (!id.isValid() || findSession(id.toString())) return nullptr;
    OperationError error;
    auto restored = m_recovery->restore(id, &error);
    if (!restored) {
        m_lastError = error.message;
        emit changed();
        return nullptr;
    }
    QByteArray baseline;
    quint64 baselineRevision = 0;
    QString path;
    if (!originalPath.isEmpty()) {
        path = canonicalPath(originalPath);
        const auto saved = m_store->load(path);
        if (saved) {
            auto original = ReferenceDocumentCodec::decode(saved.bytes);
            if (original && original->document->documentId() == id
                && original->document->revision().value
                       <= restored->document->revision().value) {
                baseline = saved.sha256;
                baselineRevision = original->document->revision().value;
            }
        }
    }
    const QJsonObject extensions = restored->extensions;
    auto *session = adopt(std::move(restored->document));
    session->initializeLoaded(path, baseline, baselineRevision, extensions, true);
    m_lastError.clear();
    emit changed();
    return session;
}

bool DocumentManager::activate(const QString &documentId)
{
    auto *session = findSession(documentId);
    if (!session) return false;
    m_active = session;
    m_undo->setActiveDocument(session->document());
    emit changed();
    return true;
}

QString DocumentManager::duplicateView(const QString &documentId)
{
    if (!findSession(documentId)) return {};
    const QString viewId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_views.insert(viewId, documentId);
    const QStringList siblings = viewsFor(documentId);
    const int after = siblings.isEmpty() ? m_viewOrder.size()
                                         : m_viewOrder.indexOf(siblings.last()) + 1;
    m_viewOrder.insert(after, viewId);
    emit changed();
    return viewId;
}

QStringList DocumentManager::viewsFor(const QString &documentId) const
{
    QStringList result;
    for (const QString &viewId : m_viewOrder)
        if (m_views.value(viewId) == documentId) result.append(viewId);
    return result;
}

bool DocumentManager::moveView(const QString &viewId, const QString &beforeViewId)
{
    if (!m_views.contains(viewId) || (!beforeViewId.isEmpty() && !m_views.contains(beforeViewId)))
        return false;
    if (viewId == beforeViewId) return true;
    const QStringList prior = m_viewOrder;
    m_viewOrder.removeAll(viewId);
    const int target = beforeViewId.isEmpty() ? m_viewOrder.size()
                                            : m_viewOrder.indexOf(beforeViewId);
    m_viewOrder.insert(target, viewId);
    if (m_viewOrder != prior) emit changed();
    return true;
}

bool DocumentManager::removeView(const QString &documentId, const QString &viewId)
{
    if (!findSession(documentId) || m_views.value(viewId) != documentId
        || viewsFor(documentId).size() <= 1) return false;
    m_views.remove(viewId);
    m_viewOrder.removeAll(viewId);
    emit changed();
    return true;
}

bool DocumentManager::setViewIds(const QString &documentId, const QStringList &viewIds)
{
    if (!findSession(documentId) || viewIds.isEmpty()) return false;
    QSet<QString> requested;
    for (const QString &id : viewIds) {
        if (QUuid(id).isNull() || requested.contains(id)
            || (m_views.contains(id) && m_views.value(id) != documentId)) return false;
        requested.insert(id);
    }
    int insertAt = m_viewOrder.size();
    for (int i = 0; i < m_viewOrder.size(); ++i)
        if (m_views.value(m_viewOrder[i]) == documentId) { insertAt = i; break; }
    for (auto it = m_views.begin(); it != m_views.end();)
        if (it.value() == documentId) it = m_views.erase(it);
        else ++it;
    for (int i = m_viewOrder.size() - 1; i >= 0; --i)
        if (!m_views.contains(m_viewOrder[i])) m_viewOrder.removeAt(i);
    for (const QString &id : viewIds) {
        m_views.insert(id, documentId);
        m_viewOrder.insert(insertAt++, id);
    }
    emit changed();
    return true;
}

bool DocumentManager::setViewOrder(const QStringList &viewIds)
{
    if (viewIds.size() != m_views.size()) return false;
    QSet<QString> unique;
    for (const QString &id : viewIds) {
        if (!m_views.contains(id) || unique.contains(id)) return false;
        unique.insert(id);
    }
    if (m_viewOrder == viewIds) return true;
    m_viewOrder = viewIds;
    emit changed();
    return true;
}

bool DocumentManager::remove(DocumentSession *session)
{
    if (!session || !m_sessions.removeOne(session)) return false;
    const QString id = session->documentId();
    for (auto it = m_views.begin(); it != m_views.end();)
        if (it.value() == id) it = m_views.erase(it);
        else ++it;
    for (int i = m_viewOrder.size() - 1; i >= 0; --i)
        if (!m_views.contains(m_viewOrder[i])) m_viewOrder.removeAt(i);
    if (m_active == session) {
        m_active = m_sessions.isEmpty() ? nullptr : m_sessions.last();
        m_undo->setActiveDocument(m_active ? m_active->document() : nullptr);
    }
    delete session;
    emit changed();
    return true;
}

bool DocumentManager::closeDocument(const QString &documentId, ClosePolicy policy)
{
    auto *session = findSession(documentId);
    if (!session) return false;
    if (session->isSaving()) {
        emit closeBlocked(documentId, QStringLiteral("Save is still in progress"));
        return false;
    }
    if (!session->isDirty()) return remove(session);
    if (policy == ClosePolicy::Cancel) {
        emit closeBlocked(documentId, QStringLiteral("Unsaved changes"));
        return false;
    }
    if (policy == ClosePolicy::Discard) return remove(session);
    if (session->uri().isEmpty()) {
        emit closeBlocked(documentId, QStringLiteral("Save As path required"));
        return false;
    }
    if (!session->requestSave()) {
        emit closeBlocked(documentId, session->lastError());
        return false;
    }
    connect(session, &DocumentSession::saveFinished, this,
            [this, session, documentId](bool success, const QString &message, const QString &) {
        if (success && !session->isDirty()) remove(session);
        else emit closeBlocked(documentId, success
                             ? QStringLiteral("Newer edits remain unsaved") : message);
    }, Qt::SingleShotConnection);
    return true;
}

void DocumentManager::finishSaveAllIfDone()
{
    if (m_saveAllRunning && m_saveAllPending == 0) {
        m_saveAllRunning = false;
        emit saveAllFinished(m_saveAllResults);
    }
}

bool DocumentManager::requestSaveAll()
{
    if (m_saveAllRunning) return false;
    m_saveAllRunning = true;
    m_saveAllPending = 0;
    m_saveAllResults.clear();
    for (auto *session : m_sessions) {
        if (!session->isDirty()) continue;
        const QString id = session->documentId();
        if (!session->requestSave()) {
            m_saveAllResults.append(QVariantMap{{QStringLiteral("documentId"), id},
                {QStringLiteral("success"), false},
                {QStringLiteral("message"), session->lastError()}});
            continue;
        }
        ++m_saveAllPending;
        connect(session, &DocumentSession::saveFinished, this,
                [this, id](bool success, const QString &message, const QString &revision) {
            m_saveAllResults.append(QVariantMap{{QStringLiteral("documentId"), id},
                {QStringLiteral("success"), success},
                {QStringLiteral("message"), message},
                {QStringLiteral("savedRevision"), revision}});
            --m_saveAllPending;
            finishSaveAllIfDone();
        }, Qt::SingleShotConnection);
    }
    finishSaveAllIfDone();
    return true;
}

} // namespace QindaTK
