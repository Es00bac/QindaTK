// SPDX-License-Identifier: LGPL-3.0-or-later
#include "detached_panel_host.h"
#include "dock_model.h"
#include "window_host.h"

#include <QQuickWindow>
#include <QScreen>
#include <QTimer>
#include <algorithm>

namespace QindaTK {
namespace {
bool belongsTo(QQuickItem *item, QQuickItem *ancestor)
{
    for (; item; item = item->parentItem())
        if (item == ancestor) return true;
    return false;
}
} // namespace

DetachedPanelHost::DetachedPanelHost(QObject *parent) : QObject(parent) {}

DetachedPanelHost::~DetachedPanelHost()
{
    for (const QString &id : m_entries.keys()) reclaim(id);
}

bool DetachedPanelHost::fail(const QString &message)
{
    m_lastError = message;
    emit changed();
    return false;
}

void DetachedPanelHost::setModel(DockModel *model)
{
    if (m_model == model) return;
    for (const QString &id : m_entries.keys()) reclaim(id);
    if (m_model) disconnect(m_model, nullptr, this, nullptr);
    m_model = model;
    if (model) {
        connect(model, &DockModel::layoutChanged, this, &DetachedPanelHost::queueSync);
        connect(model, &DockModel::workspaceChanged, this, [this] {
            for (const QString &id : m_entries.keys()) reclaim(id);
            queueSync();
        });
        connect(model, &QObject::destroyed, this, [this] {
            for (const QString &id : m_entries.keys()) reclaim(id);
        });
    }
    emit configurationChanged();
    queueSync();
}

void DetachedPanelHost::setWindowHost(WindowHost *host)
{
    if (m_windowHost == host) return;
    for (const QString &id : m_entries.keys()) reclaim(id);
    if (m_windowHost) disconnect(m_windowHost, nullptr, this, nullptr);
    m_windowHost = host;
    if (host) {
        connect(host, &QObject::destroyed, this, [this] {
            for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
                it->windowId.clear();
                if (it->content && it->home) {
                    it->content->setParentItem(it->home);
                    it->content->setVisible(true);
                    restoreFocus(*it);
                }
            }
            emit changed();
        });
        connect(host, &WindowHost::windowClosed, this, [this](const QString &closedId) {
            for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
                if (it->windowId != closedId) continue;
                const QString panelId = it.key();
                it->windowId.clear();
                if (it->content && it->home) {
                    it->content->setParentItem(it->home);
                    it->content->setVisible(true);
                    restoreFocus(*it);
                }
                if (m_model && m_model->panel(panelId).value(QStringLiteral("detached")).toBool()) {
                    const QString zone = m_model->panel(panelId).value(QStringLiteral("zone"),
                        QStringLiteral("right")).toString();
                    m_model->dock(panelId, zone);
                }
                emit panelReattached(panelId);
                emit changed();
                break;
            }
        });
    }
    emit configurationChanged();
    queueSync();
}

QVariantList DetachedPanelHost::windows() const
{
    QVariantList result;
    for (auto it = m_entries.cbegin(); it != m_entries.cend(); ++it) {
        if (it->windowId.isEmpty()) continue;
        result.append(QVariantMap{{QStringLiteral("panelId"), it.key()},
                                  {QStringLiteral("windowId"), it->windowId}});
    }
    return result;
}

QString DetachedPanelHost::windowId(const QString &panelId) const
{
    return m_entries.value(panelId).windowId;
}

bool DetachedPanelHost::registerContent(const QString &panelId, QQuickItem *content)
{
    if (panelId.isEmpty() || !content || !content->parentItem())
        return fail(QStringLiteral("Panel content requires an identity and home item"));
    if (m_entries.contains(panelId) && m_entries.value(panelId).content == content) return true;
    unregisterContent(panelId);
    m_entries.insert(panelId, {content, content->parentItem(), {}, {}, false});
    connect(content, &QObject::destroyed, this, [this, panelId] {
        auto it = m_entries.find(panelId);
        if (it == m_entries.end()) return;
        const QString id = it->windowId;
        m_entries.erase(it);
        if (m_windowHost && !id.isEmpty()) m_windowHost->closeWindow(id);
        emit changed();
    });
    m_lastError.clear();
    queueSync();
    return true;
}

void DetachedPanelHost::unregisterContent(const QString &panelId)
{
    if (!m_entries.contains(panelId)) return;
    reclaim(panelId);
    m_entries.remove(panelId);
    emit changed();
}

bool DetachedPanelHost::detach(const QString &panelId)
{
    if (!m_model || !m_windowHost || !m_entries.contains(panelId)
        || !m_entries.value(panelId).content || !m_model->hasPanel(panelId))
        return fail(QStringLiteral("Panel, content, model or window host is unavailable"));
    if (!m_entries.value(panelId).windowId.isEmpty()) {
        m_windowHost->activate(m_entries.value(panelId).windowId);
        return true;
    }
    m_model->detachPanel(panelId);
    queueSync();
    return true;
}

bool DetachedPanelHost::reattach(const QString &panelId)
{
    if (!m_model || !m_model->hasPanel(panelId))
        return fail(QStringLiteral("Unknown panel"));
    reclaim(panelId);
    const QString zone = m_model->panel(panelId).value(QStringLiteral("zone"),
        QStringLiteral("right")).toString();
    m_model->dock(panelId, zone);
    emit panelReattached(panelId);
    return true;
}

void DetachedPanelHost::queueSync()
{
    if (m_syncScheduled) return;
    m_syncScheduled = true;
    QTimer::singleShot(0, this, [this] {
        m_syncScheduled = false;
        synchronize();
    });
}

void DetachedPanelHost::reclaim(const QString &panelId)
{
    auto it = m_entries.find(panelId);
    if (it == m_entries.end()) return;
    const QString id = it->windowId;
    it->windowId.clear();
    rememberFocus(*it);
    if (it->content && it->home) {
        it->content->setParentItem(it->home);
        it->content->setVisible(true);
        restoreFocus(*it);
    }
    if (m_windowHost && !id.isEmpty()) m_windowHost->closeWindow(id);
    emit changed();
}

void DetachedPanelHost::rememberFocus(Entry &entry)
{
    if (!entry.content || !entry.content->window()) return;
    auto *focused = entry.content->window()->activeFocusItem();
    if (belongsTo(focused, entry.content)) entry.focusItem = focused;
}

void DetachedPanelHost::restoreFocus(const Entry &entry)
{
    QPointer<QQuickItem> target = entry.focusItem;
    if (!target || !entry.content || !belongsTo(target, entry.content)) return;
    QTimer::singleShot(0, this, [target] {
        if (target && target->window()) target->forceActiveFocus(Qt::OtherFocusReason);
    });
}

void DetachedPanelHost::saveGeometry(const QString &panelId)
{
    auto it = m_entries.find(panelId);
    if (it == m_entries.end()) return;
    it->geometryScheduled = false;
    if (!m_model || !m_windowHost || it->windowId.isEmpty()
        || !m_model->panel(panelId).value(QStringLiteral("detached")).toBool()) return;
    auto *window = m_windowHost->window(it->windowId);
    if (!window) return;
    const QRect bounds = window->geometry();
    if (it->content) it->content->setSize(bounds.size());
    const auto info = m_model->panel(panelId);
    if (info.value(QStringLiteral("x")).toInt() != bounds.x()
        || info.value(QStringLiteral("y")).toInt() != bounds.y()
        || info.value(QStringLiteral("width")).toInt() != bounds.width()
        || info.value(QStringLiteral("height")).toInt() != bounds.height())
        m_model->setFloatingRect(panelId, bounds.x(), bounds.y(), bounds.width(), bounds.height());
    const QString screenId = window->screen() ? window->screen()->name() : QString{};
    m_model->setDetachedScreen(panelId, screenId);
}

bool DetachedPanelHost::open(const QString &panelId)
{
    if (!m_model || !m_windowHost) return false;
    auto it = m_entries.find(panelId);
    if (it == m_entries.end() || !it->content) return false;
    const auto info = m_model->panel(panelId);
    const QString id = m_windowHost->createWindow({}, {}, QStringLiteral("panel"));
    if (id.isEmpty()) return fail(m_windowHost->lastError());
    auto *window = m_windowHost->window(id);
    if (!window) return fail(QStringLiteral("Native panel window was not created"));
    it->windowId = id;
    window->setObjectName(QStringLiteral("detachedPanelWindow_") + panelId);
    window->setTitle(info.value(QStringLiteral("title"), panelId).toString());
    window->setMinimumSize(QSize(info.value(QStringLiteral("minWidth"), 160).toInt(),
                                 info.value(QStringLiteral("minHeight"), 100).toInt()));
    const QRect wanted(info.value(QStringLiteral("x"), 80).toInt(),
                       info.value(QStringLiteral("y"), 80).toInt(),
                       info.value(QStringLiteral("width"), 320).toInt(),
                       info.value(QStringLiteral("height"), 400).toInt());
    if (!m_windowHost->moveWindow(id, wanted,
            info.value(QStringLiteral("detachedScreenId")).toString()))
        m_windowHost->moveWindow(id, wanted);
    rememberFocus(*it);
    it->content->setParentItem(nullptr);
    it->content->setParentItem(window->contentItem());
    it->content->setPosition({0, 0});
    it->content->setSize(window->size());
    it->content->setVisible(true);
    const auto geometryChanged = [this, panelId] {
        auto entry = m_entries.find(panelId);
        if (entry == m_entries.end() || entry->geometryScheduled) return;
        entry->geometryScheduled = true;
        QTimer::singleShot(0, this, [this, panelId] { saveGeometry(panelId); });
    };
    connect(window, &QWindow::xChanged, this, geometryChanged);
    connect(window, &QWindow::yChanged, this, geometryChanged);
    connect(window, &QWindow::widthChanged, this, geometryChanged);
    connect(window, &QWindow::heightChanged, this, geometryChanged);
    connect(window, &QWindow::screenChanged, this, geometryChanged);
    connect(window, &QQuickWindow::activeFocusItemChanged, this, [this, panelId] {
        auto entry = m_entries.find(panelId);
        if (entry != m_entries.end()) rememberFocus(*entry);
    });
    window->show();
    window->requestActivate();
    restoreFocus(*it);
    m_lastError.clear();
    emit panelDetached(panelId);
    emit changed();
    return true;
}

void DetachedPanelHost::synchronize()
{
    if (m_synchronizing || !m_model || !m_windowHost) return;
    m_synchronizing = true;
    for (const QString &id : m_entries.keys()) {
        const bool detached = m_model->panel(id).value(QStringLiteral("detached")).toBool();
        const bool openWindow = !m_entries.value(id).windowId.isEmpty();
        if (detached && !openWindow) open(id);
        else if (!detached && openWindow) reclaim(id);
    }
    m_synchronizing = false;
}

void DetachedPanelHost::reclaimDocked()
{
    if (!m_model) return;
    for (const QString &id : m_entries.keys()) {
        if (!m_entries.value(id).windowId.isEmpty()
            && !m_model->panel(id).value(QStringLiteral("detached")).toBool())
            reclaim(id);
    }
}
} // namespace QindaTK
