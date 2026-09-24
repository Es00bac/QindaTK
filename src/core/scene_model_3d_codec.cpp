// SPDX-License-Identifier: LGPL-3.0-or-later
#include "scene_model_3d.h"
#include <QFile>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSet>
#include <cmath>

namespace QindaTK {
QString SceneModel3D::toJson() const {
    QJsonArray records;
    // A save during a gesture persists the last committed state.
    const auto &state = editing() ? m_before : m_entities;
    for (const auto &e : state) {
        records.append(QJsonObject{{"id", e.id}, {"parent", e.parent}, {"name", e.name},
            {"primitive", e.primitive}, {"color", e.color.name(QColor::HexArgb)}, {"visible", e.visible},
            {"material", QJsonObject{{"baseColor", e.color.name(QColor::HexArgb)},
                                      {"roughness", e.roughness}, {"metalness", e.metalness},
                                      {"emissiveColor", e.emissiveColor.name(QColor::HexArgb)}}},
            {"position", QJsonArray{e.position.x(), e.position.y(), e.position.z()}},
            {"rotation", QJsonArray{e.rotation.scalar(), e.rotation.x(), e.rotation.y(), e.rotation.z()}},
            {"scale", e.scale}});
    }
    return QString::fromUtf8(QJsonDocument(QJsonObject{{"format", "QindaTK.Scene3D"},
        {"version", 2}, {"units", "world"}, {"entities", records}}).toJson(QJsonDocument::Compact));
}
bool SceneModel3D::fromJson(const QString &json) {
    if (editing() || json.size() > 4 * 1024 * 1024) return fail("Active transform or scene file too large");
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(json.toUtf8(), &error);
    auto obj = doc.object();
    if (error.error != QJsonParseError::NoError || obj["format"] != "QindaTK.Scene3D"
        || (obj["version"].toInt() != 1 && obj["version"].toInt() != 2)
        || obj["units"] != "world" || !obj["entities"].isArray())
        return fail("Unsupported scene document");
    auto array = obj["entities"].toArray();
    if (array.size() > 512) return fail("Scene entity limit exceeded");
    QVector<Entity> incoming;
    QSet<QString> ids;
    for (const auto &value : array) {
        const auto o = value.toObject(); Entity e;
        e.id = o["id"].toString(); e.parent = o["parent"].toString();
        e.name = o["name"].toString(); e.primitive = o["primitive"].toString();
        e.color = QColor(o["color"].toString()); e.visible = o["visible"].toBool(true);
        if (obj["version"].toInt() == 2) {
            if (!o["material"].isObject()) return fail("Missing scene material");
            const auto material = o["material"].toObject();
            e.color = QColor(material["baseColor"].toString());
            e.emissiveColor = QColor(material["emissiveColor"].toString());
            if (!material["roughness"].isDouble() || !material["metalness"].isDouble())
                return fail("Invalid scene material");
            e.roughness = material["roughness"].toDouble();
            e.metalness = material["metalness"].toDouble();
            if (!e.emissiveColor.isValid() || !std::isfinite(e.roughness)
                || !std::isfinite(e.metalness) || e.roughness < 0 || e.roughness > 1
                || e.metalness < 0 || e.metalness > 1) return fail("Invalid scene material");
        }
        auto p = o["position"].toArray(), r = o["rotation"].toArray();
        if (e.id.isEmpty() || ids.contains(e.id) || !e.color.isValid() || p.size() != 3 || r.size() != 4
            || (e.primitive != "cube" && e.primitive != "sphere" && e.primitive != "cylinder"))
            return fail("Invalid scene entity");
        for (auto n : p) if (!n.isDouble() || !std::isfinite(n.toDouble())) return fail("Invalid position");
        for (auto n : r) if (!n.isDouble() || !std::isfinite(n.toDouble())) return fail("Invalid rotation");
        e.position = QVector3D(p[0].toDouble(), p[1].toDouble(), p[2].toDouble());
        e.rotation = QQuaternion(r[0].toDouble(), r[1].toDouble(), r[2].toDouble(), r[3].toDouble());
        e.scale = o["scale"].toDouble();
        if (e.position.length() > 1e6f || !std::isfinite(e.rotation.lengthSquared())
            || e.rotation.lengthSquared() < .0001f || !std::isfinite(e.scale) || e.scale < .001f || e.scale > 1000.f)
            return fail("Scene transform out of range");
        if (std::abs(e.rotation.lengthSquared() - 1.f) > 1e-5f) e.rotation.normalize();
        ids.insert(e.id); incoming.append(e);
    }
    for (const auto &e : incoming) {
        QSet<QString> seen{e.id}; QString p = e.parent;
        while (!p.isEmpty()) {
            if (!ids.contains(p) || seen.contains(p) || seen.size() > 32) return fail("Invalid scene hierarchy");
            seen.insert(p);
            for (const auto &a : incoming) if (a.id == p) { p = a.parent; break; }
        }
    }
    const auto old = m_entities;
    m_entities = incoming;
    if (!validWorld()) { m_entities = old; return fail("World transform exceeds supported range"); }
    m_entities = old; remember(); restore(incoming); return true;
}
bool SceneModel3D::saveToFile(const QString &path) {
    QSaveFile file(path); const auto bytes = toJson().toUtf8();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return fail("Cannot save scene: " + file.errorString());
    m_error.clear(); emit errorChanged(); return true;
}
bool SceneModel3D::loadFromFile(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 4 * 1024 * 1024)
        return fail("Cannot read scene or file exceeds limit");
    return fromJson(QString::fromUtf8(file.readAll()));
}
} // namespace QindaTK
