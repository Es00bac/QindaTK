// SPDX-License-Identifier: LGPL-3.0-or-later
#include "mesh_rig_geometry.h"

#include "mesh_rig_document.h"

#include <QHash>
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
static_assert(sizeof(Vertex) == sizeof(float) * MeshRigGeometry::FloatsPerVertex);

Vertex packedVertex(const QVector3D &position, const QVector3D &normal, const QPointF &uv)
{
    // AGENT-NOTE: mesh UV v=0 is bottom-left; raw QImage texture rows start at
    // the top. Flip v only for this Qt Quick 3D upload, never in document state.
    return {{position.x(), position.y(), position.z()},
            {normal.x(), normal.y(), normal.z()},
            {float(uv.x()), float(1.0 - uv.y())}};
}
}

MeshRigGeometry::MeshRigGeometry(QQuick3DObject *parent)
    : QQuick3DGeometry(parent), m_textureData(new QQuick3DTextureData(this)),
      m_roughnessData(new QQuick3DTextureData(this)), m_metalnessData(new QQuick3DTextureData(this))
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

QByteArray MeshRigGeometry::packVertices(const MeshRigDocument *document, int *triangles)
{
    QByteArray bytes;
    int count = 0;
    if (document) {
        const QVariantList vertices = document->vertices();
        const QVariantList faces = document->faces();
        QHash<QString, QVector3D> positions;
        positions.reserve(vertices.size());
        for (const QVariant &item : vertices) {
            const QVariantMap vertex = item.toMap();
            positions.insert(vertex.value(QStringLiteral("vertexId")).toString(),
                             vertex.value(QStringLiteral("posedPosition")).value<QVector3D>());
        }
        bytes.reserve(faces.size() * 3 * int(sizeof(Vertex)));
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
            }
            ++count;
        }
    }
    if (triangles) *triangles = count;
    return bytes;
}

QByteArray MeshRigGeometry::packTexture(const QImage &image)
{
    if (image.isNull()) return {};
    const QImage rgba = image.convertToFormat(QImage::Format_RGBA8888);
    QByteArray bytes;
    bytes.reserve(rgba.width() * rgba.height() * 4);
    for (int y = 0; y < rgba.height(); ++y) // drop any scanline padding
        bytes.append(reinterpret_cast<const char *>(rgba.constScanLine(y)), rgba.width() * 4);
    return bytes;
}

void MeshRigGeometry::upload(QQuick3DTextureData *target, const QImage &image)
{
    target->setSize(image.size());
    target->setFormat(image.isNull() ? QQuick3DTextureData::None : QQuick3DTextureData::RGBA8);
    target->setHasTransparency(!image.isNull() && image.hasAlphaChannel());
    target->setTextureData(packTexture(image));
}

void MeshRigGeometry::rebuild()
{
    clear();
    setPrimitiveType(PrimitiveType::Triangles);
    setStride(sizeof(Vertex));
    addAttribute(Attribute::PositionSemantic, offsetof(Vertex, position), Attribute::F32Type);
    addAttribute(Attribute::NormalSemantic, offsetof(Vertex, normal), Attribute::F32Type);
    addAttribute(Attribute::TexCoordSemantic, offsetof(Vertex, uv), Attribute::F32Type);

    const QByteArray bytes = packVertices(m_document, &m_triangleCount);
    const auto *vertex = reinterpret_cast<const Vertex *>(bytes.constData());
    const qsizetype corners = bytes.size() / qsizetype(sizeof(Vertex));
    if (corners > 0) {
        QVector3D minimum(vertex[0].position[0], vertex[0].position[1], vertex[0].position[2]);
        QVector3D maximum = minimum;
        for (qsizetype i = 1; i < corners; ++i)
            for (int axis = 0; axis < 3; ++axis) {
                minimum[axis] = std::min(minimum[axis], vertex[i].position[axis]);
                maximum[axis] = std::max(maximum[axis], vertex[i].position[axis]);
            }
        setBounds(minimum, maximum);
    }
    setVertexData(bytes);
    upload(m_textureData, m_document ? m_document->texture(QStringLiteral("baseColor")) : QImage());
    upload(m_roughnessData, m_document ? m_document->texture(QStringLiteral("roughness")) : QImage());
    upload(m_metalnessData, m_document ? m_document->texture(QStringLiteral("metalness")) : QImage());
    update();
    emit geometryUpdated();
}

} // namespace QindaTK
