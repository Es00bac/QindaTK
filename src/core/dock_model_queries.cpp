// SPDX-License-Identifier: LGPL-3.0-or-later
#include "dock_model.h"

#include "dock_model_p.h"

#include <algorithm>

namespace QindaTK {

using namespace DockModelDetail;

// ---- queries -------------------------------------------------------------

QVariantMap DockModel::toMap(const QString &panelId, const PanelState &p) const
{
    QVariantMap map = arrangementOf(p);
    map.insert(QStringLiteral("panelId"), panelId);
    map.insert(QStringLiteral("title"), p.title);
    map.insert(QStringLiteral("minWidth"), p.minimumSize.width());
    map.insert(QStringLiteral("minHeight"), p.minimumSize.height());
    map.insert(QStringLiteral("fixedSize"), p.fixedSize);
    map.insert(QStringLiteral("closable"), p.closable);
    map.insert(QStringLiteral("chrome"), p.chrome);
    map.insert(QStringLiteral("allowedZones"), p.zonesDeclared ? QVariant(p.allowedZones) : QVariant(zoneNames()));
    map.insert(QStringLiteral("x"), p.floatingRect.x());
    map.insert(QStringLiteral("y"), p.floatingRect.y());
    map.insert(QStringLiteral("width"), p.floatingRect.width());
    map.insert(QStringLiteral("height"), p.floatingRect.height());
    map.insert(QStringLiteral("docked"), p.mode == Docked);
    map.insert(QStringLiteral("floating"), p.mode == Floating);
    map.insert(QStringLiteral("collapsed"), p.mode == Collapsed);
    map.insert(QStringLiteral("hidden"), p.mode == Hidden);
    map.insert(QStringLiteral("detached"), p.mode == Detached);
    map.insert(QStringLiteral("detachedScreenId"), p.detachedScreenId);
    map.insert(QStringLiteral("visible"), p.mode != Hidden);
    return map;
}

QVariantList DockModel::panels() const
{
    const Workspace *ws = activeWorkspace();
    if (ws == nullptr) {
        return {};
    }
    QVector<QPair<QString, const PanelState *>> sorted;
    for (auto it = ws->cbegin(); it != ws->cend(); ++it) {
        sorted.append({it.key(), &it.value()});
    }
    std::sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b) {
        return std::tie(a.second->zone, a.second->lane, a.second->order, a.first)
               < std::tie(b.second->zone, b.second->lane, b.second->order, b.first);
    });
    QVariantList list;
    for (const auto &entry : sorted) {
        list.append(toMap(entry.first, *entry.second));
    }
    return list;
}

QVariantMap DockModel::panel(const QString &panelId) const
{
    const PanelState *p = state(panelId);
    return p == nullptr ? QVariantMap() : toMap(panelId, *p);
}

QStringList DockModel::laneOrder(const QString &zone, int lane) const
{
    const Workspace *ws = activeWorkspace();
    if (ws == nullptr) {
        return {};
    }
    QVector<QPair<QString, const PanelState *>> members;
    for (auto it = ws->cbegin(); it != ws->cend(); ++it) {
        const PanelState &p = it.value();
        if ((p.mode == Docked || p.mode == Collapsed) && p.zone == zone && (lane < 0 || p.lane == lane)) {
            members.append({it.key(), &p});
        }
    }
    std::sort(members.begin(), members.end(), [](const auto &a, const auto &b) {
        return std::tie(a.second->lane, a.second->order, a.first)
               < std::tie(b.second->lane, b.second->order, b.first);
    });
    QStringList ids;
    for (const auto &entry : members) {
        ids.append(entry.first);
    }
    return ids;
}

QVariantList DockModel::panelsInZone(const QString &zone) const
{
    QVariantList list;
    for (const QString &id : laneOrder(zone, -1)) {
        list.append(panel(id));
    }
    return list;
}

QVariantList DockModel::lanes(const QString &zone) const
{
    QVariantList result;
    const QStringList ids = laneOrder(zone, -1);
    if (ids.isEmpty()) {
        return result;
    }
    int currentLane = -1;
    QVariantList laneSlots;
    qreal extent = 0;
    QStringList seenGroups;
    const auto flush = [&]() {
        if (currentLane < 0) {
            return;
        }
        result.append(QVariantMap{{QStringLiteral("lane"), currentLane},
                                  {QStringLiteral("extent"), extent},
                                  {QStringLiteral("slots"), laneSlots}});
        laneSlots.clear();
        extent = 0;
        seenGroups.clear();
    };
    for (const QString &id : ids) {
        const PanelState &p = *state(id);
        if (p.lane != currentLane) {
            flush();
            currentLane = p.lane;
        }
        extent = std::max(extent, p.extent);
        if (!p.group.isEmpty() && seenGroups.contains(p.group)) {
            continue;
        }
        QStringList members;
        QString active;
        bool collapsed = p.mode == Collapsed;
        qreal share = p.share;
        if (p.group.isEmpty()) {
            members.append(id);
            active = id;
        } else {
            seenGroups.append(p.group);
            for (const QString &other : ids) {
                const PanelState &o = *state(other);
                if (o.group == p.group && o.lane == p.lane) {
                    members.append(other);
                    if (o.groupActive && active.isEmpty()) {
                        active = other;
                    }
                }
            }
            if (active.isEmpty()) {
                active = members.first();
            }
            collapsed = state(active)->mode == Collapsed;
            share = state(active)->share;
        }
        laneSlots.append(QVariantMap{{QStringLiteral("panelIds"), members},
                                 {QStringLiteral("activeId"), active},
                                 {QStringLiteral("share"), share},
                                 {QStringLiteral("collapsed"), collapsed}});
    }
    flush();
    return result;
}

qreal DockModel::zoneExtent(const QString &zone) const
{
    qreal total = 0;
    for (const QVariant &lane : lanes(zone)) {
        total += lane.toMap().value(QStringLiteral("extent")).toDouble();
    }
    return total;
}

QVariantList DockModel::floatingPanels() const
{
    const Workspace *ws = activeWorkspace();
    if (ws == nullptr) {
        return {};
    }
    QVector<QPair<QString, const PanelState *>> floating;
    for (auto it = ws->cbegin(); it != ws->cend(); ++it) {
        if (it.value().mode == Floating) {
            floating.append({it.key(), &it.value()});
        }
    }
    std::sort(floating.begin(), floating.end(), [](const auto &a, const auto &b) {
        return a.second->zOrder < b.second->zOrder;
    });
    QVariantList list;
    for (const auto &entry : floating) {
        list.append(toMap(entry.first, *entry.second));
    }
    return list;
}

QVariantList DockModel::detachedPanels() const
{
    QVariantList result;
    const Workspace *ws = activeWorkspace();
    if (!ws) return result;
    QStringList ids;
    for (auto it = ws->cbegin(); it != ws->cend(); ++it)
        if (it.value().mode == Detached) ids.append(it.key());
    std::sort(ids.begin(), ids.end());
    for (const QString &id : ids) result.append(toMap(id, ws->value(id)));
    return result;
}

bool DockModel::acceptsZone(const QString &panelId, const QString &zone) const
{
    const PanelState *p = state(panelId);
    if (p == nullptr || !zoneNames().contains(zone)) {
        return false;
    }
    return !p->zonesDeclared || p->allowedZones.contains(zone);
}

} // namespace QindaTK
