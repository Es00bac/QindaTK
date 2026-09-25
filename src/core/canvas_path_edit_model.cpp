// SPDX-License-Identifier: LGPL-3.0-or-later
#include "canvas_path_edit_model.h"
#include "canvas_document_format.h"

#include <QSet>
#include <cmath>

namespace QindaTK {
namespace {
constexpr qsizetype MemoryDepth = 64;

QString nodeKey(quint64 id) { return QStringLiteral("n%1").arg(id); }

quint64 parseNode(const QString &key)
{
    bool ok = false;
    const quint64 id = key.startsWith(QLatin1Char('n')) ? key.mid(1).toULongLong(&ok) : 0;
    return ok ? id : 0;
}

QVector<CanvasPathContour> contoursOf(const CanvasObject &o)
{
    QVector<CanvasPathContour> result{CanvasPathContour{o.pathNodes, o.pathClosed}};
    result += o.extraContours;
    return result;
}

QVector<CanvasPathContour> geometryOf(const QVector<CanvasPathEditContour> &path)
{
    QVector<CanvasPathContour> result;
    for (const auto &c : path) result.append(c.contour);
    return result;
}
} // namespace

void CanvasPathEditModel::setDocument(CanvasDocumentAdapter *document)
{
    if (m_document == document) return;
    if (m_document) disconnect(m_document, nullptr, this, nullptr);
    m_document = document;
    if (document) {
        connect(document, &QObject::destroyed, this, [this] { m_document = nullptr; reconcile(); });
        connect(document, &CanvasDocumentAdapter::edited, this, [this] { reconcile(); });
        connect(document, &CanvasDocumentAdapter::reset, this, [this] { reconcile(); });
    }
    m_memory.clear();
    reconcile();
    emit documentChanged();
}

void CanvasPathEditModel::setObjectId(const QString &id)
{
    if (m_objectId == id) return;
    m_objectId = id;
    m_path.clear();
    m_memory.clear();
    m_selected.clear();
    emit objectIdChanged();
    emit selectionChanged();
    reconcile();
}

const CanvasObject *CanvasPathEditModel::target() const
{
    bool ok = false;
    const CanvasObjectId id = m_objectId.toULongLong(&ok);
    const CanvasObject *o = ok && id && m_document ? m_document->object(id) : nullptr;
    return o && o->shape == CanvasShape::Path ? o : nullptr;
}

void CanvasPathEditModel::reconcile()
{
    const CanvasObject *o = target();
    if (!o) {
        m_path.clear();
    } else {
        const QVector<CanvasPathContour> geometry = contoursOf(*o);
        const auto remembered = std::find_if(m_memory.crbegin(), m_memory.crend(), [&](const auto &entry) {
            return geometryOf(entry) == geometry;
        });
        if (m_pending && geometryOf(*m_pending) == geometry) m_path = *m_pending;
        else if (remembered != m_memory.crend()) m_path = *remembered;
        else if (geometryOf(m_path) != geometry)
            m_path = CanvasPathEditing::carryIds(m_path, geometry, [this] { return nextId(); });
        m_memory.removeIf([this](const auto &entry) { return entry == m_path; });
        m_memory.append(m_path);
        if (m_memory.size() > MemoryDepth) m_memory.removeFirst();
    }
    m_pending.reset();
    // The selection keeps only ids that still exist.
    QSet<QString> present;
    for (const auto &c : m_path)
        for (quint64 id : c.ids) present.insert(nodeKey(id));
    QStringList kept;
    for (const QString &id : m_selected)
        if (present.contains(id)) kept.append(id);
    if (kept != m_selected) {
        m_selected = kept;
        emit selectionChanged();
    }
    publish();
}

void CanvasPathEditModel::publish()
{
    QVariantList nodes, segments, contours;
    for (int c = 0; c < m_path.size(); ++c) {
        const CanvasPathContour &contour = m_path.at(c).contour;
        const auto &ids = m_path.at(c).ids;
        const int count = contour.nodes.size();
        for (int i = 0; i < count; ++i) {
            const CanvasPathNode &n = contour.nodes.at(i);
            const QString id = nodeKey(ids.at(i));
            nodes.append(QVariantMap{{QStringLiteral("id"), id}, {QStringLiteral("contour"), c},
                                     {QStringLiteral("index"), i},
                                     {QStringLiteral("x"), n.anchor.x()}, {QStringLiteral("y"), n.anchor.y()},
                                     {QStringLiteral("inX"), n.in.x()}, {QStringLiteral("inY"), n.in.y()},
                                     {QStringLiteral("outX"), n.out.x()}, {QStringLiteral("outY"), n.out.y()},
                                     {QStringLiteral("type"), CanvasPathEditing::nodeType(contour, i)},
                                     {QStringLiteral("endpoint"), !contour.closed && (i == 0 || i == count - 1)},
                                     {QStringLiteral("selected"), m_selected.contains(id)}});
            if (i + 1 < count || (contour.closed && count > 1))
                segments.append(QVariantMap{{QStringLiteral("id"), QLatin1Char('s') + id},
                                            {QStringLiteral("contour"), c},
                                            {QStringLiteral("from"), id},
                                            {QStringLiteral("to"), nodeKey(ids.at((i + 1) % count))}});
        }
        contours.append(QVariantMap{{QStringLiteral("index"), c}, {QStringLiteral("closed"), contour.closed},
                                    {QStringLiteral("nodeCount"), count}});
    }
    if (nodes == m_nodes && segments == m_segments && contours == m_contourList) return;
    m_nodes = nodes;
    m_segments = segments;
    m_contourList = contours;
    emit nodesChanged();
}

CanvasPathEditModel::Locator CanvasPathEditModel::locate(const QString &nodeId) const
{
    const quint64 id = parseNode(nodeId);
    for (int c = 0; id && c < m_path.size(); ++c) {
        const int index = m_path.at(c).ids.indexOf(id);
        if (index >= 0) return {c, index};
    }
    return {};
}

QVariantMap CanvasPathEditModel::node(const QString &nodeId) const
{
    const Locator at = locate(nodeId);
    for (const QVariant &value : m_nodes) {
        const QVariantMap map = value.toMap();
        if (at.contour >= 0 && map.value(QStringLiteral("id")).toString() == nodeId) return map;
    }
    return {};
}

void CanvasPathEditModel::setSelectedNodeIds(const QStringList &ids)
{
    QStringList next;
    for (const QString &id : ids)
        if (locate(id).contour >= 0 && !next.contains(id)) next.append(id);
    if (next == m_selected) return;
    m_selected = next;
    emit selectionChanged();
    publish();
}

bool CanvasPathEditModel::selectNode(const QString &nodeId, bool extend)
{
    if (locate(nodeId).contour < 0) return fail(tr("Unknown path node"));
    QStringList next = extend ? m_selected : QStringList();
    if (extend && next.contains(nodeId)) next.removeAll(nodeId);
    else next.append(nodeId);
    setSelectedNodeIds(next);
    return succeed();
}

QStringList CanvasPathEditModel::requested(const QStringList &nodeIds) const
{
    return nodeIds.isEmpty() ? m_selected : nodeIds;
}

bool CanvasPathEditModel::fail(const QString &message)
{
    if (m_lastError != message) {
        m_lastError = message;
        emit errorChanged();
    }
    return false;
}

bool CanvasPathEditModel::succeed()
{
    if (!m_lastError.isEmpty()) {
        m_lastError.clear();
        emit errorChanged();
    }
    return true;
}

bool CanvasPathEditModel::commitPath(QVector<CanvasPathEditContour> path, const QString &label)
{
    const CanvasObject *before = target();
    if (!before) return fail(tr("Select a path object first"));
    if (!m_document->effectiveVisible(*before) || m_document->effectiveLocked(*before))
        return fail(tr("The path is hidden or locked"));
    if (path.isEmpty()) return fail(tr("A path needs at least one contour"));
    for (const auto &c : path)
        if (c.contour.nodes.size() < (c.contour.closed ? 3 : 2))
            return fail(tr("A closed contour needs three nodes and an open one two"));
    CanvasObject after = *before;
    after.pathNodes = path.first().contour.nodes;
    after.pathClosed = path.first().contour.closed;
    after.extraContours.clear();
    for (qsizetype c = 1; c < path.size(); ++c) after.extraContours.append(path.at(c).contour);
    after.clearParametric();
    if (after == *before) return succeed();
    if (const OperationError error = CanvasDocumentFormat::validateObject(after, before->id, CanvasDocumentLimits{}))
        return fail(error.message);
    CanvasEdit edit;
    edit.label = label;
    edit.changes.append({before->id, *before, after});
    m_pending = std::move(path);
    if (!m_document->commit(edit)) {
        m_pending.reset();
        return fail(tr("The document refused the path edit"));
    }
    m_pending.reset();
    return succeed();
}

QString CanvasPathEditModel::insertNode(const QString &segmentId, qreal t)
{
    const Locator at = segmentId.startsWith(QLatin1Char('s')) ? locate(segmentId.mid(1)) : Locator{};
    if (at.contour < 0) { fail(tr("Unknown path segment")); return {}; }
    QVector<CanvasPathEditContour> path = m_path;
    QString error;
    const quint64 id = nextId();
    if (!CanvasPathEditing::insert(path, at.contour, at.index, t, id, &error)) { fail(error); return {}; }
    return commitPath(path, tr("Insert path node")) ? nodeKey(id) : QString();
}

bool CanvasPathEditModel::deleteNodes(const QStringList &nodeIds)
{
    const QStringList ids = requested(nodeIds);
    if (ids.isEmpty()) return fail(tr("Select the nodes to delete"));
    QVector<CanvasPathEditContour> path = m_path;
    for (const QString &id : ids) {
        const quint64 key = parseNode(id);
        bool found = false;
        for (auto &c : path) {
            const int index = key ? c.ids.indexOf(key) : -1;
            if (index < 0) continue;
            c.contour.nodes.removeAt(index);
            c.ids.removeAt(index);
            found = true;
        }
        if (!found) return fail(tr("Unknown path node"));
    }
    return commitPath(path, tr("Delete path nodes"));
}

bool CanvasPathEditModel::setNodeType(const QString &type, const QStringList &nodeIds)
{
    const QStringList ids = requested(nodeIds);
    if (ids.isEmpty()) return fail(tr("Select the nodes to change"));
    QVector<CanvasPathEditContour> path = m_path;
    for (const QString &id : ids) {
        const Locator at = locate(id);
        QString error;
        if (at.contour < 0) return fail(tr("Unknown path node"));
        if (!CanvasPathEditing::setType(path, at.contour, at.index, type, &error)) return fail(error);
    }
    return commitPath(path, tr("Change path node type"));
}

bool CanvasPathEditModel::splitAt(const QString &nodeId)
{
    const Locator at = locate(nodeId);
    if (at.contour < 0) return fail(tr("Unknown path node"));
    QVector<CanvasPathEditContour> path = m_path;
    QString error;
    if (!CanvasPathEditing::split(path, at.contour, at.index, nextId(), &error)) return fail(error);
    return commitPath(path, tr("Split path"));
}

bool CanvasPathEditModel::join(const QString &firstNodeId, const QString &secondNodeId)
{
    const Locator a = locate(firstNodeId), b = locate(secondNodeId);
    if (a.contour < 0 || b.contour < 0) return fail(tr("Unknown path node"));
    QVector<CanvasPathEditContour> path = m_path;
    QString error;
    if (!CanvasPathEditing::join(path, a.contour, a.index, b.contour, b.index, &error)) return fail(error);
    return commitPath(path, tr("Join path"));
}

bool CanvasPathEditModel::moveNodePart(const QString &nodeId, const QString &part, qreal x, qreal y)
{
    const Locator at = locate(nodeId);
    if (at.contour < 0) return fail(tr("Unknown path node"));
    if (!std::isfinite(x) || !std::isfinite(y)) return fail(tr("Invalid node position"));
    QVector<CanvasPathEditContour> path = m_path;
    CanvasPathNode &n = path[at.contour].contour.nodes[at.index];
    const QPointF to(x, y);
    if (part == QLatin1String("anchor")) {
        const QPointF delta = to - n.anchor;
        n.anchor = to;
        n.in += delta;
        n.out += delta;
    } else if (part == QLatin1String("in")) {
        n.in = to;
    } else if (part == QLatin1String("out")) {
        n.out = to;
    } else {
        return fail(tr("Unknown node part: %1").arg(part));
    }
    return commitPath(path, tr("Move path node or handle"));
}

} // namespace QindaTK
