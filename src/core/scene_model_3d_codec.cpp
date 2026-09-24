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
    if (editing()) return fail("Finish the active transform first");
    QVector<Entity> incoming;
    QString problem;
    if (!decodeNative(json.toUtf8(), &incoming, nullptr, nullptr, &problem)) return fail(problem);
    return commitState(m_entities, incoming, QStringLiteral("Load 3D scene"));
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
