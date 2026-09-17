// SPDX-License-Identifier: LGPL-3.0-or-later
#include "dock_model.h"

#include "dock_model_p.h"

#include <algorithm>

namespace QindaTK {

using namespace DockModelDetail;

// ---- geometry ------------------------------------------------------------

void DockModel::moveFloating(const QString &panelId, double x, double y)
{
    PanelState *p = state(panelId);
    if (p == nullptr) {
        return;
    }
    p->floatingRect.moveTo(x, y);
    touched();
}

void DockModel::resizeFloating(const QString &panelId, double width, double height)
{
    PanelState *p = state(panelId);
    if (p == nullptr || p->fixedSize) {
        return;
    }
    p->floatingRect.setSize(QSizeF(std::max(width, p->minimumSize.width()),
                                   std::max(height, p->minimumSize.height())));
    touched();
}

void DockModel::setFloatingRect(const QString &panelId, double x, double y, double width, double height)
{
    PanelState *p = state(panelId);
    if (p == nullptr) {
        return;
    }
    p->floatingRect.moveTo(x, y);
    if (!p->fixedSize) {
        p->floatingRect.setSize(QSizeF(std::max(width, p->minimumSize.width()),
                                       std::max(height, p->minimumSize.height())));
    }
    touched();
}

void DockModel::resizeDocked(const QString &panelId, double extent)
{
    const PanelState *p = state(panelId);
    if (p == nullptr) {
        return;
    }
    setLaneExtent(p->zone, p->lane, extent);
}

void DockModel::setLaneExtent(const QString &zone, int lane, double extent)
{
    const QStringList ids = laneOrder(zone, lane);
    if (ids.isEmpty()) {
        return;
    }
    // A lane never shrinks below the largest minimum its panels declared.
    qreal floor = 0;
    for (const QString &id : ids) {
        const PanelState &s = *state(id);
        floor = std::max(floor, isSideZone(zone) ? s.minimumSize.width() : s.minimumSize.height());
    }
    const qreal clamped = std::max<qreal>(extent, floor);
    for (const QString &id : ids) {
        state(id)->extent = clamped;
    }
    touched();
}

void DockModel::setShare(const QString &panelId, double share)
{
    PanelState *p = state(panelId);
    if (p == nullptr) {
        return;
    }
    const qreal clamped = std::max(0.05, share);
    // Group members share one slot, so they share one weight.
    if (!p->group.isEmpty()) {
        for (const QString &id : laneOrder(p->zone, p->lane)) {
            if (state(id)->group == p->group) {
                state(id)->share = clamped;
            }
        }
    } else {
        p->share = clamped;
    }
    touched();
}

void DockModel::bringToFront(const QString &panelId)
{
    PanelState *p = state(panelId);
    if (p == nullptr || p->mode != Floating) {
        return;
    }
    if (p->zOrder == m_zCounter) {
        return;
    }
    p->zOrder = nextZOrder();
    touched();
}

// ---- lanes and groups ----------------------------------------------------

// Orders become 0..n-1 in the lane with tab-group members contiguous, so
// insertion by index and "the slot after this one" are well defined.
void DockModel::renumberLane(const QString &zone, int lane)
{
    const QStringList ids = laneOrder(zone, lane);
    QStringList placed;
    for (const QString &id : ids) {
        if (placed.contains(id)) {
            continue;
        }
        const QString group = state(id)->group;
        placed.append(id);
        if (group.isEmpty()) {
            continue;
        }
        for (const QString &other : ids) {
            if (other != id && state(other)->group == group && !placed.contains(other)) {
                placed.append(other);
            }
        }
    }
    for (int i = 0; i < placed.size(); ++i) {
        state(placed[i])->order = i;
    }
}

void DockModel::groupWith(const QString &panelId, const QString &targetPanelId)
{
    PanelState *p = state(panelId);
    PanelState *t = state(targetPanelId);
    if (p == nullptr || t == nullptr || p == t || (t->mode != Docked && t->mode != Collapsed)) {
        return;
    }
    if (!acceptsZone(panelId, t->zone)) {
        return;
    }
    const QString oldZone = p->zone;
    const int oldLane = p->lane;
    if (t->group.isEmpty()) {
        t->group = QStringLiteral("group:") + targetPanelId;
    }
    const QString group = t->group;
    for (const QString &id : laneOrder(t->zone, t->lane)) {
        if (state(id)->group == group) {
            state(id)->groupActive = false;
        }
    }
    p->mode = t->mode;
    p->zone = t->zone;
    p->lane = t->lane;
    p->group = group;
    p->groupActive = true;
    p->share = t->share;
    int maxOrder = t->order;
    for (const QString &id : laneOrder(t->zone, t->lane)) {
        if (state(id)->group == group) {
            maxOrder = std::max(maxOrder, state(id)->order);
        }
    }
    p->order = maxOrder + 1;
    if (oldZone != p->zone || oldLane != p->lane) {
        renumberLane(oldZone, oldLane);
    }
    renumberLane(p->zone, p->lane);
    touched();
}

void DockModel::ungroup(const QString &panelId)
{
    PanelState *p = state(panelId);
    if (p == nullptr || p->group.isEmpty()) {
        return;
    }
    const QString group = p->group;
    const bool wasActive = p->groupActive;
    p->group.clear();
    p->groupActive = true;
    QStringList remaining;
    for (const QString &id : laneOrder(p->zone, p->lane)) {
        if (id != panelId && state(id)->group == group) {
            remaining.append(id);
        }
    }
    if (remaining.size() == 1) {
        state(remaining.first())->group.clear();
        state(remaining.first())->groupActive = true;
    } else if (wasActive && !remaining.isEmpty()) {
        state(remaining.first())->groupActive = true;
    }
    int maxOrder = -1;
    for (const QString &id : laneOrder(p->zone, p->lane)) {
        maxOrder = std::max(maxOrder, state(id)->order);
    }
    p->order = maxOrder + 1;
    renumberLane(p->zone, p->lane);
    touched();
}

void DockModel::activateInGroup(const QString &panelId)
{
    PanelState *p = state(panelId);
    if (p == nullptr) {
        return;
    }
    if (p->group.isEmpty()) {
        touched();
        return;
    }
    for (const QString &id : laneOrder(p->zone, p->lane)) {
        if (state(id)->group == p->group) {
            state(id)->groupActive = id == panelId;
        }
    }
    touched();
}

void DockModel::moveInGroup(const QString &panelId, int index)
{
    PanelState *p = state(panelId);
    if (p == nullptr || p->group.isEmpty()) {
        return;
    }
    QStringList members;
    for (const QString &id : laneOrder(p->zone, p->lane)) {
        if (state(id)->group == p->group) {
            members.append(id);
        }
    }
    members.removeAll(panelId);
    members.insert(std::clamp<int>(index, 0, members.size()), panelId);
    const int base = state(members.first())->order;
    for (int i = 0; i < members.size(); ++i) {
        state(members[i])->order = base + i;
    }
    renumberLane(p->zone, p->lane);
    touched();
}

void DockModel::dockBeside(const QString &panelId, const QString &targetPanelId,
                           const QString &placement)
{
    PanelState *p = state(panelId);
    PanelState *t = state(targetPanelId);
    if (p == nullptr || t == nullptr || p == t || (t->mode != Docked && t->mode != Collapsed)) {
        return;
    }
    if (!acceptsZone(panelId, t->zone)) {
        return;
    }
    if (placement == QLatin1String("tab")) {
        groupWith(panelId, targetPanelId);
        return;
    }
    const bool wasHidden = p->mode == Hidden;
    const QString oldZone = p->zone;
    const int oldLane = p->lane;
    const QString zone = t->zone;
    p->group.clear();
    p->groupActive = true;
    p->mode = Docked;
    if (placement == QLatin1String("lane-before") || placement == QLatin1String("lane-after")) {
        const int newLane = t->lane + (placement == QLatin1String("lane-after") ? 1 : 0);
        for (const QString &id : laneOrder(zone, -1)) {
            PanelState *s = state(id);
            if (s != p && s->lane >= newLane) {
                ++s->lane;
            }
        }
        p->zone = zone;
        p->lane = newLane;
        p->order = 0;
        renumberLane(oldZone, oldLane);
        touched();
        if (wasHidden) emit panelShown(panelId);
        return;
    }
    // before / after: the slot next to the target's slot in its lane.
    QStringList slotOrder;
    for (const QString &id : laneOrder(zone, t->lane)) {
        if (id != panelId) {
            slotOrder.append(id);
        }
    }
    int index = slotOrder.indexOf(targetPanelId);
    if (index < 0) {
        index = slotOrder.size();
    } else if (placement == QLatin1String("after")) {
        const QString group = t->group;
        while (index + 1 < slotOrder.size() && !group.isEmpty() && state(slotOrder[index + 1])->group == group) {
            ++index;
        }
        ++index;
    } else {
        const QString group = t->group;
        while (index > 0 && !group.isEmpty() && state(slotOrder[index - 1])->group == group) {
            --index;
        }
    }
    slotOrder.insert(index, panelId);
    p->zone = zone;
    p->lane = t->lane;
    for (int i = 0; i < slotOrder.size(); ++i) {
        state(slotOrder[i])->order = i;
    }
    if (oldZone != zone || oldLane != t->lane) {
        renumberLane(oldZone, oldLane);
    }
    renumberLane(zone, t->lane);
    touched();
    if (wasHidden) emit panelShown(panelId);
}

} // namespace QindaTK
