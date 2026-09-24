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
    m_recovery->setCodecRegistry(m_codecs);
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

bool DocumentManager::setCodecRegistry(std::shared_ptr<DocumentCodecRegistry> registry)
{
    if (!m_sessions.isEmpty() || !registry) return false;
    m_codecs = std::move(registry);
    m_recovery->setCodecRegistry(m_codecs);
    emit changed();
    return true;
}

QVariantList DocumentManager::documentTypes() const
{
    QVariantList result;
    for (const auto &codec : m_codecs->codecs()) result.append(codec->info());
    return result;
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
    const QString key = id.toString();
    for (auto *session : m_sessions)
        if (session->documentId() == key) return session;
    return nullptr;
}

DocumentSession *DocumentManager::failWith(const OperationError &error)
{
    m_lastError = error.message;
    emit changed();
    return nullptr;
}

DocumentSession *DocumentManager::adopt(std::unique_ptr<DocumentModel> model, DocumentCodecPtr codec)
{
    if (!model || !codec || findSession(model->modelId().toString())) return nullptr;
    auto *session = new DocumentSession(std::move(model), std::move(codec), m_undo, m_store, this);
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
    if (!document) return nullptr;
    DocumentCodecPtr codec = m_codecs->codec(document->header().schemaId);
    if (!codec) codec = referenceDocumentCodec();
    return adopt(std::move(document), std::move(codec));
}

DocumentSession *DocumentManager::create(std::unique_ptr<DocumentModel> model)
{
    if (!model) return nullptr;
    const QString schema = model->modelHeader().schemaId;
    DocumentCodecPtr codec = m_codecs->codec(schema);
    if (!codec)
        return failWith({ErrorCode::Unsupported,
                         QStringLiteral("No codec is registered for schema %1").arg(schema), {}, {}});
    return adopt(std::move(model), std::move(codec));
}

DocumentSession *DocumentManager::createReferenceDocument()
{
    return create(std::make_unique<PropertyDocument>());
}

DocumentSession *DocumentManager::createDocument(const QString &schemaId)
{
    const DocumentCodecPtr codec = m_codecs->codec(schemaId);
    if (!codec)
        return failWith({ErrorCode::Unsupported,
                         QStringLiteral("No codec is registered for schema %1").arg(schemaId), {}, {}});
    return adopt(codec->create(), codec);
}

DocumentSession *DocumentManager::adoptBytes(const QByteArray &bytes, OperationError *error)
{
    const auto fail = [&](const OperationError &problem) {
        if (error) *error = problem;
        return failWith(problem);
    };
    if (error) *error = {};
    const DocumentCodecPtr codec = m_codecs->detect(bytes);
    if (!codec) return fail({ErrorCode::Unsupported, QStringLiteral("Unrecognized document format"), {}, {}});
    OperationError problem;
    const DecodedDocumentPtr decoded = codec->decode(bytes, &problem);
    if (!decoded) return fail(problem);
    if (findSession(decoded->header().documentId.toString()))
        return fail({ErrorCode::Conflict, QStringLiteral("Document identity is already open"), {}, {}});
    auto model = codec->adopt(*decoded, &problem);
    if (!model) return fail(problem);
    auto *session = adopt(std::move(model), codec);
    if (!session) return fail({ErrorCode::Conflict, QStringLiteral("Document identity is already open"), {}, {}});
    session->setExtensions(decoded->extensions());
    m_lastError.clear();
    emit changed();
    return session;
}

DocumentSession *DocumentManager::openReferenceDocument(const QString &path)
{
    return openDocument(path, QString::fromLatin1(PropertyDocument::SchemaId));
}

DocumentSession *DocumentManager::openDocument(const QString &path, const QString &schemaId)
{
    if (path.isEmpty()) return nullptr;
    const QString canonical = canonicalPath(path);
    for (auto *session : m_sessions)
        if (!session->uri().isEmpty() && canonicalPath(session->uri()) == canonical) {
            if (!schemaId.isEmpty() && session->schemaId() != schemaId)
                return failWith({ErrorCode::Conflict,
                                 QStringLiteral("The open document at this path has another schema"), {}, {}});
            activate(session->documentId());
            return session;
        }
    const auto loaded = m_store->load(canonical);
    if (!loaded) return failWith(loaded.error);
    const DocumentCodecPtr codec = m_codecs->detect(loaded.bytes);
    if (!codec)
        return failWith({ErrorCode::Unsupported, QStringLiteral("Unrecognized document format"), canonical, {}});
    if (!schemaId.isEmpty() && codec->schemaId() != schemaId)
        return failWith({ErrorCode::Unsupported, QStringLiteral("The file holds another document kind"),
                         canonical, {}});
    OperationError error;
    const DecodedDocumentPtr decoded = codec->decode(loaded.bytes, &error);
    if (!decoded) return failWith(error);
    if (findSession(decoded->header().documentId.toString()))
        return failWith({ErrorCode::Conflict, QStringLiteral("Document identity is already open"), {}, {}});
    auto model = codec->adopt(*decoded, &error);
    if (!model) return failWith(error);
    if (const auto mismatch = compareSnapshotHeaders(model->modelHeader(), decoded->header()))
        return failWith(mismatch);
    auto *session = adopt(std::move(model), codec);
    session->initializeLoaded(canonical, loaded.sha256, decoded->header().revision.value,
                              decoded->extensions(), false);
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
    const auto restored = m_recovery->restoreDocument(id, &error);
    if (!restored) return failWith(error);
    auto model = restored->codec->adopt(*restored->decoded, &error);
    if (!model) return failWith(error);
    const SnapshotHeader &recovered = restored->snapshot->header;
    QByteArray baseline;
    quint64 baselineRevision = 0;
    QString path;
    if (!originalPath.isEmpty()) {
        path = canonicalPath(originalPath);
        const auto saved = m_store->load(path);
        const auto header = saved && restored->codec->recognizes(saved.bytes)
            ? restored->codec->inspect(saved.bytes) : std::nullopt;
        // The saved file is a baseline only while it holds the same document
        // at an older revision, or exactly the recovered bytes.
        if (header && header->schemaId == recovered.schemaId && header->documentId == id
            && (header->revision.value < recovered.revision.value
                || saved.bytes == restored->snapshot->payload)) {
            baseline = saved.sha256;
            baselineRevision = header->revision.value;
        }
    }
    auto *session = adopt(std::move(model), restored->codec);
    session->initializeLoaded(path, baseline, baselineRevision, restored->decoded->extensions(), true);
    m_lastError.clear();
    emit changed();
    return session;
}

bool DocumentManager::activate(const QString &documentId)
{
    auto *session = findSession(documentId);
    if (!session) return false;
    m_active = session;
    m_undo->setActiveModel(session->model());
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
        m_undo->setActiveModel(m_active ? m_active->model() : nullptr);
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
