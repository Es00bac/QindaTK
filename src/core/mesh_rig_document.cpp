// SPDX-License-Identifier: LGPL-3.0-or-later
#include "mesh_rig_document.h"
#include "undo_service.h"

#include <QSaveFile>
#include <QTextStream>
#include <QUuid>
#include <cmath>

namespace QindaTK {
namespace {
QString newId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
}

MeshRigDocument::MeshRigDocument(QObject *parent) : QObject(parent)
{
    m_documentId = DocumentId::create().toString();
    for (int c = 0; c < ChannelCount; ++c) m_state.textures[c] = blankChannel(c, 256, 256);
    // AGENT-NOTE: like SceneModel3D, a standalone document keeps a private
    // history; a host that adopts the model into its UndoService (a session,
    // or one service shared with canvas and scene models) takes it over.
    m_localHistory = new UndoService(this);
    m_localHistory->setMemoryLimit(quint64(512) << 20);
    m_localHistory->addModel(this);
    m_localHistory->setStepLimit(this, 16);
}
MeshRigDocument::~MeshRigDocument() = default;

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
int MeshRigDocument::boneIndex(const QString &id) const { return boneIndexIn(m_state, id); }
int MeshRigDocument::boneIndexIn(const State &state, const QString &id) const {
    for (int i = 0; i < state.bones.size(); ++i) if (state.bones[i].id == id) return i;
    return -1;
}
bool MeshRigDocument::fail(const QString &message) const {
    m_error = message; emit const_cast<MeshRigDocument *>(this)->errorChanged(); return false;
}
void MeshRigDocument::notify() { m_error.clear(); emit changed(); emit errorChanged(); }

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

QString MeshRigDocument::addVertex(const QVector3D &position) {
    if (m_painting || !finitePoint(position) || m_state.vertices.size() >= 4096)
        { fail("Invalid vertex or vertex limit reached"); return {}; }
    State next = m_state; const QString id = newId(); next.vertices.append({id, position, {}});
    return commit(next, QStringLiteral("Add vertex")) ? id : QString();
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
    State next = m_state; next.vertices[i].rest = position;
    return commit(next, QStringLiteral("Move vertex"));
}
bool MeshRigDocument::removeVertex(const QString &id) {
    const int i = vertexIndex(id);
    if (m_painting || i < 0) return fail("Invalid vertex");
    for (const auto &f : m_state.faces)
        if (f.vertices[0] == i || f.vertices[1] == i || f.vertices[2] == i)
            return fail("Remove incident faces before removing vertex");
    State next = m_state; next.vertices.removeAt(i);
    for (auto &f : next.faces) for (int &v : f.vertices) if (v > i) --v;
    return commit(next, QStringLiteral("Remove vertex"));
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
    Face face;
    face.id = newId(); face.vertices[0] = a; face.vertices[1] = b; face.vertices[2] = c;
    face.uv[0] = {0, 0}; face.uv[1] = {1, 0}; face.uv[2] = {0, 1};
    for (auto &id : face.uvIds) id = newId();
    State next = m_state; next.faces.append(face);
    return commit(next, QStringLiteral("Add face")) ? face.id : QString();
}
bool MeshRigDocument::removeFace(const QString &id) {
    const int i = faceIndex(id);
    if (m_painting || i < 0) return fail("Invalid face");
    State next = m_state; next.faces.removeAt(i);
    QSet<QString> liveEdges;
    for (const auto &f : next.faces) for (int k = 0; k < 3; ++k)
        liveEdges.insert(edgeKey(next.vertices[f.vertices[k]].id,
                                 next.vertices[f.vertices[(k + 1) % 3]].id));
    next.seams.intersect(liveEdges);
    return commit(next, QStringLiteral("Remove face"));
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
    State next = m_state; next.vertices[i].weights = parsed;
    return commit(next, QStringLiteral("Set vertex weights"));
}

bool MeshRigDocument::exportObj(const QString &path) const {
    // AGENT-NOTE: OBJ carries the pose evaluated at currentFrame (keys,
    // constraints and limits applied); the native JSON keeps the rig itself.
    QSaveFile file(path); if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return fail(file.errorString());
    QTextStream stream(&file); stream << "# QindaTK posed mesh, frame " << m_frame << '\n';
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

} // namespace QindaTK
