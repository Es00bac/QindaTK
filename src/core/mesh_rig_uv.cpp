// SPDX-License-Identifier: LGPL-3.0-or-later
#include "mesh_rig_document.h"

#include <QHash>
#include <QRectF>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <complex>

namespace QindaTK {
namespace {
// Floating residue from a transform may land a hair outside [0,1]; anything
// further out is a real overflow and the edit is refused.
bool settleUv(QPointF *uv) {
    constexpr double slack = 1e-9;
    if (!std::isfinite(uv->x()) || !std::isfinite(uv->y()) || uv->x() < -slack || uv->y() < -slack
        || uv->x() > 1 + slack || uv->y() > 1 + slack) return false;
    uv->setX(std::clamp(uv->x(), 0.0, 1.0)); uv->setY(std::clamp(uv->y(), 0.0, 1.0));
    return true;
}
struct PackItem { QRectF source; QPointF target; };
// Shelf packing at unit scale, tallest islands first, then one uniform scale
// that fits every row and the stack with `margin` gaps inside [0,1].
bool planPack(QVector<PackItem> &items, qreal margin, qreal *scale) {
    if (items.isEmpty() || !std::isfinite(margin) || margin < 0 || margin > 0.25) return false;
    QVector<int> order(items.size());
    for (int i = 0; i < order.size(); ++i) order[i] = i;
    auto width = [&](int i) { return std::max(items[i].source.width(), 1e-6); };
    auto height = [&](int i) { return std::max(items[i].source.height(), 1e-6); };
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return height(a) > height(b); });
    qreal area = 0, widest = 0;
    for (int i = 0; i < items.size(); ++i) { area += width(i) * height(i); widest = std::max(widest, width(i)); }
    const qreal shelf = std::max(widest, std::sqrt(area));
    struct Row { qreal width = 0; qreal height = 0; QVector<int> members; };
    QVector<Row> rows(1);
    QVector<QPointF> unit(items.size());
    qreal y = 0;
    for (int i : order) {
        if (!rows.last().members.isEmpty() && rows.last().width + width(i) > shelf) {
            y += rows.last().height; rows.append(Row{});
        }
        unit[i] = QPointF(rows.last().width, y);
        rows.last().width += width(i);
        rows.last().height = std::max(rows.last().height, height(i));
        rows.last().members.append(i);
    }
    const qreal stack = y + rows.last().height;
    qreal s = (1 - margin * (rows.size() + 1)) / stack;
    for (const auto &row : rows) s = std::min(s, (1 - margin * (row.members.size() + 1)) / row.width);
    if (!(s > 0) || !std::isfinite(s)) return false;
    for (int r = 0; r < rows.size(); ++r)
        for (int k = 0; k < rows[r].members.size(); ++k) {
            const int i = rows[r].members[k];
            items[i].target = QPointF(margin * (k + 1) + s * unit[i].x(), margin * (r + 1) + s * unit[i].y());
        }
    *scale = s;
    return true;
}
QRectF uvBounds(const QVector<QPointF> &points) {
    qreal x0 = 1, y0 = 1, x1 = 0, y1 = 0;
    for (const auto &p : points) {
        x0 = std::min(x0, p.x()); y0 = std::min(y0, p.y());
        x1 = std::max(x1, p.x()); y1 = std::max(y1, p.y());
    }
    return points.isEmpty() ? QRectF() : QRectF(QPointF(x0, y0), QPointF(x1, y1));
}
}

QVector<QVector<int>> MeshRigDocument::islandIndices() const {
    QVector<QVector<int>> groups;
    QHash<QString, QVector<int>> edgeFaces;
    for (int i = 0; i < m_state.faces.size(); ++i) {
        const auto &f = m_state.faces[i];
        for (int k = 0; k < 3; ++k)
            edgeFaces[edgeKey(m_state.vertices[f.vertices[k]].id,
                              m_state.vertices[f.vertices[(k + 1) % 3]].id)].append(i);
    }
    QVector<bool> visited(m_state.faces.size());
    for (int start = 0; start < m_state.faces.size(); ++start) {
        if (visited[start]) continue;
        QVector<int> group{start}; visited[start] = true;
        for (int cursor = 0; cursor < group.size(); ++cursor) {
            const auto &face = m_state.faces[group[cursor]];
            for (int e = 0; e < 3; ++e) {
                const auto key = edgeKey(m_state.vertices[face.vertices[e]].id,
                                         m_state.vertices[face.vertices[(e + 1) % 3]].id);
                if (m_state.seams.contains(key)) continue;
                for (int j : edgeFaces.value(key)) {
                    if (visited[j]) continue;
                    visited[j] = true; group.append(j);
                }
            }
        }
        groups.append(group);
    }
    return groups;
}
QVariantList MeshRigDocument::uvIslands() const {
    QVariantList result;
    for (const auto &group : islandIndices()) {
        QVariantList faceIds; QVector<QPointF> points;
        for (int i : group) {
            faceIds.append(m_state.faces[i].id);
            for (const auto &uv : m_state.faces[i].uv) points.append(uv);
        }
        result.append(QVariantMap{{"faceIds", faceIds}, {"bounds", uvBounds(points)}});
    }
    return result;
}
bool MeshRigDocument::setUV(const QString &id, int corner, const QPointF &uv) {
    const int i = faceIndex(id);
    if (m_painting || i < 0 || corner < 0 || corner > 2 || !validUv(uv))
        return fail("Invalid UV corner or coordinate");
    if (m_state.faces[i].uv[corner] == uv) return true;
    State next = m_state; next.faces[i].uv[corner] = uv;
    return commit(next, QStringLiteral("Move UV corner"));
}
bool MeshRigDocument::setSeam(const QString &v0, const QString &v1, bool seam) {
    if (m_painting || v0 == v1 || vertexIndex(v0) < 0 || vertexIndex(v1) < 0)
        return fail("Invalid seam vertices");
    const QString key = edgeKey(v0, v1);
    bool edgeExists = false;
    for (const auto &f : m_state.faces) for (int e = 0; e < 3; ++e)
        if (edgeKey(m_state.vertices[f.vertices[e]].id,
                    m_state.vertices[f.vertices[(e + 1) % 3]].id) == key) edgeExists = true;
    if (!edgeExists) return fail("Seam edge is not in the mesh");
    if (m_state.seams.contains(key) == seam) return true;
    State next = m_state;
    if (seam) next.seams.insert(key); else next.seams.remove(key);
    return commit(next, seam ? QStringLiteral("Cut UV seam") : QStringLiteral("Clear UV seam"));
}
bool MeshRigDocument::translateUvIsland(const QString &id, const QPointF &delta) {
    if (delta.isNull() && faceIndex(id) >= 0 && !m_painting) return true;
    return transformUvIsland(id, delta, 0, 1);
}
bool MeshRigDocument::transformUvIsland(const QString &id, const QPointF &translation,
                                        qreal rotationDegrees, qreal scale) {
    const int face = faceIndex(id);
    if (m_painting || face < 0 || !std::isfinite(translation.x()) || !std::isfinite(translation.y())
        || !std::isfinite(rotationDegrees) || !std::isfinite(scale) || scale < 0.001 || scale > 1000)
        return fail("Invalid UV island transform");
    for (const auto &group : islandIndices()) {
        if (!group.contains(face)) continue;
        QVector<QPointF> points;
        for (int i : group) for (const auto &uv : m_state.faces[i].uv) points.append(uv);
        const QPointF pivot = uvBounds(points).center();
        const qreal c = std::cos(qDegreesToRadians(rotationDegrees)) * scale;
        const qreal s = std::sin(qDegreesToRadians(rotationDegrees)) * scale;
        State next = m_state;
        for (int i : group) for (auto &uv : next.faces[i].uv) {
            const QPointF d = uv - pivot;
            // A pure translation adds the delta exactly (no pivot round trip).
            QPointF moved = rotationDegrees == 0 && scale == 1 ? uv + translation
                : QPointF(pivot.x() + c * d.x() - s * d.y() + translation.x(),
                          pivot.y() + s * d.x() + c * d.y() + translation.y());
            if (!settleUv(&moved)) return fail("UV island would leave texture space");
            uv = moved;
        }
        return commit(next, QStringLiteral("Transform UV island"));
    }
    return fail("UV island not found");
}
bool MeshRigDocument::stitchSeam(const QString &v0, const QString &v1) {
    const QString key = edgeKey(v0, v1);
    if (m_painting || !m_state.seams.contains(key)) return fail("Edge is not a UV seam");
    struct Side { int face; int c0; int c1; };
    QVector<Side> sides;
    for (int i = 0; i < m_state.faces.size(); ++i) {
        int c0 = -1, c1 = -1;
        for (int k = 0; k < 3; ++k) {
            const QString vid = m_state.vertices[m_state.faces[i].vertices[k]].id;
            if (vid == v0) c0 = k; else if (vid == v1) c1 = k;
        }
        if (c0 >= 0 && c1 >= 0) sides.append({i, c0, c1});
    }
    if (sides.size() != 2) return fail("A stitch needs a seam edge shared by two faces");
    const auto &keep = m_state.faces[sides[0].face];
    const auto &move = m_state.faces[sides[1].face];
    const std::complex<double> q0(keep.uv[sides[0].c0].x(), keep.uv[sides[0].c0].y());
    const std::complex<double> q1(keep.uv[sides[0].c1].x(), keep.uv[sides[0].c1].y());
    const std::complex<double> p0(move.uv[sides[1].c0].x(), move.uv[sides[1].c0].y());
    const std::complex<double> p1(move.uv[sides[1].c1].x(), move.uv[sides[1].c1].y());
    QVector<int> moving;
    for (const auto &group : islandIndices())
        if (group.contains(sides[1].face)) moving = group;
    State next = m_state;
    next.seams.remove(key);
    if (moving.contains(sides[0].face)) {
        // Already one island through another path: only coincident corners stitch.
        if (std::abs(p0 - q0) > 1e-9 || std::abs(p1 - q1) > 1e-9)
            return fail("Faces across this seam already share a UV island");
        return commit(next, QStringLiteral("Stitch UV seam"));
    }
    if (std::abs(p1 - p0) < 1e-12 || std::abs(q1 - q0) < 1e-12)
        return fail("Seam edge has no UV length");
    // AGENT-NOTE: the moving island gets the similarity (rotation, uniform
    // scale, translation) that lands its seam edge exactly on the kept edge,
    // so its shape is preserved and the two corners per vertex coincide.
    const std::complex<double> factor = (q1 - q0) / (p1 - p0);
    for (int i : moving) for (auto &uv : next.faces[i].uv) {
        const std::complex<double> z = q0 + (std::complex<double>(uv.x(), uv.y()) - p0) * factor;
        QPointF moved(z.real(), z.imag());
        if (!settleUv(&moved)) return fail("Stitched UV island would leave texture space");
        uv = moved;
    }
    next.faces[sides[1].face].uv[sides[1].c0] = keep.uv[sides[0].c0];
    next.faces[sides[1].face].uv[sides[1].c1] = keep.uv[sides[0].c1];
    return commit(next, QStringLiteral("Stitch UV seam"));
}
bool MeshRigDocument::unwrapUvIsland(const QString &id) {
    const int face = faceIndex(id);
    if (m_painting || face < 0) return fail("Invalid UV island");
    QVector<int> group;
    for (const auto &g : islandIndices()) if (g.contains(face)) group = g;
    auto corner = [&](int f, int k) { return m_state.vertices[m_state.faces[f].vertices[k]].rest; };
    QVector3D normal;
    for (int f : group)
        normal += QVector3D::crossProduct(corner(f, 1) - corner(f, 0), corner(f, 2) - corner(f, 0));
    if (normal.lengthSquared() < 1e-12f) return fail("UV island has no projection plane");
    normal.normalize();
    // AGENT-GUARD: planar unwrap is valid only when every face keeps its
    // winding on the plane; a folded island needs seams first, never overlap.
    for (int f : group)
        if (QVector3D::dotProduct(QVector3D::crossProduct(corner(f, 1) - corner(f, 0),
                                                          corner(f, 2) - corner(f, 0)), normal) <= 0)
            return fail("UV island folds over its projection plane; cut seams first");
    const QVector3D up = qAbs(normal.y()) < 0.99f ? QVector3D(0, 1, 0) : QVector3D(0, 0, -1);
    const QVector3D uAxis = QVector3D::crossProduct(up, normal).normalized();
    const QVector3D vAxis = QVector3D::crossProduct(normal, uAxis);
    qreal x0 = 0, y0 = 0, x1 = 0, y1 = 0; bool first = true;
    for (int f : group) for (int k = 0; k < 3; ++k) {
        const qreal x = QVector3D::dotProduct(corner(f, k), uAxis), y = QVector3D::dotProduct(corner(f, k), vAxis);
        if (first) { x0 = x1 = x; y0 = y1 = y; first = false; }
        x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y);
    }
    const qreal extent = std::max(x1 - x0, y1 - y0);
    if (!(extent > 1e-9)) return fail("UV island has no projected extent");
    State next = m_state;
    for (int f : group) for (int k = 0; k < 3; ++k) {
        QPointF uv((QVector3D::dotProduct(corner(f, k), uAxis) - x0) / extent,
                   (QVector3D::dotProduct(corner(f, k), vAxis) - y0) / extent);
        if (!settleUv(&uv)) return fail("Unwrapped UVs left texture space");
        next.faces[f].uv[k] = uv;
    }
    return commit(next, QStringLiteral("Unwrap UV island"));
}
QVariantList MeshRigDocument::packUvLayout(qreal margin) const {
    const auto groups = islandIndices();
    QVector<PackItem> items;
    for (const auto &group : groups) {
        QVector<QPointF> points;
        for (int i : group) for (const auto &uv : m_state.faces[i].uv) points.append(uv);
        items.append({uvBounds(points), {}});
    }
    qreal scale = 0; QVariantList result;
    if (!planPack(items, margin, &scale)) return result;
    for (int g = 0; g < groups.size(); ++g) {
        QVariantList faceIds;
        for (int i : groups[g]) faceIds.append(m_state.faces[i].id);
        const QRectF &source = items[g].source;
        result.append(QVariantMap{{"faceIds", faceIds}, {"scale", scale},
                                  {"bounds", QRectF(items[g].target, source.size() * scale)}});
    }
    return result;
}
bool MeshRigDocument::packUvIslands(qreal margin) {
    if (m_painting) return fail("Finish painting before packing");
    const auto groups = islandIndices();
    QVector<PackItem> items;
    for (const auto &group : groups) {
        QVector<QPointF> points;
        for (int i : group) for (const auto &uv : m_state.faces[i].uv) points.append(uv);
        items.append({uvBounds(points), {}});
    }
    qreal scale = 0;
    if (!planPack(items, margin, &scale)) return fail("UV islands cannot be packed with that margin");
    State next = m_state;
    for (int g = 0; g < groups.size(); ++g)
        for (int i : groups[g]) for (auto &uv : next.faces[i].uv) {
            QPointF packed = items[g].target + (uv - items[g].source.topLeft()) * scale;
            if (!settleUv(&packed)) return fail("Packed UVs left texture space");
            uv = packed;
        }
    return commit(next, QStringLiteral("Pack UV islands"));
}

} // namespace QindaTK
