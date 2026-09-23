// SPDX-License-Identifier: LGPL-3.0-or-later
#include "workspace_session.h"
#include <QDir>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QScreen>
#include <QSet>
#include <QStandardPaths>
#include <algorithm>
#include <cmath>

namespace QindaTK {
namespace {
constexpr qsizetype kMaximumWorkspaceBytes = 16 * 1024 * 1024;

bool number(const QVariantMap &map, const QString &key, double *out)
{
    const QVariant value = map.value(key);
    const int type = value.metaType().id();
    if (type != QMetaType::Double && type != QMetaType::Int
        && type != QMetaType::UInt && type != QMetaType::LongLong) return false;
    *out = value.toDouble();
    return std::isfinite(*out);
}

std::optional<QRectF> rectOf(const QVariant &value)
{
    if (value.metaType().id() != QMetaType::QVariantMap) return std::nullopt;
    const QVariantMap map = value.toMap();
    double x, y, width, height;
    if (!number(map, QStringLiteral("x"), &x)
        || !number(map, QStringLiteral("y"), &y)
        || !number(map, QStringLiteral("width"), &width)
        || !number(map, QStringLiteral("height"), &height)
        || width <= 0 || height <= 0) return std::nullopt;
    return QRectF(x, y, width, height);
}

QVariantMap mapOf(const QRectF &rect)
{
    return {{QStringLiteral("x"), rect.x()}, {QStringLiteral("y"), rect.y()},
            {QStringLiteral("width"), rect.width()},
            {QStringLiteral("height"), rect.height()}};
}

bool nonemptyString(const QVariantMap &map, const QString &key)
{
    return map.value(key).metaType().id() == QMetaType::QString
        && !map.value(key).toString().isEmpty();
}

std::optional<QVariantList> requireRecords(const QVariantMap &root, const QString &key,
                                            OperationError *error)
{
    const QVariant value = root.value(key);
    if (value.metaType().id() != QMetaType::QVariantList) {
        if (error) *error = {ErrorCode::Validation, QStringLiteral("Expected record list"), key, {}};
        return std::nullopt;
    }
    return value.toList();
}

void invalid(OperationError *error, const QString &field)
{
    if (error) *error = {ErrorCode::Validation,
        QStringLiteral("Invalid workspace state"), field, {}};
}
} // namespace

WorkspaceSession::WorkspaceSession(QObject *parent)
    : QObject(parent), m_store(std::make_shared<AtomicStateStore>())
{
    m_path = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
                 .filePath(QStringLiteral("workspace.json"));
    reset();
}

void WorkspaceSession::setPath(const QString &path)
{
    if (path == m_path) return;
    m_path = path;
    emit changed();
}

void WorkspaceSession::setStore(StateStorePtr store)
{
    if (store) m_store = std::move(store);
}

QVariantList WorkspaceSession::windows() const
{
    return m_state.value(QStringLiteral("windows")).toList();
}

QVariantList WorkspaceSession::views() const
{
    return m_state.value(QStringLiteral("views")).toList();
}

QVariantList WorkspaceSession::documents() const
{
    return m_state.value(QStringLiteral("documents")).toList();
}

QString WorkspaceSession::activeWindowId() const
{
    return m_state.value(QStringLiteral("activeWindowId")).toString();
}

QString WorkspaceSession::activeDocumentId() const
{
    return m_state.value(QStringLiteral("activeDocumentId")).toString();
}

QVariantMap WorkspaceSession::state() const { return m_state; }

void WorkspaceSession::fail(const OperationError &error)
{
    m_lastError = error.message;
    emit changed();
}

std::optional<QVariantMap> WorkspaceSession::normalize(const QVariantMap &input,
                                                        const QVariantList &screenInput,
                                                        OperationError *error)
{
    QVariantMap root = input;
    double versionNumber = 0;
    if (!number(root, QStringLiteral("version"), &versionNumber)
        || (versionNumber != 1 && versionNumber != 2)) {
        invalid(error, QStringLiteral("version"));
        return std::nullopt;
    }
    const auto docs = requireRecords(root, QStringLiteral("documents"), error);
    const auto views = requireRecords(root, QStringLiteral("views"), error);
    const auto windows = requireRecords(root, QStringLiteral("windows"), error);
    if (!docs || !views || !windows) return std::nullopt;
    if (root.value(QStringLiteral("dockState")).metaType().id() != QMetaType::QString
        || (root.contains(QStringLiteral("dockWorkspace"))
            && root.value(QStringLiteral("dockWorkspace")).metaType().id() != QMetaType::QString)
        || (root.contains(QStringLiteral("extensions"))
            && root.value(QStringLiteral("extensions")).metaType().id() != QMetaType::QVariantMap)) {
        invalid(error, QStringLiteral("dockState"));
        return std::nullopt;
    }
    QSet<QString> documentIds, viewIds, windowIds;
    for (const QVariant &value : *docs) {
        if (value.metaType().id() != QMetaType::QVariantMap) {
            invalid(error, QStringLiteral("documents")); return std::nullopt;
        }
        const QVariantMap record = value.toMap();
        const QString id = record.value(QStringLiteral("documentId")).toString();
        if (!DocumentId::parse(id).isValid() || documentIds.contains(id)
            || record.value(QStringLiteral("uri")).metaType().id() != QMetaType::QString) {
            invalid(error, QStringLiteral("documents")); return std::nullopt;
        }
        documentIds.insert(id);
    }
    QVariantList nextViews;
    for (const QVariant &value : *views) {
        if (value.metaType().id() != QMetaType::QVariantMap) {
            invalid(error, QStringLiteral("views")); return std::nullopt;
        }
        QVariantMap record = value.toMap();
        const QString id = record.value(QStringLiteral("viewId")).toString();
        const QString documentId = record.value(QStringLiteral("documentId")).toString();
        if (!QUuid(id).isNull() && !viewIds.contains(id) && documentIds.contains(documentId)
            && nonemptyString(record, QStringLiteral("windowId"))
            && (!record.contains(QStringLiteral("state"))
                || record.value(QStringLiteral("state")).metaType().id() == QMetaType::QVariantMap)) {
            if (!record.contains(QStringLiteral("state"))) record.insert(QStringLiteral("state"), QVariantMap{});
            viewIds.insert(id);
            nextViews.append(record);
        } else {
            invalid(error, QStringLiteral("views")); return std::nullopt;
        }
    }
    const QVariantList &screens = screenInput;
    QHash<QString, QRectF> available;
    QString firstScreenId;
    for (const QVariant &value : screens) {
        if (value.metaType().id() != QMetaType::QVariantMap) continue;
        const QVariantMap screen = value.toMap();
        const auto rect = rectOf(screen.value(QStringLiteral("availableRect")));
        if (rect && nonemptyString(screen, QStringLiteral("id"))) {
            const QString id = screen.value(QStringLiteral("id")).toString();
            if (firstScreenId.isEmpty()) firstScreenId = id;
            available.insert(id, *rect);
        }
    }
    QVariantList nextWindows;
    for (const QVariant &value : *windows) {
        if (value.metaType().id() != QMetaType::QVariantMap) {
            invalid(error, QStringLiteral("windows")); return std::nullopt;
        }
        QVariantMap record = value.toMap();
        const QString id = record.value(QStringLiteral("windowId")).toString();
        auto geometry = rectOf(record.value(QStringLiteral("geometry")));
        const QString activeView = record.value(QStringLiteral("activeViewId")).toString();
        if (!nonemptyString(record, QStringLiteral("windowId")) || windowIds.contains(id)
            || !geometry || (record.contains(QStringLiteral("maximized"))
                && record.value(QStringLiteral("maximized")).metaType().id() != QMetaType::Bool)
            || (record.contains(QStringLiteral("dockLayoutId"))
                && record.value(QStringLiteral("dockLayoutId")).metaType().id() != QMetaType::QString)
            || (!activeView.isEmpty() && !viewIds.contains(activeView))) {
            invalid(error, QStringLiteral("windows")); return std::nullopt;
        }
        windowIds.insert(id);
        QString screenId = record.value(QStringLiteral("screenId")).toString();
        if (!available.isEmpty()) {
            if (!available.contains(screenId)) screenId = firstScreenId;
            const QRectF bounds = available.value(screenId);
            geometry->setWidth(std::min(geometry->width(), bounds.width()));
            geometry->setHeight(std::min(geometry->height(), bounds.height()));
            geometry->moveLeft(std::clamp(geometry->left(), bounds.left(),
                                          bounds.right() - geometry->width()));
            geometry->moveTop(std::clamp(geometry->top(), bounds.top(),
                                         bounds.bottom() - geometry->height()));
            record.insert(QStringLiteral("screenId"), screenId);
            record.insert(QStringLiteral("geometry"), mapOf(*geometry));
        }
        if (!record.contains(QStringLiteral("maximized"))) record.insert(QStringLiteral("maximized"), false);
        if (!record.contains(QStringLiteral("dockLayoutId"))) record.insert(QStringLiteral("dockLayoutId"), QString{});
        nextWindows.append(record);
    }
    for (const QVariant &value : nextViews)
        if (!windowIds.contains(value.toMap().value(QStringLiteral("windowId")).toString())) {
            invalid(error, QStringLiteral("views.windowId")); return std::nullopt;
        }
    for (const QVariant &value : nextWindows) {
        const QVariantMap record = value.toMap();
        const QString activeView = record.value(QStringLiteral("activeViewId")).toString();
        if (!activeView.isEmpty()) {
            bool belongs = false;
            for (const QVariant &view : nextViews)
                if (view.toMap().value(QStringLiteral("viewId")).toString() == activeView
                    && view.toMap().value(QStringLiteral("windowId")).toString()
                       == record.value(QStringLiteral("windowId")).toString()) belongs = true;
            if (!belongs) { invalid(error, QStringLiteral("windows.activeViewId")); return std::nullopt; }
        }
    }
    const QString activeWindow = root.value(QStringLiteral("activeWindowId")).toString();
    const QString activeDocument = root.value(QStringLiteral("activeDocumentId")).toString();
    if ((!activeWindow.isEmpty() && !windowIds.contains(activeWindow))
        || (!activeDocument.isEmpty() && !documentIds.contains(activeDocument))) {
        invalid(error, QStringLiteral("active")); return std::nullopt;
    }
    root.insert(QStringLiteral("version"), 2);
    root.insert(QStringLiteral("documents"), *docs);
    root.insert(QStringLiteral("views"), nextViews);
    root.insert(QStringLiteral("windows"), nextWindows);
    root.insert(QStringLiteral("activeWindowId"), activeWindow);
    root.insert(QStringLiteral("activeDocumentId"), activeDocument);
    if (!root.contains(QStringLiteral("dockWorkspace")))
        root.insert(QStringLiteral("dockWorkspace"), QStringLiteral("default"));
    if (!root.contains(QStringLiteral("extensions"))) root.insert(QStringLiteral("extensions"), QVariantMap{});
    if (error) *error = {};
    return root;
}

bool WorkspaceSession::setState(const QVariantMap &state, const QVariantList &screens)
{
    OperationError error;
    QVariantList currentScreens = screens;
    if (currentScreens.isEmpty()) {
        for (const QScreen *screen : QGuiApplication::screens()) {
            currentScreens.append(QVariantMap{{QStringLiteral("id"), screen->name()},
                {QStringLiteral("availableRect"), mapOf(screen->availableGeometry())}});
        }
    }
    const auto normalized = normalize(state, currentScreens, &error);
    if (!normalized) { fail(error); return false; }
    m_state = *normalized;
    m_unavailableDocuments.clear();
    fail({});
    return true;
}

QByteArray WorkspaceSession::encode(const QVariantMap &state, OperationError *error)
{
    const auto normalized = normalize(state, {}, error);
    if (!normalized) return {};
    return QJsonDocument(QJsonObject::fromVariantMap(*normalized))
        .toJson(QJsonDocument::Compact);
}

std::optional<QVariantMap> WorkspaceSession::decode(const QByteArray &bytes,
                                                     OperationError *error)
{
    QJsonParseError parse{};
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject()) {
        invalid(error, QStringLiteral("json"));
        return std::nullopt;
    }
    return normalize(document.object().toVariantMap(), {}, error);
}

bool WorkspaceSession::capture(DocumentManager *documents, DockModel *dock,
                               const QVariantList &windowRecords,
                               const QVariantList &viewRecords,
                               const QString &activeWindow)
{
    if (!documents || !dock) {
        fail({ErrorCode::InvalidArgument, QStringLiteral("Document manager and dock model required"), {}, {}});
        return false;
    }
    QVariantList documentRecords;
    QVariantList allViews = viewRecords;
    const QString defaultWindow = windowRecords.isEmpty() ? QString{}
        : windowRecords.first().toMap().value(QStringLiteral("windowId")).toString();
    for (const QVariant &value : documents->sessions()) {
        auto *session = value.value<DocumentSession *>();
        if (!session) continue;
        documentRecords.append(QVariantMap{{QStringLiteral("documentId"), session->documentId()},
            {QStringLiteral("uri"), session->uri()}});
        if (viewRecords.isEmpty())
            for (const QString &viewId : documents->viewsFor(session->documentId()))
                allViews.append(QVariantMap{{QStringLiteral("viewId"), viewId},
                    {QStringLiteral("documentId"), session->documentId()},
                    {QStringLiteral("windowId"), defaultWindow},
                    {QStringLiteral("state"), QVariantMap{}}});
    }
    QVariantMap next = m_state;
    next.insert(QStringLiteral("version"), 2);
    next.insert(QStringLiteral("documents"), documentRecords);
    next.insert(QStringLiteral("views"), allViews);
    next.insert(QStringLiteral("windows"), windowRecords);
    next.insert(QStringLiteral("activeWindowId"), activeWindow);
    next.insert(QStringLiteral("activeDocumentId"), documents->activeSession()
        ? documents->activeSession()->documentId() : QString{});
    next.insert(QStringLiteral("dockState"), dock->serialize());
    next.insert(QStringLiteral("dockWorkspace"), dock->workspace());
    return setState(next);
}

bool WorkspaceSession::restore(DocumentManager *documents, DockModel *dock)
{
    if (!documents || !dock) {
        fail({ErrorCode::InvalidArgument, QStringLiteral("Document manager and dock model required"), {}, {}});
        return false;
    }
    // Validate the model's serialized arrangement before opening documents.
    // This is not a document transaction: an unavailable URI is reported and
    // retained for a later restore attempt; no document bytes are mutated.
    const QString serialized = m_state.value(QStringLiteral("dockState")).toString();
    if (!serialized.isEmpty() && !dock->deserialize(serialized)) {
        fail({ErrorCode::Validation, QStringLiteral("Invalid dock arrangement"),
              QStringLiteral("dockState"), {}});
        return false;
    }
    dock->setWorkspace(m_state.value(QStringLiteral("dockWorkspace")).toString());
    QStringList unavailable;
    for (const QVariant &value : this->documents()) {
        const QVariantMap record = value.toMap();
        const QString id = record.value(QStringLiteral("documentId")).toString();
        const QString uri = record.value(QStringLiteral("uri")).toString();
        DocumentSession *session = documents->findSession(id);
        if (!session && !uri.isEmpty()) session = documents->openReferenceDocument(uri);
        if (!session || session->documentId() != id) unavailable.append(id);
    }
    for (const QVariant &value : this->documents()) {
        const QString id = value.toMap().value(QStringLiteral("documentId")).toString();
        if (!documents->findSession(id)) continue;
        QStringList ids;
        for (const QVariant &view : this->views())
            if (view.toMap().value(QStringLiteral("documentId")).toString() == id)
                ids.append(view.toMap().value(QStringLiteral("viewId")).toString());
        if (!ids.isEmpty()) documents->setViewIds(id, ids);
    }
    const QString active = activeDocumentId();
    if (!active.isEmpty() && documents->findSession(active)) documents->activate(active);
    m_unavailableDocuments = unavailable;
    fail({});
    return true;
}

bool WorkspaceSession::load(const QVariantList &screens)
{
    const StateReadResult read = m_store->read(m_path, kMaximumWorkspaceBytes);
    if (!read) { fail(read.error); return false; }
    QJsonParseError parse{};
    const QJsonDocument document = QJsonDocument::fromJson(read.bytes, &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject()) {
        fail({ErrorCode::Validation, QStringLiteral("Invalid workspace JSON"), {}, {}});
        return false;
    }
    return setState(document.object().toVariantMap(), screens);
}

bool WorkspaceSession::save()
{
    OperationError error;
    const QByteArray bytes = encode(m_state, &error);
    if (bytes.isEmpty()) { fail(error); return false; }
    if (bytes.size() > kMaximumWorkspaceBytes) {
        fail({ErrorCode::Unsupported, QStringLiteral("Workspace file exceeds limit"), m_path, {}});
        return false;
    }
    error = m_store->write(m_path, bytes);
    fail(error);
    return !error;
}

void WorkspaceSession::reset()
{
    m_state = {{QStringLiteral("version"), 2},
               {QStringLiteral("documents"), QVariantList{}},
               {QStringLiteral("views"), QVariantList{}},
               {QStringLiteral("windows"), QVariantList{}},
               {QStringLiteral("activeWindowId"), QString{}},
               {QStringLiteral("activeDocumentId"), QString{}},
               {QStringLiteral("dockState"), QString{}},
               {QStringLiteral("dockWorkspace"), QStringLiteral("default")},
               {QStringLiteral("extensions"), QVariantMap{}}};
    m_unavailableDocuments.clear();
    fail({});
}

} // namespace QindaTK
