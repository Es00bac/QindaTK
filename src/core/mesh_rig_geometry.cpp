// SPDX-License-Identifier: LGPL-3.0-or-later
#include "mesh_rig_geometry.h"

#include "mesh_rig_document.h"

#include <QHash>
#include <QImage>
#include <QPointF>
#include <QVariantMap>
#include <QVector3D>
#include <algorithm>
#include <array>
#include <cstddef>

namespace QindaTK {

namespace {
struct Vertex {
    float position[3];
    float normal[3];
    float uv[2];
};
static_assert(sizeof(Vertex) == sizeof(float) * 8);

Vertex packedVertex(const QVector3D &position, const QVector3D &normal, const QPointF &uv)
{
    return {{position.x(), position.y(), position.z()},
            {normal.x(), normal.y(), normal.z()},
            {float(uv.x()), float(uv.y())}};
}
}

MeshRigGeometry::MeshRigGeometry(QQuick3DObject *parent)
    : QQuick3DGeometry(parent), m_textureData(new QQuick3DTextureData(this))
{
    rebuild();
}

void MeshRigGeometry::setDocument(MeshRigDocument *document)
{
    if (m_document == document) return;
    if (m_document) disconnect(m_document, nullptr, this, nullptr);
    m_document = document;
    if (m_document) {
        connect(m_document, &MeshRigDocument::changed, this, &MeshRigGeometry::rebuild);
        connect(m_document, &QObject::destroyed, this, [this] {
            m_document = nullptr;
            rebuild();
            emit documentChanged();
        });
    }
    rebuild();
    emit documentChanged();
}

void MeshRigGeometry::rebuild()
{
    clear();
    setPrimitiveType(PrimitiveType::Triangles);
    setStride(sizeof(Vertex));
    addAttribute(Attribute::PositionSemantic, offsetof(Vertex, position), Attribute::F32Type);
    addAttribute(Attribute::NormalSemantic, offsetof(Vertex, normal), Attribute::F32Type);
    addAttribute(Attribute::TexCoordSemantic, offsetof(Vertex, uv), Attribute::F32Type);

    QByteArray bytes;
    m_triangleCount = 0;
    if (m_document) {
        const QVariantList vertices = m_document->vertices();
        const QVariantList faces = m_document->faces();
        QHash<QString, QVector3D> positions;
        positions.reserve(vertices.size());
        for (const QVariant &item : vertices) {
            const QVariantMap vertex = item.toMap();
            positions.insert(vertex.value(QStringLiteral("vertexId")).toString(),
                             vertex.value(QStringLiteral("posedPosition")).value<QVector3D>());
        }
        bytes.reserve(faces.size() * 3 * int(sizeof(Vertex)));
        QVector3D minimum, maximum;
        bool haveBounds = false;
        for (const QVariant &item : faces) {
            const QVariantMap face = item.toMap();
            const QVariantList ids = face.value(QStringLiteral("vertexIds")).toList();
            const QVariantList uv = face.value(QStringLiteral("uv")).toList();
            if (ids.size() != 3 || uv.size() != 3) continue;
            std::array<QVector3D, 3> point;
            bool valid = true;
            for (int i = 0; i < 3; ++i) {
                const QString id = ids[i].toString();
                if (!positions.contains(id)) { valid = false; break; }
                point[i] = positions.value(id);
            }
            if (!valid) continue;
            const QVector3D normal = QVector3D::crossProduct(point[1] - point[0],
                                                               point[2] - point[0]).normalized();
            if (normal.isNull()) continue;
            for (int i = 0; i < 3; ++i) {
                const Vertex vertex = packedVertex(point[i], normal, uv[i].value<QPointF>());
                bytes.append(reinterpret_cast<const char *>(&vertex), sizeof(vertex));
                if (!haveBounds) { minimum = maximum = point[i]; haveBounds = true; }
                minimum.setX(std::min(minimum.x(), point[i].x()));
                minimum.setY(std::min(minimum.y(), point[i].y()));
                minimum.setZ(std::min(minimum.z(), point[i].z()));
                maximum.setX(std::max(maximum.x(), point[i].x()));
                maximum.setY(std::max(maximum.y(), point[i].y()));
                maximum.setZ(std::max(maximum.z(), point[i].z()));
            }
            ++m_triangleCount;
        }
        if (haveBounds) setBounds(minimum, maximum);
    }
    setVertexData(bytes);
    const QImage texture = m_document ? m_document->texture().convertToFormat(QImage::Format_RGBA8888)
                                      : QImage();
    m_textureData->setSize(texture.size());
    m_textureData->setFormat(texture.isNull() ? QQuick3DTextureData::None
                                             : QQuick3DTextureData::RGBA8);
    m_textureData->setHasTransparency(texture.hasAlphaChannel());
    m_textureData->setTextureData(texture.isNull() ? QByteArray()
        : QByteArray(reinterpret_cast<const char *>(texture.constBits()), texture.sizeInBytes()));
    emit geometryUpdated();
}

} // namespace QindaTK
