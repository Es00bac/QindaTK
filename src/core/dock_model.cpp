// SPDX-License-Identifier: LGPL-3.0-or-later
#include "dock_model.h"

#include "dock_model_p.h"

#include <algorithm>

namespace QindaTK {

using namespace DockModelDetail;

DockModel::DockModel(QObject *parent)
    : QObject(parent)
{
}

DockModel::~DockModel() = default;

QString DockModel::modeName(int mode)
{
    return modeToName(static_cast<Mode>(mode));
}

QString DockModel::zoneName(int zone)
{
    const QStringList names = zoneNames();
    return zone >= 0 && zone < names.size() ? names[zone] : names.first();
}

QStringList DockModel::zoneNames()
{
    return {QStringLiteral("left"), QStringLiteral("right"), QStringLiteral("top"),
            QStringLiteral("bottom"), QStringLiteral("center"), QStringLiteral("overlay")};
}

// ---- registry ------------------------------------------------------------

void DockModel::registerPanel(const QString &workspace, const QString &panelId,
                              const QVariantMap &definition)
{
    if (workspace.isEmpty() || panelId.isEmpty()) {
        return;
    }
    Workspace &ws = m_workspaces[workspace];
    const bool existed = ws.contains(panelId);
    PanelState &p = ws[panelId];
    p.title = definition.value(QStringLiteral("title"), panelId).toString();
    p.defaultMode = modeFromName(definition.value(QStringLiteral("mode")).toString(), Docked);
    p.defaultZone = definition.value(QStringLiteral("zone"),
                                     definition.value(QStringLiteral("dockZone"), QStringLiteral("right"))).toString();
    p.defaultLane = definition.value(QStringLiteral("lane"),
                                     definition.value(QStringLiteral("column"), 0)).toInt();
    p.defaultOrder = definition.value(QStringLiteral("order"), existed ? p.defaultOrder : ws.size() - 1).toInt();
    p.defaultExtent = definition.value(QStringLiteral("extent"),
                                       definition.value(QStringLiteral("dockedExtent"), 280)).toDouble();
    p.defaultShare = std::max(0.05, definition.value(QStringLiteral("share"), 1.0).toDouble());
    const QVariantMap rect = definition.value(QStringLiteral("floatingRect")).toMap();
    if (!rect.isEmpty()) {
        p.defaultFloatingRect = QRectF(rect.value(QStringLiteral("x"), 80).toDouble(),
                                       rect.value(QStringLiteral("y"), 80).toDouble(),
                                       rect.value(QStringLiteral("width"), 320).toDouble(),
                                       rect.value(QStringLiteral("height"), 400).toDouble());
    }
    p.minimumSize = QSizeF(definition.value(QStringLiteral("minWidth"), 160).toDouble(),
                           definition.value(QStringLiteral("minHeight"), 100).toDouble());
    p.fixedSize = definition.value(QStringLiteral("fixedSize"), false).toBool();
    p.closable = definition.value(QStringLiteral("closable"), true).toBool();
    p.chrome = definition.value(QStringLiteral("chrome"), QStringLiteral("default")).toString();
    if (definition.contains(QStringLiteral("allowedZones"))) {
        p.allowedZones = definition.value(QStringLiteral("allowedZones")).toStringList();
        p.zonesDeclared = true;
    }
    p.defaultGroup = definition.value(QStringLiteral("group")).toString();
    p.defaultGroupActive = definition.value(QStringLiteral("groupActive"), true).toBool();
    if (!existed) {
        applyDefaults(p);
        const auto pending = m_pending.constFind(workspace);
        if (pending != m_pending.cend() && pending->contains(panelId)) {
            applyArrangement(p, pending->value(panelId));
        }
    }
    if (workspace == m_workspace) {
        ++m_generation;
        emit layoutChanged();
    }
}

void DockModel::unregisterPanel(const QString &workspace, const QString &panelId)
{
    auto ws = m_workspaces.find(workspace);
    if (ws == m_workspaces.end() || ws->remove(panelId) == 0) {
        return;
    }
    if (workspace == m_workspace) {
        ++m_generation;
        emit layoutChanged();
    }
}

bool DockModel::hasPanel(const QString &panelId) const
{
    return state(panelId) != nullptr;
}

QStringList DockModel::panelIds() const
{
    const Workspace *ws = activeWorkspace();
    if (ws == nullptr) {
        return {};
    }
    QStringList ids = ws->keys();
    std::sort(ids.begin(), ids.end());
    return ids;
}

void DockModel::setWorkspace(const QString &workspace)
{
    if (workspace.isEmpty() || workspace == m_workspace) {
        return;
    }
    m_workspace = workspace;
    ++m_generation;
    emit workspaceChanged();
    emit layoutChanged();
    emit savedLayoutsChanged();
}

DockModel::Workspace *DockModel::activeWorkspace()
{
    const auto it = m_workspaces.find(m_workspace);
    return it == m_workspaces.end() ? nullptr : &it.value();
}

const DockModel::Workspace *DockModel::activeWorkspace() const
{
    const auto it = m_workspaces.constFind(m_workspace);
    return it == m_workspaces.cend() ? nullptr : &it.value();
}

DockModel::PanelState *DockModel::state(const QString &panelId)
{
    Workspace *ws = activeWorkspace();
    if (ws == nullptr) {
        return nullptr;
    }
    const auto it = ws->find(panelId);
    return it == ws->end() ? nullptr : &it.value();
}

const DockModel::PanelState *DockModel::state(const QString &panelId) const
{
    const Workspace *ws = activeWorkspace();
    if (ws == nullptr) {
        return nullptr;
    }
    const auto it = ws->constFind(panelId);
    return it == ws->cend() ? nullptr : &it.value();
}

int DockModel::nextZOrder()
{
    return ++m_zCounter;
}

void DockModel::touched()
{
    if (m_restoring) {
        return;
    }
    ++m_generation;
    emit layoutChanged();
    persist();
}

// ---- presentation --------------------------------------------------------

void DockModel::setMode(const QString &panelId, const QString &mode)
{
    const PanelState *p = state(panelId);
    if (p == nullptr) {
        return;
    }
    switch (modeFromName(mode, p->mode)) {
    case Docked:
        if (p->mode == Collapsed) {
            collapse(panelId, false);
        } else {
            dock(panelId, p->zone, p->lane);
        }
        break;
    case Floating:
        floatPanel(panelId);
        break;
    case Collapsed:
        collapse(panelId, true);
        break;
    case Hidden:
        hide(panelId);
        break;
    case Detached:
        detachPanel(panelId);
        break;
    }
}

void DockModel::dock(const QString &panelId, const QString &zone, int lane)
{
    PanelState *p = state(panelId);
    if (p == nullptr || !acceptsZone(panelId, zone)) {
        return;
    }
    const bool wasHidden = p->mode == Hidden;
    const bool moved = p->zone != zone || (lane >= 0 && lane != p->lane) || (p->mode != Docked && p->mode != Collapsed);
    if (p->zone != zone) {
        p->group.clear();
        p->lane = 0;
    }
    if (lane >= 0) {
        p->lane = lane;
    }
    if (moved) {
        p->group.clear();
        int maxOrder = -1;
        for (const QString &id : laneOrder(zone, p->lane)) {
            if (id != panelId) {
                maxOrder = std::max(maxOrder, state(id)->order);
            }
        }
        p->order = maxOrder + 1;
    }
    p->mode = (p->mode == Collapsed && !moved) ? Collapsed : Docked;
    p->zone = zone;
    renumberLane(zone, p->lane);
    touched();
    if (wasHidden) {
        emit panelShown(panelId);
    }
}

void DockModel::floatPanel(const QString &panelId, double x, double y)
{
    PanelState *p = state(panelId);
    if (p == nullptr) {
        return;
    }
    const bool wasHidden = p->mode == Hidden;
    const QString oldZone = p->zone;
    const int oldLane = p->lane;
    p->mode = Floating;
    p->group.clear();
    if (x >= 0 && y >= 0) {
        p->floatingRect.moveTo(x, y);
    }
    p->zOrder = nextZOrder();
    renumberLane(oldZone, oldLane);
    touched();
    if (wasHidden) {
        emit panelShown(panelId);
    }
}

void DockModel::detachPanel(const QString &panelId, const QRectF &globalRect)
{
    PanelState *p = state(panelId);
    if (!p) return;
    if (p->mode == Detached && !globalRect.isValid()) return;
    const QString oldZone = p->zone;
    const int oldLane = p->lane;
    p->mode = Detached;
    p->group.clear();
    if (globalRect.isValid() && globalRect.width() > 0 && globalRect.height() > 0)
        p->floatingRect = globalRect;
    renumberLane(oldZone, oldLane);
    touched();
}

void DockModel::setDetachedScreen(const QString &panelId, const QString &screenId)
{
    PanelState *p = state(panelId);
    if (!p || p->mode != Detached || p->detachedScreenId == screenId) return;
    p->detachedScreenId = screenId;
    touched();
}

void DockModel::hide(const QString &panelId)
{
    PanelState *p = state(panelId);
    if (p == nullptr || p->mode == Hidden) {
        return;
    }
    const QString zone = p->zone;
    const int lane = p->lane;
    p->mode = Hidden;
    renumberLane(zone, lane);
    touched();
}

void DockModel::show(const QString &panelId)
{
    PanelState *p = state(panelId);
    if (p == nullptr) {
        return;
    }
    if (p->mode == Hidden) {
        p->mode = Docked;
        if (p->group.isEmpty()) {
            int maxOrder = -1;
            for (const QString &id : laneOrder(p->zone, p->lane)) {
                maxOrder = std::max(maxOrder, state(id)->order);
            }
            p->order = maxOrder + 1;
        }
        renumberLane(p->zone, p->lane);
    } else if (p->mode == Collapsed) {
        p->mode = Docked;
    }
    if (!p->group.isEmpty()) {
        activateInGroup(panelId);
    } else {
        touched();
    }
    emit panelShown(panelId);
}

void DockModel::collapse(const QString &panelId, bool collapsed)
{
    PanelState *p = state(panelId);
    if (p == nullptr) {
        return;
    }
    if (collapsed && p->mode == Docked) {
        p->mode = Collapsed;
    } else if (!collapsed && p->mode == Collapsed) {
        p->mode = Docked;
    } else {
        return;
    }
    touched();
}

void DockModel::toggle(const QString &panelId)
{
    const PanelState *p = state(panelId);
    if (p == nullptr) {
        return;
    }
    if (p->mode == Hidden) {
        show(panelId);
    } else {
        hide(panelId);
    }
}

} // namespace QindaTK
