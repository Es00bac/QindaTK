// SPDX-License-Identifier: LGPL-3.0-or-later
#include "mesh_rig_document.h"

#include <QBuffer>
#include <QColorSpace>
#include <QFile>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <algorithm>
#include <cmath>

namespace QindaTK {
namespace {
QJsonArray vec(const QVector3D &v) { return {v.x(), v.y(), v.z()}; }
QJsonArray point(const QPointF &p) { return {p.x(), p.y()}; }
bool readVec(const QJsonValue &value, QVector3D *result) {
    if (!value.isArray()) return false;
    const auto a = value.toArray(); if (a.size() != 3) return false;
    for (const auto v : a) if (!v.isDouble() || !std::isfinite(v.toDouble()) || qAbs(v.toDouble()) > 100000) return false;
    *result = {float(a[0].toDouble()), float(a[1].toDouble()), float(a[2].toDouble())}; return true;
}
bool readUv(const QJsonValue &value, QPointF *result) {
    if (!value.isArray()) return false;
    const auto a = value.toArray(); if (a.size() != 2) return false;
    for (const auto v : a) if (!v.isDouble() || !std::isfinite(v.toDouble()) || v.toDouble() < 0 || v.toDouble() > 1) return false;
    *result = {a[0].toDouble(), a[1].toDouble()}; return true;
}
bool validId(const QString &id) { return !id.isEmpty() && id.size() <= 80 && !id.contains(u'|'); }
}

QString MeshRigDocument::toJson() const {
    // AGENT-GUARD: A save during an active paint stroke records the committed
    // pixels, matching SceneModel3D's preview/persistence boundary.
    const State &state = m_painting ? m_beforePaint : m_state;
    QJsonObject root{{"format", "qindatk.mesh-rig"}, {"version", 2}};
    QJsonArray vertices;
    for (const auto &v : state.vertices) {
        QJsonObject weights;
        for (auto it = v.weights.cbegin(); it != v.weights.cend(); ++it) weights.insert(it.key(), it.value());
        vertices.append(QJsonObject{{"id", v.id}, {"position", vec(v.rest)}, {"weights", weights}});
    }
    root.insert("vertices", vertices);
    QJsonArray faces;
    for (const auto &f : state.faces) {
        QJsonArray ids, uvIds, uvs;
        for (int i = 0; i < 3; ++i) {
            ids.append(state.vertices[f.vertices[i]].id); uvIds.append(f.uvIds[i]); uvs.append(point(f.uv[i]));
        }
        faces.append(QJsonObject{{"id", f.id}, {"vertexIds", ids}, {"uvIds", uvIds}, {"uv", uvs}});
    }
    root.insert("faces", faces);
    QJsonArray bones;
    for (const auto &b : state.bones) {
        QJsonArray constraints, keys;
        for (const auto &c : b.constraints)
            constraints.append(QJsonObject{{"id", c.id}, {"type", c.type},
                                           {"targetId", c.targetId}, {"influence", c.influence}});
        for (const auto &k : b.keys) keys.append(QJsonObject{{"frame", k.frame}, {"pose", vec(k.pose)}});
        bones.append(QJsonObject{{"id", b.id}, {"name", b.name}, {"parentId", b.parentId},
                                 {"head", vec(b.head)}, {"tail", vec(b.tail)},
                                 {"minimum", vec(b.minimum)}, {"maximum", vec(b.maximum)},
                                 {"pose", vec(b.pose)}, {"constraints", constraints}, {"keys", keys}});
    }
    root.insert("bones", bones);
    QJsonArray seams;
    for (const auto &key : state.seams) {
        const auto parts = key.split(u'|'); seams.append(QJsonArray{parts[0], parts[1]});
    }
    root.insert("seams", seams);
    QJsonObject textures;
    const QStringList channels = textureChannels();
    for (int c = 0; c < ChannelCount; ++c) {
        QByteArray pixels; QBuffer buffer(&pixels); buffer.open(QIODevice::WriteOnly);
        state.textures[c].save(&buffer, "PNG");
        textures.insert(channels[c], QString::fromLatin1(pixels.toBase64()));
    }
    root.insert("textures", textures);
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

bool MeshRigDocument::stateFromJson(const QString &json, State *result) const {
    QString problem;
    return parseState(json.toUtf8(), result, &problem) || fail(problem);
}
// Static so the session codec (mesh_rig_document_codec.cpp) validates files
// without a live document; every rule applies to both paths.
bool MeshRigDocument::parseState(const QByteArray &json, State *result, QString *problem) {
    const auto fail = [problem](const char *message) { *problem = QString::fromLatin1(message); return false; };
    if (json.size() > 17 * 1024 * 1024) return fail("Mesh document exceeds its size limit");
    QJsonParseError error;
    const auto parsed = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !parsed.isObject()) return fail("Invalid mesh JSON");
    const auto root = parsed.object();
    // Version 1 files (one sRGB texture, no constraints or keys) still open.
    const int version = root.value("version").toInt();
    if (root.value("format") != "qindatk.mesh-rig" || (version != 1 && version != 2))
        return fail("Unsupported mesh format or version");
    const auto vertexRows = root.value("vertices").toArray();
    const auto faceRows = root.value("faces").toArray();
    const auto boneRows = root.value("bones").toArray();
    const auto seamRows = root.value("seams").toArray();
    if (!root.value("vertices").isArray() || !root.value("faces").isArray()
        || !root.value("bones").isArray() || !root.value("seams").isArray()
        || vertexRows.size() > 4096 || faceRows.size() > 8192 || boneRows.size() > 128)
        return fail("Invalid mesh arrays or element limit reached");
    State state; QSet<QString> ids, uvIds;
    for (const auto row : vertexRows) {
        if (!row.isObject()) return fail("Invalid vertex record");
        const auto item = row.toObject(); Vertex v;
        v.id = item.value("id").toString();
        if (!validId(v.id) || ids.contains(v.id) || !readVec(item.value("position"), &v.rest)
            || !item.value("weights").isObject()) return fail("Invalid vertex identity or position");
        ids.insert(v.id); state.vertices.append(v);
    }
    QSet<QString> boneIds;
    for (const auto row : boneRows) {
        if (!row.isObject()) return fail("Invalid bone record");
        const auto item = row.toObject(); Bone b;
        b.id = item.value("id").toString(); b.name = item.value("name").toString();
        b.parentId = item.value("parentId").toString();
        if (!validId(b.id) || boneIds.contains(b.id) || b.name.isEmpty() || b.name.size() > 256
            || (!b.parentId.isEmpty() && !boneIds.contains(b.parentId))
            || !readVec(item.value("head"), &b.head) || !readVec(item.value("tail"), &b.tail)
            || (b.head - b.tail).lengthSquared() < 1e-12f
            || !readVec(item.value("minimum"), &b.minimum)
            || !readVec(item.value("maximum"), &b.maximum)
            || !readVec(item.value("pose"), &b.pose)) return fail("Invalid bone or hierarchy");
        for (int k = 0; k < 3; ++k)
            if (b.minimum[k] < -180 || b.maximum[k] > 180 || b.minimum[k] > 0
                || b.maximum[k] < 0 || b.minimum[k] > b.maximum[k]
                || b.pose[k] < b.minimum[k] || b.pose[k] > b.maximum[k])
                return fail("Invalid bone limit or pose");
        boneIds.insert(b.id); state.bones.append(b);
    }
    // Constraints and keys may name any bone, so they parse after all bones.
    QSet<QString> constraintIds;
    for (int i = 0; version == 2 && i < boneRows.size(); ++i) {
        const auto item = boneRows[i].toObject();
        const auto constraints = item.value("constraints").toArray();
        const auto keys = item.value("keys").toArray();
        if (!item.value("constraints").isArray() || !item.value("keys").isArray()
            || constraints.size() > 4 || keys.size() > 1024) return fail("Invalid bone constraints or keys");
        Bone &b = state.bones[i];
        for (const auto row : constraints) {
            const auto c = row.toObject(); Constraint constraint;
            constraint.id = c.value("id").toString(); constraint.type = c.value("type").toString();
            constraint.targetId = c.value("targetId").toString();
            constraint.influence = c.value("influence").toDouble(-1);
            if (!validId(constraint.id) || constraintIds.contains(constraint.id)
                || constraint.type != QLatin1String("copyRotation") || !boneIds.contains(constraint.targetId)
                || constraint.targetId == b.id || !(constraint.influence >= 0 && constraint.influence <= 1))
                return fail("Invalid bone constraint");
            constraintIds.insert(constraint.id); b.constraints.append(constraint);
        }
        int previous = -1;
        for (const auto row : keys) {
            const auto k = row.toObject(); PoseKey key;
            const QJsonValue frame = k.value("frame");
            key.frame = frame.toInt(-1);
            if (!frame.isDouble() || frame.toDouble() != key.frame || key.frame <= previous || key.frame > 100000
                || !readVec(k.value("pose"), &key.pose) || !withinLimits(b, key.pose))
                return fail("Invalid or unordered pose key");
            previous = key.frame; b.keys.append(key);
        }
    }
    for (const auto &b : state.bones)
        for (const auto &c : b.constraints)
            if (constraintReaches(state, c.targetId, b.id)) return fail("Constraint cycle in mesh file");
    for (int i = 0; i < vertexRows.size(); ++i) {
        const auto weights = vertexRows[i].toObject().value("weights").toObject();
        if (weights.size() > 4) return fail("Too many vertex influences");
        double sum = 0;
        for (auto it = weights.begin(); it != weights.end(); ++it) {
            if (!boneIds.contains(it.key()) || !it.value().isDouble() || it.value().toDouble() <= 0
                || it.value().toDouble() > 1 || !std::isfinite(it.value().toDouble()))
                return fail("Invalid vertex influence");
            state.vertices[i].weights.insert(it.key(), it.value().toDouble()); sum += it.value().toDouble();
        }
        if (!weights.isEmpty() && qAbs(sum - 1.0) > .001) return fail("Vertex weights do not sum to one");
    }
    QSet<QString> faceIds, faceTopology;
    QHash<QString, int> edgeUses;
    for (const auto row : faceRows) {
        if (!row.isObject()) return fail("Invalid face record");
        const auto item = row.toObject(); Face f;
        f.id = item.value("id").toString();
        const auto verts = item.value("vertexIds").toArray();
        const auto uvNames = item.value("uvIds").toArray();
        const auto points = item.value("uv").toArray();
        if (!validId(f.id) || faceIds.contains(f.id) || verts.size() != 3
            || uvNames.size() != 3 || points.size() != 3) return fail("Invalid face identity or corners");
        for (int k = 0; k < 3; ++k) {
            const QString vertexId = verts[k].toString();
            f.vertices[k] = -1;
            for (int v = 0; v < state.vertices.size(); ++v)
                if (state.vertices[v].id == vertexId) { f.vertices[k] = v; break; }
            f.uvIds[k] = uvNames[k].toString();
            if (f.vertices[k] < 0 || !validId(f.uvIds[k]) || uvIds.contains(f.uvIds[k])
                || !readUv(points[k], &f.uv[k])) return fail("Invalid face vertex or UV corner");
            uvIds.insert(f.uvIds[k]);
        }
        if (f.vertices[0] == f.vertices[1] || f.vertices[1] == f.vertices[2]
            || f.vertices[0] == f.vertices[2]
            || QVector3D::crossProduct(state.vertices[f.vertices[1]].rest - state.vertices[f.vertices[0]].rest,
                                       state.vertices[f.vertices[2]].rest - state.vertices[f.vertices[0]].rest).lengthSquared() < 1e-12f)
            return fail("Degenerate face in mesh file");
        QList<int> sorted{f.vertices[0], f.vertices[1], f.vertices[2]};
        std::sort(sorted.begin(), sorted.end());
        const QString topology = QString::number(sorted[0]) + u'|'
                                 + QString::number(sorted[1]) + u'|'
                                 + QString::number(sorted[2]);
        if (faceTopology.contains(topology)) return fail("Duplicate face in mesh file");
        faceTopology.insert(topology);
        for (int k = 0; k < 3; ++k) {
            const auto key = edgeKey(state.vertices[f.vertices[k]].id,
                                     state.vertices[f.vertices[(k + 1) % 3]].id);
            if (++edgeUses[key] > 2) return fail("Nonmanifold edge in mesh file");
        }
        faceIds.insert(f.id); state.faces.append(f);
    }
    for (const auto row : seamRows) {
        const auto pair = row.toArray();
        if (!row.isArray() || pair.size() != 2 || !ids.contains(pair[0].toString())
            || !ids.contains(pair[1].toString()) || pair[0] == pair[1]) return fail("Invalid UV seam");
        const QString key = edgeKey(pair[0].toString(), pair[1].toString());
        bool found = false;
        for (const auto &f : state.faces) for (int k = 0; k < 3; ++k)
            if (edgeKey(state.vertices[f.vertices[k]].id,
                        state.vertices[f.vertices[(k + 1) % 3]].id) == key) found = true;
        if (!found || state.seams.contains(key)) return fail("Unknown or duplicate UV seam");
        state.seams.insert(key);
    }
    QJsonObject textures;
    if (version == 1) textures.insert("baseColor", root.value("texturePng"));
    else if (root.value("textures").isObject()) textures = root.value("textures").toObject();
    QSize size;
    const QStringList channels = textureChannels();
    for (int c = 0; c < ChannelCount; ++c) {
        // AGENT-GUARD: version 1 files carry only baseColor ("texturePng");
        // that channel is still required, so a file with no texture is
        // refused here instead of loading null images that save unreadably.
        if (version == 1 && c != BaseColor) continue;
        const auto encoded = textures.value(channels[c]).toString();
        if (encoded.isEmpty() || encoded.size() > 8 * 1024 * 1024) return fail("Invalid texture data");
        QByteArray bytes = QByteArray::fromBase64(encoded.toLatin1());
        QBuffer buffer(&bytes); buffer.open(QIODevice::ReadOnly); QImageReader reader(&buffer, "PNG");
        const auto channelSize = reader.size();
        if (channelSize.width() < 1 || channelSize.height() < 1 || channelSize.width() > 1024
            || channelSize.height() > 1024 || (size.isValid() && channelSize != size))
            return fail("Texture dimensions are out of bounds or differ between channels");
        size = channelSize;
        QImage decoded = reader.read();
        if (decoded.isNull()) return fail("Invalid texture pixels");
        if (c == BaseColor) {
            if (decoded.colorSpace().isValid() && decoded.colorSpace() != QColorSpace::SRgb)
                decoded.convertToColorSpace(QColorSpace::SRgb);
        } else {
            decoded.setColorSpace(QColorSpace()); // linear scalar data: never colour-converted
        }
        state.textures[c] = decoded.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        if (c == BaseColor) state.textures[c].setColorSpace(QColorSpace::SRgb);
    }
    if (!size.isValid()) return fail("Invalid texture data");
    for (int c = 0; c < ChannelCount; ++c)
        if (state.textures[c].isNull()) state.textures[c] = blankChannel(c, size.width(), size.height());
    *result = state; return true;
}

bool MeshRigDocument::fromJson(const QString &json) {
    if (m_painting) return fail("Finish painting before loading");
    State next; if (!stateFromJson(json, &next)) return false;
    return commit(next, QStringLiteral("Load mesh document"));
}
bool MeshRigDocument::saveToFile(const QString &path) const {
    const QByteArray bytes = toJson().toUtf8();
    if (bytes.size() > 16 * 1024 * 1024) return fail("Mesh document exceeds 16 MiB");
    QSaveFile file(path); if (!file.open(QIODevice::WriteOnly)) return fail(file.errorString());
    if (file.write(bytes) != bytes.size() || !file.commit()) return fail(file.errorString());
    return true;
}
bool MeshRigDocument::loadFromFile(const QString &path) {
    QFile file(path); if (!file.open(QIODevice::ReadOnly)) return fail(file.errorString());
    if (file.size() > 16 * 1024 * 1024) return fail("Mesh document exceeds 16 MiB");
    return fromJson(QString::fromUtf8(file.readAll()));
}

} // namespace QindaTK
