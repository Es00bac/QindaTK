// SPDX-License-Identifier: LGPL-3.0-or-later
#include "scientific_geometry3d.h"

#include "scientific_scene3d.h"

#include <QVector3D>
#include <cstddef>

namespace QindaTK {
namespace {
struct Vertex {
    float position[3];
    float normal[3];
    float color[4];
};
static_assert(sizeof(Vertex) == sizeof(float) * ScientificGeometry3D::FloatsPerVertex);
void append(QByteArray *bytes, const QVector3D &p, const QVector3D &n, const QColor &c)
{
    const Vertex v{{p.x(), p.y(), p.z()}, {n.x(), n.y(), n.z()}, {c.redF(), c.greenF(), c.blueF(), c.alphaF()}};
    bytes->append(reinterpret_cast<const char *>(&v), sizeof(v));
}
// AGENT-NOTE: an octahedron per point: GPU point sprites have no portable
// size across RHI backends, triangles do.
void appendOctahedron(QByteArray *bytes, const QVector3D &centre, float r, const QColor &color)
{
    const QVector3D axes[6] = {{r, 0, 0}, {-r, 0, 0}, {0, r, 0}, {0, -r, 0}, {0, 0, r}, {0, 0, -r}};
    const int faces[8][3] = {{0, 2, 4}, {2, 1, 4}, {1, 3, 4}, {3, 0, 4}, {2, 0, 5}, {1, 2, 5}, {3, 1, 5}, {0, 3, 5}};
    for (const auto &f : faces) {
        const QVector3D n = (axes[f[0]] + axes[f[1]] + axes[f[2]]).normalized();
        for (int k : f) append(bytes, centre + axes[k], n, color);
    }
}
static_assert(ScientificGeometry3D::VerticesPerPoint == 8 * 3);
float pointRadius(const ScientificScene3D *scene) { return float(scene->sceneExtent()) * 0.012f; }
}

ScientificGeometry3D::ScientificGeometry3D(QQuick3DObject *parent) : QQuick3DGeometry(parent) { rebuild(); }

void ScientificGeometry3D::setScene(ScientificScene3D *value)
{
    if (m_scene == value) return;
    if (m_scene) disconnect(m_scene, nullptr, this, nullptr);
    m_scene = value;
    if (value) {
        connect(value, &ScientificScene3D::meshChanged, this, &ScientificGeometry3D::rebuild);
        connect(value, &ScientificScene3D::selectionChanged, this, &ScientificGeometry3D::recolorSelection);
        connect(value, &QObject::destroyed, this, [this] { m_scene = nullptr; rebuild(); emit sceneChanged(); });
    }
    rebuild(); emit sceneChanged();
}
void ScientificGeometry3D::setPart(Part value)
{ if (m_part == value) return; m_part = value; rebuild(); emit sceneChanged(); }

void ScientificGeometry3D::rebuild()
{
    clear();
    setStride(sizeof(Vertex));
    addAttribute(Attribute::PositionSemantic, offsetof(Vertex, position), Attribute::F32Type);
    addAttribute(Attribute::NormalSemantic, offsetof(Vertex, normal), Attribute::F32Type);
    addAttribute(Attribute::ColorSemantic, offsetof(Vertex, color), Attribute::F32Type);
    QByteArray bytes;
    m_primitives = 0; m_selectedIndex = -1;
    const float e = m_scene ? float(m_scene->sceneExtent()) : 1.0f;
    if (m_scene && m_scene->problem().isEmpty()) {
        if (m_part == Surface) {
            setPrimitiveType(PrimitiveType::Triangles);
            const auto &mesh = m_scene->surfaceMesh();
            // Expanded (non-indexed) triangles keep one upload path for all parts.
            for (quint32 index : mesh.indices)
                append(&bytes, mesh.positions[index], mesh.normals[index], mesh.colors[index]);
            m_primitives = int(mesh.indices.size() / 3);
        } else if (m_part == Points) {
            setPrimitiveType(PrimitiveType::Triangles);
            // Bounded by ScientificScene3D::pointLimit (thinned there).
            const auto &positions = m_scene->pointPositions();
            const auto &ids = m_scene->pointIds();
            const QColor normal = m_scene->property("pointColor").value<QColor>();
            const QColor selected = m_scene->property("selectionColor").value<QColor>();
            bytes.reserve(qsizetype(positions.size()) * VerticesPerPoint * qsizetype(sizeof(Vertex)));
            for (int i = 0; i < positions.size(); ++i) {
                const bool isSelected = ids[i] == m_scene->selectedPointId();
                if (isSelected) m_selectedIndex = i;
                appendOctahedron(&bytes, positions[i], pointRadius(m_scene), isSelected ? selected : normal);
            }
            m_primitives = int(positions.size()) * VerticesPerPoint / 3;
        } else {
            setPrimitiveType(PrimitiveType::Lines);
            const QColor color = m_scene->property("frameColor").value<QColor>();
            const QVector<QVector3D> segments = m_scene->frameSegments();
            for (const QVector3D &p : segments) append(&bytes, p, QVector3D(0, 1, 0), color);
            m_primitives = int(segments.size() / 2);
        }
    }
    // The frame bounds everything drawn inside the domain; points or surface
    // values beyond it still render, only culling bounds use the cube.
    setBounds(QVector3D(-e, -e, -e) * 1.1f, QVector3D(e, e, e) * 1.1f);
    setVertexData(bytes);
    update();
    emit geometryUpdated();
}

void ScientificGeometry3D::recolorSelection()
{
    if (m_part != Points || !m_scene || !m_scene->problem().isEmpty()) return;
    const auto &positions = m_scene->pointPositions();
    const auto &ids = m_scene->pointIds();
    const int next = int(ids.indexOf(m_scene->selectedPointId()));
    if (next == m_selectedIndex) return;
    const qsizetype pointBytes = VerticesPerPoint * qsizetype(sizeof(Vertex));
    if (vertexData().size() != positions.size() * pointBytes) { rebuild(); return; }
    const QColor normal = m_scene->property("pointColor").value<QColor>();
    const QColor selected = m_scene->property("selectionColor").value<QColor>();
    for (const int index : {m_selectedIndex, next}) {
        if (index < 0 || index >= positions.size()) continue;
        QByteArray bytes;
        appendOctahedron(&bytes, positions[index], pointRadius(m_scene), index == next ? selected : normal);
        setVertexData(int(index * pointBytes), bytes);
    }
    m_selectedIndex = next;
    update();
    emit geometryUpdated();
}

} // namespace QindaTK
