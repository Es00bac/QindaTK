// SPDX-License-Identifier: LGPL-3.0-or-later
#include "dock_model.h"

#include "dock_model_p.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>

#include <algorithm>

namespace QindaTK {

using namespace DockModelDetail;

// AGENT-CONTRACT: the persisted arrangement of one panel. Keys are stable
// across releases; add keys, never rename them, and read absent keys as
// "keep the current value". docs/docking.md documents the schema.
QVariantMap DockModel::arrangementOf(const PanelState &p) const
{
    return {
        {QStringLiteral("mode"), modeToName(p.mode)},
        {QStringLiteral("zone"), p.zone},
        {QStringLiteral("lane"), p.lane},
        {QStringLiteral("order"), p.order},
        {QStringLiteral("extent"), p.extent},
        {QStringLiteral("share"), p.share},
        {QStringLiteral("zOrder"), p.zOrder},
        {QStringLiteral("group"), p.group},
        {QStringLiteral("groupActive"), p.groupActive},
        {QStringLiteral("floatingRect"), QVariantMap{{QStringLiteral("x"), p.floatingRect.x()},
                                                     {QStringLiteral("y"), p.floatingRect.y()},
                                                     {QStringLiteral("width"), p.floatingRect.width()},
                                                     {QStringLiteral("height"), p.floatingRect.height()}}},
    };
}

void DockModel::applyArrangement(PanelState &p, const QVariantMap &values)
{
    if (values.contains(QStringLiteral("mode"))) {
        p.mode = modeFromName(values.value(QStringLiteral("mode")).toString(), p.mode);
    }
    const QString zone = values.value(QStringLiteral("zone"),
                                      values.value(QStringLiteral("dockZone"))).toString();
    if (zoneNames().contains(zone) && (!p.zonesDeclared || p.allowedZones.contains(zone))) {
        p.zone = zone;
    }
    if (values.contains(QStringLiteral("lane"))) p.lane = std::max(0, values.value(QStringLiteral("lane")).toInt());
    if (values.contains(QStringLiteral("order"))) p.order = values.value(QStringLiteral("order")).toInt();
    if (values.contains(QStringLiteral("extent"))) p.extent = std::max(0.0, values.value(QStringLiteral("extent")).toDouble());
    if (values.contains(QStringLiteral("share"))) p.share = std::max(0.05, values.value(QStringLiteral("share")).toDouble());
    if (values.contains(QStringLiteral("zOrder"))) {
        p.zOrder = values.value(QStringLiteral("zOrder")).toInt();
        m_zCounter = std::max(m_zCounter, p.zOrder);
    }
    if (values.contains(QStringLiteral("group"))) p.group = values.value(QStringLiteral("group")).toString();
    if (values.contains(QStringLiteral("groupActive"))) p.groupActive = values.value(QStringLiteral("groupActive")).toBool();
    const QVariantMap rect = values.value(QStringLiteral("floatingRect")).toMap();
    if (!rect.isEmpty()) {
        QRectF r(rect.value(QStringLiteral("x"), p.floatingRect.x()).toDouble(),
                 rect.value(QStringLiteral("y"), p.floatingRect.y()).toDouble(),
                 rect.value(QStringLiteral("width"), p.floatingRect.width()).toDouble(),
                 rect.value(QStringLiteral("height"), p.floatingRect.height()).toDouble());
        if (p.fixedSize) {
            r.setSize(p.floatingRect.size());
        }
        r.setWidth(std::max(r.width(), p.minimumSize.width()));
        r.setHeight(std::max(r.height(), p.minimumSize.height()));
        p.floatingRect = r;
    }
}

void DockModel::applyDefaults(PanelState &p)
{
    p.mode = p.defaultMode;
    p.zone = p.defaultZone;
    p.lane = p.defaultLane;
    p.order = p.defaultOrder;
    p.extent = p.defaultExtent;
    p.share = p.defaultShare;
    p.floatingRect = p.defaultFloatingRect;
    p.group = p.defaultGroup;
    p.groupActive = p.defaultGroupActive;
    p.zOrder = nextZOrder();
}

// ---- presets -------------------------------------------------------------

void DockModel::reset()
{
    Workspace *ws = activeWorkspace();
    if (ws == nullptr) {
        return;
    }
    for (PanelState &p : *ws) {
        applyDefaults(p);
    }
    touched();
}

void DockModel::registerPreset(const QString &workspace, const QString &presetId,
                               const QVariantMap &statesByPanelId)
{
    QVariantMap normalized;
    for (auto it = statesByPanelId.cbegin(); it != statesByPanelId.cend(); ++it) {
        // A bare mode string is accepted for PanelLayoutController parity.
        if (it.value().typeId() == QMetaType::QString) {
            normalized.insert(it.key(), QVariantMap{{QStringLiteral("mode"), it.value()}});
        } else {
            normalized.insert(it.key(), it.value().toMap());
        }
    }
    m_presets[workspace].insert(presetId, normalized);
    if (workspace == m_workspace) {
        ++m_generation;
        emit layoutChanged();
    }
}

void DockModel::applyPreset(const QString &presetId)
{
    Workspace *ws = activeWorkspace();
    const auto presets = m_presets.constFind(m_workspace);
    if (ws == nullptr || presets == m_presets.cend() || !presets->contains(presetId)) {
        return;
    }
    const QVariantMap preset = presets->value(presetId);
    for (auto it = ws->begin(); it != ws->end(); ++it) {
        applyDefaults(it.value());
        applyArrangement(it.value(), preset.value(it.key()).toMap());
    }
    touched();
}

QStringList DockModel::presets() const
{
    QStringList ids = m_presets.value(m_workspace).keys();
    std::sort(ids.begin(), ids.end());
    return ids;
}

// ---- saved layouts -------------------------------------------------------

QStringList DockModel::savedLayouts() const
{
    QStringList names = m_savedLayouts.value(m_workspace).keys();
    std::sort(names.begin(), names.end());
    return names;
}

void DockModel::saveLayout(const QString &name)
{
    const Workspace *ws = activeWorkspace();
    if (ws == nullptr || name.trimmed().isEmpty()) {
        return;
    }
    QVariantMap snapshot;
    for (auto it = ws->cbegin(); it != ws->cend(); ++it) {
        snapshot.insert(it.key(), arrangementOf(it.value()));
    }
    m_savedLayouts[m_workspace].insert(name.trimmed(), snapshot);
    emit savedLayoutsChanged();
    persist();
}

void DockModel::applyLayout(const QString &name)
{
    Workspace *ws = activeWorkspace();
    const auto saved = m_savedLayouts.constFind(m_workspace);
    if (ws == nullptr || saved == m_savedLayouts.cend() || !saved->contains(name)) {
        return;
    }
    const QVariantMap snapshot = saved->value(name);
    for (auto it = ws->begin(); it != ws->end(); ++it) {
        if (snapshot.contains(it.key())) {
            applyArrangement(it.value(), snapshot.value(it.key()).toMap());
        }
    }
    touched();
}

void DockModel::deleteLayout(const QString &name)
{
    auto saved = m_savedLayouts.find(m_workspace);
    if (saved == m_savedLayouts.end() || saved->remove(name) == 0) {
        return;
    }
    emit savedLayoutsChanged();
    persist();
}

// ---- persistence ---------------------------------------------------------

void DockModel::setStorageKey(const QString &key)
{
    if (key == m_storageKey) {
        return;
    }
    m_storageKey = key;
    emit storageKeyChanged();
}

QString DockModel::serialize() const
{
    QVariantMap workspaces;
    for (auto ws = m_workspaces.cbegin(); ws != m_workspaces.cend(); ++ws) {
        QVariantMap panels;
        for (auto p = ws->cbegin(); p != ws->cend(); ++p) {
            panels.insert(p.key(), arrangementOf(p.value()));
        }
        workspaces.insert(ws.key(), panels);
    }
    QVariantMap saved;
    for (auto ws = m_savedLayouts.cbegin(); ws != m_savedLayouts.cend(); ++ws) {
        QVariantMap layouts;
        for (auto l = ws->cbegin(); l != ws->cend(); ++l) {
            layouts.insert(l.key(), l.value());
        }
        saved.insert(ws.key(), layouts);
    }
    const QVariantMap document{{QStringLiteral("version"), 1},
                               {QStringLiteral("workspaces"), workspaces},
                               {QStringLiteral("saved"), saved}};
    return QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(document)).toJson(QJsonDocument::Compact));
}

bool DockModel::deserialize(const QString &json)
{
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(json.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return false;
    }
    const QVariantMap root = document.object().toVariantMap();
    m_restoring = true;
    const QVariantMap workspaces = root.value(QStringLiteral("workspaces")).toMap();
    for (auto ws = workspaces.cbegin(); ws != workspaces.cend(); ++ws) {
        const QVariantMap panels = ws.value().toMap();
        auto known = m_workspaces.find(ws.key());
        for (auto p = panels.cbegin(); p != panels.cend(); ++p) {
            if (known != m_workspaces.end() && known->contains(p.key())) {
                applyArrangement((*known)[p.key()], p.value().toMap());
            } else {
                m_pending[ws.key()].insert(p.key(), p.value().toMap());
            }
        }
    }
    const QVariantMap saved = root.value(QStringLiteral("saved")).toMap();
    for (auto ws = saved.cbegin(); ws != saved.cend(); ++ws) {
        const QVariantMap layouts = ws.value().toMap();
        for (auto l = layouts.cbegin(); l != layouts.cend(); ++l) {
            m_savedLayouts[ws.key()].insert(l.key(), l.value().toMap());
        }
    }
    m_restoring = false;
    ++m_generation;
    emit layoutChanged();
    emit savedLayoutsChanged();
    return true;
}

void DockModel::persist() const
{
    if (m_storageKey.isEmpty() || m_restoring) {
        return;
    }
    QSettings settings;
    settings.setValue(m_storageKey + QStringLiteral("/dock"), serialize());
}

void DockModel::restore()
{
    if (m_storageKey.isEmpty()) {
        return;
    }
    QSettings settings;
    const QString json = settings.value(m_storageKey + QStringLiteral("/dock")).toString();
    if (!json.isEmpty()) {
        deserialize(json);
    }
}

} // namespace QindaTK
