// SPDX-License-Identifier: LGPL-3.0-or-later
#include "window_host.h"
#include "document_manager.h"
#include <QEvent>
#include <QGuiApplication>
#include <QScreen>
#include <QUuid>
#include <QTimer>

namespace QindaTK {
WindowHost::WindowHost(QObject *parent) : QObject(parent)
{
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, [this] { reconcileScreens(); });
    connect(qGuiApp, &QGuiApplication::screenAdded, this, [this] { reconcileScreens(); });
    connect(qGuiApp, &QGuiApplication::focusWindowChanged, this, [this] { updateActive(); });
}
WindowHost::~WindowHost()
{
    for (const auto &record : m_records) releaseView(record);
}
DocumentManager *WindowHost::documentManager() const { return m_documents; }
void WindowHost::setDocumentManager(DocumentManager *manager)
{
    if (m_documents == manager) return;
    if (!m_records.isEmpty()) {
        m_lastError = QStringLiteral("Cannot replace document manager while windows are registered");
        emit changed();
        return;
    }
    m_documents = manager;
    m_lastError.clear();
    emit changed();
}
int WindowHost::indexOf(const QString &id) const
{
    for (int i = 0; i < m_records.size(); ++i) if (m_records.at(i).id == id) return i;
    return -1;
}
int WindowHost::indexOf(const QQuickWindow *window) const
{
    for (int i = 0; i < m_records.size(); ++i) if (m_records.at(i).window == window) return i;
    return -1;
}
QVariantList WindowHost::windows() const
{
    QVariantList out;
    for (const auto &record : m_records) {
        if (!record.window) continue;
        auto *screen = record.window->screen();
        out << QVariantMap{{"id", record.id}, {"documentId", record.documentId},
                           {"viewId", record.viewId}, {"role", record.role},
                           {"screenId", screen ? screen->name() : QString()},
                           {"geometry", record.window->geometry()},
                           {"devicePixelRatio", record.window->devicePixelRatio()},
                           {"visible", record.window->isVisible()},
                           {"active", record.window->isActive()},
                           {"owned", record.owned}};
    }
    return out;
}
QString WindowHost::registerWindow(QQuickWindow *window, const QString &documentId,
                                   const QString &viewId, const QString &role)
{
    if (!window || indexOf(window) >= 0 || role.isEmpty()) {
        m_lastError = QStringLiteral("Invalid or already registered window");
        emit changed();
        return {};
    }
    QString assignedView = viewId;
    if (!documentId.isEmpty()) {
        if (!m_documents || !m_documents->findSession(documentId)) {
            m_lastError = QStringLiteral("Unknown document identity");
            emit changed();
            return {};
        }
        const QStringList knownViews = m_documents->viewsFor(documentId);
        if (assignedView.isEmpty()) {
            for (const QString &candidate : knownViews) {
                bool used = false;
                for (const auto &record : m_records)
                    if (record.documentId == documentId && record.viewId == candidate) {
                        used = true; break;
                    }
                if (!used) { assignedView = candidate; break; }
            }
            if (assignedView.isEmpty()) assignedView = m_documents->duplicateView(documentId);
        }
        if (!knownViews.contains(assignedView) && !m_documents->viewsFor(documentId).contains(assignedView)) {
            m_lastError = QStringLiteral("Unknown view identity");
            emit changed();
            return {};
        }
        for (const auto &record : m_records)
            if (record.documentId == documentId && record.viewId == assignedView) {
                m_lastError = QStringLiteral("View is already hosted");
                emit changed();
                return {};
            }
    } else if (!assignedView.isEmpty()) {
        m_lastError = QStringLiteral("View requires a document identity");
        emit changed();
        return {};
    }
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_records.append({id, documentId, assignedView, role, window, false});
    window->installEventFilter(this);
    connect(window, &QObject::destroyed, this, [this, id] {
        const int index = indexOf(id);
        if (index < 0) return;
        // AGENT-GUARD: a host view must be released even when an external
        // owner destroys its window without sending a native close event.
        releaseView(m_records.at(index));
        m_records.removeAt(index);
        updateActive();
        emit windowClosed(id);
        emit changed();
    });
    connect(window, &QWindow::screenChanged, this, [this] { emit changed(); });
    connect(window, &QWindow::xChanged, this, [this] { emit changed(); });
    connect(window, &QWindow::yChanged, this, [this] { emit changed(); });
    connect(window, &QWindow::widthChanged, this, [this] { emit changed(); });
    connect(window, &QWindow::heightChanged, this, [this] { emit changed(); });
    m_lastError.clear();
    emit changed();
    return id;
}
QString WindowHost::createWindow(const QString &documentId, const QString &viewId,
                                 const QString &role, QQuickWindow *transientParent)
{
    auto *native = new QQuickWindow;
    native->QObject::setParent(this);
    if (transientParent) native->setTransientParent(transientParent);
    const QString id = registerWindow(native, documentId, viewId, role);
    if (id.isEmpty()) { delete native; return {}; }
    m_records[indexOf(id)].owned = true;
    native->show();
    emit changed();
    return id;
}
QQuickWindow *WindowHost::window(const QString &id) const
{
    const int index = indexOf(id);
    return index < 0 ? nullptr : m_records.at(index).window;
}
void WindowHost::updateActive()
{
    QString active;
    for (const auto &record : m_records)
        if (record.window && record.window == QGuiApplication::focusWindow()) {
            active = record.id;
            break;
        }
    if (m_activeId == active) return;
    m_activeId = active;
    emit changed();
}
bool WindowHost::activate(const QString &id)
{
    auto *target = window(id);
    if (!target) { m_lastError = QStringLiteral("Unknown window"); emit changed(); return false; }
    target->show();
    target->raise();
    target->requestActivate();
    m_lastError.clear();
    emit changed();
    return true;
}
bool WindowHost::mayClose(const Record &record, bool discardUnsaved)
{
    if (!m_documents || record.documentId.isEmpty()) return true;
    auto *session = m_documents->findSession(record.documentId);
    if (!session) return true;
    if (m_documents->viewsFor(record.documentId).size() > 1) return true;
    if (session->isSaving()) {
        m_lastError = QStringLiteral("Save is still in progress");
        emit closeBlocked(record.id, m_lastError);
        emit changed();
        return false;
    }
    if (!session->isDirty()) return true;
    if (discardUnsaved) return true;
    m_lastError = QStringLiteral("Unsaved document requires a close decision");
    emit closeBlocked(record.id, m_lastError);
    emit changed();
    return false;
}
void WindowHost::releaseView(const Record &record, bool discardUnsaved)
{
    if (!m_documents || record.documentId.isEmpty() || record.viewId.isEmpty()
        || !m_documents->findSession(record.documentId)) return;
    if (m_documents->viewsFor(record.documentId).size() > 1)
        m_documents->removeView(record.documentId, record.viewId);
    else
        m_documents->closeDocument(record.documentId, discardUnsaved
            ? DocumentManager::ClosePolicy::Discard : DocumentManager::ClosePolicy::Cancel);
}
bool WindowHost::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() != QEvent::Close || m_explicitClose) return QObject::eventFilter(watched, event);
    auto *native = qobject_cast<QQuickWindow *>(watched);
    const int index = indexOf(native);
    if (index < 0) return QObject::eventFilter(watched, event);
    if (!mayClose(m_records.at(index), false)) {
        event->ignore();
        return true;
    }
    const QString id = m_records.at(index).id;
    const QPointer<QQuickWindow> tracked(native);
    QTimer::singleShot(0, this, [this, tracked, id] {
        const int active = indexOf(id);
        if (active >= 0 && tracked && !tracked->isVisible()) {
            forgetWindow(tracked);
        }
    });
    return QObject::eventFilter(watched, event);
}
bool WindowHost::closeWindow(const QString &id, bool discardUnsaved)
{
    const int index = indexOf(id);
    if (index < 0 || !m_records.at(index).window) {
        m_lastError = QStringLiteral("Unknown window"); emit changed(); return false;
    }
    const Record record = m_records.at(index);
    if (!mayClose(record, discardUnsaved)) return false;
    m_explicitClose = true;
    const bool accepted = record.window->close();
    m_explicitClose = false;
    if (!accepted) {
        m_lastError = QStringLiteral("Window close was rejected"); emit changed(); return false;
    }
    record.window->removeEventFilter(this);
    releaseView(record, discardUnsaved);
    m_records.removeAt(index);
    if (record.owned) record.window->deleteLater();
    m_lastError.clear();
    updateActive();
    emit windowClosed(id);
    emit changed();
    return true;
}
void WindowHost::forgetWindow(QQuickWindow *window)
{
    const int index = indexOf(window);
    if (index < 0) return;
    const Record record = m_records.at(index);
    const QString id = record.id;
    releaseView(record);
    m_records.removeAt(index);
    updateActive();
    emit windowClosed(id);
    emit changed();
}
bool WindowHost::moveWindow(const QString &id, const QRect &geometry, const QString &screenId)
{
    auto *native = window(id);
    if (!native || geometry.width() <= 0 || geometry.height() <= 0) {
        m_lastError = QStringLiteral("Invalid window or geometry"); emit changed(); return false;
    }
    QScreen *screen = nullptr;
    if (!screenId.isEmpty()) {
        for (auto *candidate : QGuiApplication::screens())
            if (candidate->name() == screenId) { screen = candidate; break; }
        if (!screen) {
            m_lastError = QStringLiteral("Requested screen is unavailable"); emit changed(); return false;
        }
    } else screen = native->screen() && QGuiApplication::screens().contains(native->screen())
                        ? native->screen() : QGuiApplication::primaryScreen();
    if (!screen) { m_lastError = QStringLiteral("No screen available"); emit changed(); return false; }
    const QRect available = screen->availableGeometry();
    const QSize size = geometry.size().boundedTo(available.size());
    const int x = qBound(available.left(), geometry.x(), available.right() - size.width() + 1);
    const int y = qBound(available.top(), geometry.y(), available.bottom() - size.height() + 1);
    native->setScreen(screen);
    native->setGeometry(QRect(QPoint(x, y), size));
    m_lastError.clear();
    emit changed();
    return true;
}
void WindowHost::reconcileScreens()
{
    auto *primary = QGuiApplication::primaryScreen();
    if (!primary) return;
    for (const auto &record : m_records) {
        if (!record.window) continue;
        auto *screen = record.window->screen();
        if (screen && QGuiApplication::screens().contains(screen)
            && screen->availableGeometry().intersects(record.window->geometry())) continue;
        moveWindow(record.id, record.window->geometry(), primary->name());
    }
}
QVariantMap WindowHost::capabilities() const
{
    return {{"nativeWindows", true}, {"logicalGeometry", true},
            {"multipleScreens", QGuiApplication::screens().size() > 1},
            {"crossPlatformScreenRestoreQualified", false}};
}
} // namespace QindaTK
