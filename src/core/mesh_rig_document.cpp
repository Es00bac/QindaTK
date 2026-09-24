// SPDX-License-Identifier: LGPL-3.0-or-later
#include "mesh_rig_document.h"

#include <QBuffer>
#include <QColorSpace>
#include <QMatrix4x4>
#include <QQuaternion>
#include <QSaveFile>
#include <QTextStream>
#include <QUuid>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <limits>

namespace QindaTK {
namespace {
QString newId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
bool near(const QVector3D &a, const QVector3D &b) { return (a - b).lengthSquared() < 1e-12f; }
}

MeshRigDocument::MeshRigDocument(QObject *parent) : QObject(parent) {
    m_state.texture = QImage(256, 256, QImage::Format_ARGB32_Premultiplied);
    m_state.texture.fill(Qt::white);
    m_state.texture.setColorSpace(QColorSpace::SRgb);
}
bool MeshRigDocument::finitePoint(const QVector3D &p) {
    return std::isfinite(p.x()) && std::isfinite(p.y()) && std::isfinite(p.z())
           && qAbs(p.x()) <= 100000 && qAbs(p.y()) <= 100000 && qAbs(p.z()) <= 100000;
}
bool MeshRigDocument::validUv(const QPointF &uv) {
    return std::isfinite(uv.x()) && std::isfinite(uv.y())
           && uv.x() >= 0 && uv.x() <= 1 && uv.y() >= 0 && uv.y() <= 1;
}
QString MeshRigDocument::edgeKey(const QString &a, const QString &b) {
    return a < b ? a + u'|' + b : b + u'|' + a;
}
int MeshRigDocument::vertexIndex(const QString &id) const {
    for (int i = 0; i < m_state.vertices.size(); ++i) if (m_state.vertices[i].id == id) return i;
    return -1;
}
int MeshRigDocument::faceIndex(const QString &id) const {
    for (int i = 0; i < m_state.faces.size(); ++i) if (m_state.faces[i].id == id) return i;
    return -1;
}
int MeshRigDocument::boneIndex(const QString &id) const {
    for (int i = 0; i < m_state.bones.size(); ++i) if (m_state.bones[i].id == id) return i;
    return -1;
}
bool MeshRigDocument::fail(const QString &message) const {
    m_error = message; emit const_cast<MeshRigDocument *>(this)->errorChanged(); return false;
}
void MeshRigDocument::remember() {
    m_undo.append(m_state);
    if (m_undo.size() > 16) m_undo.removeFirst();
    m_redo.clear();
}
void MeshRigDocument::notify() { m_error.clear(); emit changed(); emit errorChanged(); }
int MeshRigDocument::textureWidth() const { return m_state.texture.width(); }
int MeshRigDocument::textureHeight() const { return m_state.texture.height(); }
QImage MeshRigDocument::texture() const { return m_state.texture; }
bool MeshRigDocument::canUndo() const { return !m_painting && !m_undo.isEmpty(); }
bool MeshRigDocument::canRedo() const { return !m_painting && !m_redo.isEmpty(); }
QString MeshRigDocument::textureDataUrl() const {
    QByteArray bytes; QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly);
    m_state.texture.save(&buffer, "PNG");
    return QStringLiteral("data:image/png;base64,") + QString::fromLatin1(bytes.toBase64());
}
QVector<QVector3D> MeshRigDocument::posedPositions() const {
    QVector<QMatrix4x4> transforms(m_state.bones.size());
    for (int i = 0; i < m_state.bones.size(); ++i) {
        const auto &b = m_state.bones[i];
        QMatrix4x4 transform;
        const int p = boneIndex(b.parentId);
        if (p >= 0) transform = transforms[p];
        transform.translate(b.head);
        transform.rotate(QQuaternion::fromEulerAngles(b.pose));
        transform.translate(-b.head);
        transforms[i] = transform;
    }
    QVector<QVector3D> result; result.reserve(m_state.vertices.size());
    for (const auto &v : m_state.vertices) {
        if (v.weights.isEmpty()) { result.append(v.rest); continue; }
        QVector3D posed;
        for (auto it = v.weights.cbegin(); it != v.weights.cend(); ++it)
            posed += transforms[boneIndex(it.key())].map(v.rest) * float(it.value());
        result.append(posed);
    }
    return result;
}
QVariantList MeshRigDocument::vertices() const {
    const auto posed = posedPositions(); QVariantList result;
    for (int i = 0; i < m_state.vertices.size(); ++i) {
        const auto &v = m_state.vertices[i]; QVariantMap weights;
        for (auto it = v.weights.cbegin(); it != v.weights.cend(); ++it) weights[it.key()] = it.value();
        result.append(QVariantMap{{"vertexId", v.id}, {"position", QVariant::fromValue(v.rest)},
                                  {"posedPosition", QVariant::fromValue(posed[i])}, {"weights", weights}});
    }
    return result;
}
QVariantList MeshRigDocument::faces() const {
    QVariantList result;
    for (const auto &f : m_state.faces) {
        QVariantList ids, uvIds, uvs, seams;
        for (int i = 0; i < 3; ++i) {
            ids.append(m_state.vertices[f.vertices[i]].id);
            uvIds.append(f.uvIds[i]); uvs.append(f.uv[i]);
            seams.append(m_state.seams.contains(edgeKey(
                m_state.vertices[f.vertices[i]].id,
                m_state.vertices[f.vertices[(i + 1) % 3]].id)));
        }
        result.append(QVariantMap{{"faceId", f.id}, {"vertexIds", ids},
                                  {"uvIds", uvIds}, {"uv", uvs}, {"seams", seams}});
    }
    return result;
}
QVariantList MeshRigDocument::bones() const {
    QVariantList result;
    for (const auto &b : m_state.bones)
        result.append(QVariantMap{{"boneId", b.id}, {"name", b.name}, {"parentId", b.parentId},
                                  {"head", QVariant::fromValue(b.head)}, {"tail", QVariant::fromValue(b.tail)},
                                  {"minimum", QVariant::fromValue(b.minimum)},
                                  {"maximum", QVariant::fromValue(b.maximum)},
                                  {"pose", QVariant::fromValue(b.pose)}});
    return result;
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
                const auto a = m_state.vertices[face.vertices[e]].id;
                const auto b = m_state.vertices[face.vertices[(e + 1) % 3]].id;
                const auto key = edgeKey(a, b);
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
        QVariantList faceIds;
        for (int i : group) faceIds.append(m_state.faces[i].id);
        result.append(QVariantMap{{"faceIds", faceIds}});
    }
    return result;
}
QString MeshRigDocument::addVertex(const QVector3D &position) {
    if (m_painting || !finitePoint(position) || m_state.vertices.size() >= 4096)
        { fail("Invalid vertex or vertex limit reached"); return {}; }
    remember(); const QString id = newId(); m_state.vertices.append({id, position, {}}); notify(); return id;
}
bool MeshRigDocument::setVertexPosition(const QString &id, const QVector3D &position) {
    const int i = vertexIndex(id);
    if (m_painting || i < 0 || !finitePoint(position)) return fail("Invalid vertex position");
    if (m_state.vertices[i].rest == position) return true;
    for (const auto &f : m_state.faces) {
        if (f.vertices[0] != i && f.vertices[1] != i && f.vertices[2] != i) continue;
        QVector3D p[3];
        for (int k = 0; k < 3; ++k)
            p[k] = f.vertices[k] == i ? position : m_state.vertices[f.vertices[k]].rest;
        if (QVector3D::crossProduct(p[1] - p[0], p[2] - p[0]).lengthSquared() < 1e-12f)
            return fail("Vertex move would degenerate a face");
    }
    remember(); m_state.vertices[i].rest = position; notify(); return true;
}
bool MeshRigDocument::removeVertex(const QString &id) {
    const int i = vertexIndex(id);
    if (m_painting || i < 0) return fail("Invalid vertex");
    for (const auto &f : m_state.faces)
        if (f.vertices[0] == i || f.vertices[1] == i || f.vertices[2] == i)
            return fail("Remove incident faces before removing vertex");
    remember(); m_state.vertices.removeAt(i);
    for (auto &f : m_state.faces) for (int &v : f.vertices) if (v > i) --v;
    notify(); return true;
}
QString MeshRigDocument::addFace(const QString &v0, const QString &v1, const QString &v2) {
    const int a = vertexIndex(v0), b = vertexIndex(v1), c = vertexIndex(v2);
    if (m_painting || a < 0 || b < 0 || c < 0 || a == b || b == c || a == c
        || m_state.faces.size() >= 8192 ||
        QVector3D::crossProduct(m_state.vertices[b].rest - m_state.vertices[a].rest,
                                m_state.vertices[c].rest - m_state.vertices[a].rest).lengthSquared() < 1e-12f)
        { fail("Invalid or degenerate triangle"); return {}; }
    const QSet<int> newVertices{a, b, c};
    const QString edgeKeys[3]{edgeKey(v0, v1), edgeKey(v1, v2), edgeKey(v2, v0)};
    int edgeUses[3]{};
    for (const auto &f : m_state.faces) {
        if (QSet<int>{f.vertices[0], f.vertices[1], f.vertices[2]} == newVertices)
            { fail("Duplicate mesh triangle"); return {}; }
        for (int e = 0; e < 3; ++e)
            for (int k = 0; k < 3; ++k)
                if (edgeKey(m_state.vertices[f.vertices[k]].id,
                            m_state.vertices[f.vertices[(k + 1) % 3]].id) == edgeKeys[e]) ++edgeUses[e];
    }
    for (int uses : edgeUses) if (uses >= 2) { fail("Nonmanifold mesh edge"); return {}; }
    remember(); Face face;
    face.id = newId(); face.vertices[0] = a; face.vertices[1] = b; face.vertices[2] = c;
    face.uv[0] = {0, 0}; face.uv[1] = {1, 0}; face.uv[2] = {0, 1};
    for (auto &id : face.uvIds) id = newId();
    m_state.faces.append(face); notify(); return face.id;
}
bool MeshRigDocument::removeFace(const QString &id) {
    const int i = faceIndex(id);
    if (m_painting || i < 0) return fail("Invalid face");
    remember(); m_state.faces.removeAt(i);
    QSet<QString> liveEdges;
    for (const auto &f : m_state.faces) for (int k = 0; k < 3; ++k)
        liveEdges.insert(edgeKey(m_state.vertices[f.vertices[k]].id,
                                 m_state.vertices[f.vertices[(k + 1) % 3]].id));
    m_state.seams.intersect(liveEdges); notify(); return true;
}
bool MeshRigDocument::setUV(const QString &id, int corner, const QPointF &uv) {
    const int i = faceIndex(id);
    if (m_painting || i < 0 || corner < 0 || corner > 2 || !validUv(uv))
        return fail("Invalid UV corner or coordinate");
    if (m_state.faces[i].uv[corner] == uv) return true;
    remember(); m_state.faces[i].uv[corner] = uv; notify(); return true;
}
bool MeshRigDocument::setSeam(const QString &v0, const QString &v1, bool seam) {
    if (m_painting || v0 == v1 || vertexIndex(v0) < 0 || vertexIndex(v1) < 0)
        return fail("Invalid seam vertices");
    bool edgeExists = false;
    for (const auto &f : m_state.faces) for (int e = 0; e < 3; ++e) {
        if (edgeKey(m_state.vertices[f.vertices[e]].id,
                    m_state.vertices[f.vertices[(e + 1) % 3]].id) == edgeKey(v0, v1)) edgeExists = true;
    }
    if (!edgeExists) return fail("Seam edge is not in the mesh");
    const QString key = edgeKey(v0, v1);
    if (m_state.seams.contains(key) == seam) return true;
    remember(); if (seam) m_state.seams.insert(key); else m_state.seams.remove(key);
    notify(); return true;
}
bool MeshRigDocument::translateUvIsland(const QString &id, const QPointF &delta) {
    const int face = faceIndex(id);
    if (m_painting || face < 0 || !std::isfinite(delta.x()) || !std::isfinite(delta.y()))
        return fail("Invalid UV island translation");
    if (delta.isNull()) return true;
    for (const auto &group : islandIndices()) if (group.contains(face)) {
        for (int i : group) for (const auto &uv : m_state.faces[i].uv)
            if (!validUv(uv + delta)) return fail("UV island would leave texture space");
        remember();
        for (int i : group) for (auto &uv : m_state.faces[i].uv) uv += delta;
        notify(); return true;
    }
    return fail("UV island not found");
}
QString MeshRigDocument::addBone(const QString &name, const QString &parentId,
                                  const QVector3D &head, const QVector3D &tail) {
    if (m_painting || name.isEmpty() || name.size() > 256 || (!parentId.isEmpty() && boneIndex(parentId) < 0)
        || !finitePoint(head) || !finitePoint(tail) || near(head, tail) || m_state.bones.size() >= 128)
        { fail("Invalid bone, parent or bone limit reached"); return {}; }
    remember(); Bone bone; bone.id = newId(); bone.name = name;
    bone.parentId = parentId; bone.head = head; bone.tail = tail;
    m_state.bones.append(bone); notify(); return bone.id;
}
bool MeshRigDocument::setBoneLimits(const QString &id, const QVector3D &min, const QVector3D &max) {
    const int i = boneIndex(id);
    if (m_painting || i < 0 || !finitePoint(min) || !finitePoint(max)
        || min.x() < -180 || min.y() < -180 || min.z() < -180
        || max.x() > 180 || max.y() > 180 || max.z() > 180
        || min.x() > max.x() || min.y() > max.y() || min.z() > max.z())
        return fail("Invalid bone rotation limits");
    if (min.x() > 0 || min.y() > 0 || min.z() > 0
        || max.x() < 0 || max.y() < 0 || max.z() < 0)
        return fail("Bone limits must include the reset pose");
    const auto pose = m_state.bones[i].pose;
    if (pose.x() < min.x() || pose.y() < min.y() || pose.z() < min.z()
        || pose.x() > max.x() || pose.y() > max.y() || pose.z() > max.z())
        return fail("Current pose is outside new limits");
    remember(); m_state.bones[i].minimum = min; m_state.bones[i].maximum = max; notify(); return true;
}
bool MeshRigDocument::setBonePose(const QString &id, const QVector3D &euler) {
    const int i = boneIndex(id);
    if (m_painting || i < 0 || !finitePoint(euler)) return fail("Invalid bone pose");
    const auto &b = m_state.bones[i];
    if (euler.x() < b.minimum.x() || euler.y() < b.minimum.y() || euler.z() < b.minimum.z()
        || euler.x() > b.maximum.x() || euler.y() > b.maximum.y() || euler.z() > b.maximum.z())
        return fail("Bone pose exceeds rotation limits");
    if (euler == b.pose) return true;
    remember(); m_state.bones[i].pose = euler; notify(); return true;
}
bool MeshRigDocument::resetPose() {
    if (m_painting) return fail("Finish painting before posing");
    bool changed = false; for (const auto &b : m_state.bones) changed |= !b.pose.isNull();
    if (!changed) return true;
    remember(); for (auto &b : m_state.bones) b.pose = {}; notify(); return true;
}
bool MeshRigDocument::setVertexWeights(const QString &id, const QVariantMap &weights) {
    const int i = vertexIndex(id);
    if (m_painting || i < 0 || weights.size() > 4) return fail("Invalid vertex weights");
    QMap<QString, double> parsed; double total = 0;
    for (auto it = weights.cbegin(); it != weights.cend(); ++it) {
        bool ok = false; const double value = it.value().toDouble(&ok);
        if (!ok || !std::isfinite(value) || value <= 0 || value > 1 || boneIndex(it.key()) < 0)
            return fail("Invalid bone weight");
        parsed.insert(it.key(), value); total += value;
    }
    if (!parsed.isEmpty() && qAbs(total - 1.0) > 0.001) return fail("Bone weights must sum to one");
    if (parsed == m_state.vertices[i].weights) return true;
    remember(); m_state.vertices[i].weights = parsed; notify(); return true;
}
bool MeshRigDocument::resizeTexture(int width, int height) {
    if (m_painting || width < 1 || height < 1 || width > 1024 || height > 1024)
        return fail("Texture size must be 1 to 1024 pixels per side");
    if (width == textureWidth() && height == textureHeight()) return true;
    remember(); QImage next(width, height, QImage::Format_ARGB32_Premultiplied);
    next.fill(Qt::white); next.setColorSpace(QColorSpace::SRgb);
    m_state.texture = next; notify(); return true;
}
bool MeshRigDocument::beginPaint() {
    if (m_painting) return fail("Paint stroke already active");
    m_beforePaint = m_state; m_painting = true; m_paintChanged = false;
    m_paintSamples = 0; m_paintPixelWork = 0; return true;
}
bool MeshRigDocument::paintRay(const QVector3D &origin, const QVector3D &direction,
                               const QColor &color, int radius, qreal opacity) {
    if (!m_painting || !finitePoint(origin) || !finitePoint(direction)
        || direction.lengthSquared() < 1e-12f || !color.isValid()
        || radius < 1 || radius > 128 || !std::isfinite(opacity) || opacity <= 0 || opacity > 1)
        return fail("Invalid paint ray or brush");
    if (++m_paintSamples > 8192 || (m_paintPixelWork += qint64(radius * 2 + 1) * (radius * 2 + 1)) > 16000000)
        { cancelPaint(); return fail("Paint stroke work limit reached"); }
    const auto posed = posedPositions();
    float nearest = std::numeric_limits<float>::max(); QPointF hitUv; bool hit = false;
    for (const auto &face : m_state.faces) {
        const QVector3D a = posed[face.vertices[0]], b = posed[face.vertices[1]], c = posed[face.vertices[2]];
        const auto e1 = b - a, e2 = c - a;
        const auto p = QVector3D::crossProduct(direction, e2);
        const float det = QVector3D::dotProduct(e1, p);
        if (qAbs(det) < 1e-8f) continue;
        const float inv = 1.0f / det;
        const auto t = origin - a;
        const float u = QVector3D::dotProduct(t, p) * inv;
        if (u < -1e-5f || u > 1.00001f) continue;
        const auto q = QVector3D::crossProduct(t, e1);
        const float v = QVector3D::dotProduct(direction, q) * inv;
        if (v < -1e-5f || u + v > 1.00001f) continue;
        const float distance = QVector3D::dotProduct(e2, q) * inv;
        if (distance <= 1e-6f || distance >= nearest) continue;
        nearest = distance; hit = true;
        hitUv = face.uv[0] * (1 - u - v) + face.uv[1] * u + face.uv[2] * v;
    }
    if (!hit) return fail("Paint ray missed the mesh");
    const int centerX = qRound(hitUv.x() * (textureWidth() - 1));
    const int centerY = qRound((1 - hitUv.y()) * (textureHeight() - 1));
    const int srcA = qRound(color.alpha() * opacity);
    const int srcR = (color.red() * srcA + 127) / 255;
    const int srcG = (color.green() * srcA + 127) / 255;
    const int srcB = (color.blue() * srcA + 127) / 255;
    for (int y = std::max(0, centerY - radius); y <= std::min(textureHeight() - 1, centerY + radius); ++y) {
        auto *row = reinterpret_cast<QRgb *>(m_state.texture.scanLine(y));
        for (int x = std::max(0, centerX - radius); x <= std::min(textureWidth() - 1, centerX + radius); ++x) {
            const int dx = x - centerX, dy = y - centerY;
            if (dx * dx + dy * dy > radius * radius) continue;
            const QRgb dst = row[x];
            const int inverse = 255 - srcA;
            row[x] = qRgba(std::min(255, srcR + (int(qRed(dst)) * inverse + 127) / 255),
                           std::min(255, srcG + (int(qGreen(dst)) * inverse + 127) / 255),
                           std::min(255, srcB + (int(qBlue(dst)) * inverse + 127) / 255),
                           std::min(255, srcA + (int(qAlpha(dst)) * inverse + 127) / 255));
        }
    }
    m_paintChanged = true; notify(); return true;
}
bool MeshRigDocument::endPaint() {
    if (!m_painting) return fail("No paint stroke active");
    if (m_paintChanged) {
        m_undo.append(m_beforePaint); if (m_undo.size() > 16) m_undo.removeFirst(); m_redo.clear();
    }
    m_beforePaint = {}; m_painting = false; notify(); return true;
}
void MeshRigDocument::cancelPaint() {
    if (!m_painting) return;
    m_state = m_beforePaint; m_beforePaint = {}; m_painting = false; notify();
}
bool MeshRigDocument::undo() {
    if (!canUndo()) return fail("No undo step available");
    m_redo.append(m_state); m_state = m_undo.takeLast(); notify(); return true;
}
bool MeshRigDocument::redo() {
    if (!canRedo()) return fail("No redo step available");
    m_undo.append(m_state); m_state = m_redo.takeLast(); notify(); return true;
}
bool MeshRigDocument::exportObj(const QString &path) const {
    QSaveFile file(path); if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return fail(file.errorString());
    QTextStream stream(&file); stream << "# QindaTK posed mesh\n";
    const auto posed = posedPositions();
    for (const auto &p : posed) stream << "v " << p.x() << ' ' << p.y() << ' ' << p.z() << '\n';
    for (const auto &f : m_state.faces) for (const auto &uv : f.uv)
        stream << "vt " << uv.x() << ' ' << uv.y() << '\n';
    for (int i = 0; i < m_state.faces.size(); ++i) {
        const auto &f = m_state.faces[i]; stream << "f";
        for (int c = 0; c < 3; ++c) stream << ' ' << f.vertices[c] + 1 << '/' << i * 3 + c + 1;
        stream << '\n';
    }
    stream.flush(); return stream.status() == QTextStream::Ok && file.commit();
}
bool MeshRigDocument::exportTexturePng(const QString &path) const {
    QSaveFile file(path); if (!file.open(QIODevice::WriteOnly)) return fail(file.errorString());
    return m_state.texture.save(&file, "PNG") && file.commit();
}

} // namespace QindaTK
