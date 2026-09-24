// SPDX-License-Identifier: LGPL-3.0-or-later
#include "scene_model_3d.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <algorithm>
#include <cmath>

namespace QindaTK {
// Pure parser shared by the native scene loader and DocumentCodec. It touches
// no QObject, so inspect/decode can validate files on a worker thread.
bool SceneModel3D::decodeNative(const QByteArray &bytes, QVector<Entity> *entities,
                                SnapshotHeader *header, QJsonObject *extensions, QString *problem)
{
    auto reject = [&](const QString &message) { if (problem) *problem = message; return false; };
    if (bytes.size() > 4 * 1024 * 1024) return reject(QStringLiteral("Scene file exceeds 4 MiB"));
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return reject(QStringLiteral("Invalid scene JSON"));
    const QJsonObject root = document.object();
    const int version = root.value("version").toInt();
    if (root.value("format") != QStringLiteral("QindaTK.Scene3D")
        || (version != 1 && version != 2 && version != 3)
        || root.value("units") != QStringLiteral("world") || !root.value("entities").isArray())
        return reject(QStringLiteral("Unsupported scene document"));
    const QJsonArray records = root.value("entities").toArray();
    if (records.size() > 512) return reject(QStringLiteral("Scene entity limit exceeded"));

    SnapshotHeader parsedHeader;
    parsedHeader.schemaId = QStringLiteral("qindatk.scene3d.v1");
    if (version == 3) {
        parsedHeader.documentId = DocumentId::parse(root.value("documentId").toString());
        bool revisionOk = false, generationOk = false;
        parsedHeader.revision.value = root.value("revision").toString().toULongLong(&revisionOk);
        parsedHeader.resetGeneration = root.value("resetGeneration").toString().toULongLong(&generationOk);
        if (!parsedHeader.documentId.isValid() || !revisionOk || !parsedHeader.revision.value
            || !generationOk || !root.value("extensions").isObject())
            return reject(QStringLiteral("Invalid scene session header"));
        if (QJsonDocument(root.value("extensions").toObject()).toJson(QJsonDocument::Compact).size() > 64 * 1024)
            return reject(QStringLiteral("Scene extensions exceed 64 KiB"));
    } else {
        // Legacy native files carry no identity. Deriving one from the bytes
        // lets inspect and decode agree and makes duplicate opens detectable.
        QByteArray hash = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).left(16);
        hash[6] = char((quint8(hash[6]) & 0x0f) | 0x50);
        hash[8] = char((quint8(hash[8]) & 0x3f) | 0x80);
        parsedHeader.documentId = {QUuid::fromRfc4122(hash)};
        parsedHeader.revision.value = 1;
    }

    QVector<Entity> parsed;
    parsed.reserve(records.size());
    QSet<QString> ids;
    for (const QJsonValue &value : records) {
        if (!value.isObject()) return reject(QStringLiteral("Invalid scene entity"));
        const QJsonObject record = value.toObject();
        Entity e;
        e.id = record.value("id").toString();
        e.parent = record.value("parent").toString();
        e.name = record.value("name").toString();
        e.primitive = record.value("primitive").toString();
        e.color = QColor(record.value("color").toString());
        e.visible = record.value("visible").toBool(true);
        if (version >= 2) {
            if (!record.value("material").isObject()) return reject(QStringLiteral("Missing scene material"));
            const QJsonObject material = record.value("material").toObject();
            e.color = QColor(material.value("baseColor").toString());
            e.emissiveColor = QColor(material.value("emissiveColor").toString());
            if (!material.value("roughness").isDouble() || !material.value("metalness").isDouble())
                return reject(QStringLiteral("Invalid scene material"));
            e.roughness = material.value("roughness").toDouble();
            e.metalness = material.value("metalness").toDouble();
            if (!e.emissiveColor.isValid() || !std::isfinite(e.roughness)
                || !std::isfinite(e.metalness) || e.roughness < 0 || e.roughness > 1
                || e.metalness < 0 || e.metalness > 1)
                return reject(QStringLiteral("Invalid scene material"));
        }
        const QJsonArray position = record.value("position").toArray();
        const QJsonArray rotation = record.value("rotation").toArray();
        if (e.id.isEmpty() || ids.contains(e.id) || !e.color.isValid()
            || (e.primitive != "cube" && e.primitive != "sphere" && e.primitive != "cylinder")
            || position.size() != 3 || rotation.size() != 4 || !record.value("scale").isDouble())
            return reject(QStringLiteral("Invalid scene entity"));
        for (const QJsonValue &part : position)
            if (!part.isDouble() || !std::isfinite(part.toDouble())) return reject(QStringLiteral("Invalid position"));
        for (const QJsonValue &part : rotation)
            if (!part.isDouble() || !std::isfinite(part.toDouble())) return reject(QStringLiteral("Invalid rotation"));
        e.position = QVector3D(position[0].toDouble(), position[1].toDouble(), position[2].toDouble());
        e.rotation = QQuaternion(rotation[0].toDouble(), rotation[1].toDouble(),
                                 rotation[2].toDouble(), rotation[3].toDouble());
        e.scale = record.value("scale").toDouble();
        if (e.position.length() > 1e6f || !std::isfinite(e.rotation.lengthSquared())
            || e.rotation.lengthSquared() < .0001f || !std::isfinite(e.scale)
            || e.scale < .001f || e.scale > 1000.f)
            return reject(QStringLiteral("Scene transform out of range"));
        if (std::abs(e.rotation.lengthSquared() - 1.f) > 1e-5f) e.rotation.normalize();
        ids.insert(e.id);
        parsed.append(std::move(e));
    }
    for (const Entity &entity : parsed) {
        QSet<QString> seen{entity.id};
        QString parent = entity.parent;
        while (!parent.isEmpty()) {
            if (!ids.contains(parent) || seen.contains(parent) || seen.size() > 32)
                return reject(QStringLiteral("Invalid scene hierarchy"));
            seen.insert(parent);
            for (const Entity &candidate : parsed)
                if (candidate.id == parent) { parent = candidate.parent; break; }
        }
    }
    for (const Entity &entity : parsed) {
        QVector3D position = entity.position;
        QQuaternion rotation = entity.rotation;
        float scale = entity.scale;
        QString parent = entity.parent;
        while (!parent.isEmpty()) {
            const auto it = std::find_if(parsed.cbegin(), parsed.cend(),
                                         [&](const Entity &item) { return item.id == parent; });
            const Entity &ancestor = *it; // hierarchy was validated above
            position = ancestor.position + ancestor.rotation.rotatedVector(position * ancestor.scale);
            rotation = (ancestor.rotation * rotation).normalized();
            scale *= ancestor.scale;
            parent = ancestor.parent;
        }
        if (!std::isfinite(position.x()) || !std::isfinite(position.y()) || !std::isfinite(position.z())
            || position.length() > 1e6f || !std::isfinite(scale) || scale < 1e-6f || scale > 1e6f)
            return reject(QStringLiteral("World transform exceeds supported range"));
    }
    if (entities) *entities = std::move(parsed);
    if (header) *header = parsedHeader;
    if (extensions) *extensions = version == 3 ? root.value("extensions").toObject() : QJsonObject{};
    if (problem) problem->clear();
    return true;
}
} // namespace QindaTK
