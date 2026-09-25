// SPDX-License-Identifier: LGPL-3.0-or-later
// The Qt Quick 3D upload path of ScientificScene3D (WP-085, D-140), headless:
// ScientificGeometry3D's vertex buffers must be exactly the scene's surface
// mesh, drawn points and frame, so the GPU draws what the software view and
// export are tested to draw. No RHI is needed to build or read the buffers;
// on-screen drawing and device loss are tst_scientific_plot3d_render.
#include "scientific_geometry3d.h"
#include "scientific_scene3d.h"

#include <QSignalSpy>
#include <QtTest>
#include <cstring>
#include <memory>

using namespace QindaTK;

namespace {
constexpr int Floats = ScientificGeometry3D::FloatsPerVertex;
QVector<float> floats(const QByteArray &bytes)
{
    QVector<float> result(bytes.size() / int(sizeof(float)));
    std::memcpy(result.data(), bytes.constData(), size_t(result.size()) * sizeof(float));
    return result;
}
QVector3D position(const QVector<float> &v, int vertex)
{ return QVector3D(v[vertex * Floats], v[vertex * Floats + 1], v[vertex * Floats + 2]); }
QVector3D normal(const QVector<float> &v, int vertex)
{ return QVector3D(v[vertex * Floats + 3], v[vertex * Floats + 4], v[vertex * Floats + 5]); }
QRgb color(const QVector<float> &v, int vertex)
{
    const float *c = v.constData() + vertex * Floats + 6;
    return QColor::fromRgbF(c[0], c[1], c[2], c[3]).rgba();
}
QRgb rgba(Qt::GlobalColor c) { return QColor(c).rgba(); }
// z = x * y on a 3 x 3 grid, and two probes; the scene fits the domain.
struct Fixture {
    ScientificGrid grid;
    ScientificDataset points;
    ScientificScene3D scene;
    Fixture()
    {
        grid.setSourceId("product"); grid.setXUnit("m"); grid.setYUnit("m"); grid.setZUnit("K");
        grid.replaceGrid({0.0, 1.0, 2.0}, {0.0, 1.0, 2.0}, {0.0, 0.0, 0.0, 0.0, 1.0, 2.0, 0.0, 2.0, 4.0});
        points.setSourceId("probes"); points.setKind(ScientificDataset::Scatter); points.setDimensions(3);
        points.setXUnit("m"); points.setYUnit("m"); points.setZUnit("K");
        points.replaceSamples({QVariantMap{{"id", "p0"}, {"x", 0.0}, {"y", 0.0}, {"z", 0.0}},
                               QVariantMap{{"id", "p1"}, {"x", 2.0}, {"y", 2.0}, {"z", 4.0}}});
        scene.setProperty("surfaceColors", QVariant::fromValue(QList<QColor>{QColor(0, 0, 255), QColor(255, 0, 0)}));
        scene.setProperty("pointColor", QColor(Qt::black));
        scene.setProperty("frameColor", QColor(Qt::gray));
        scene.setProperty("selectionColor", QColor(Qt::magenta));
        scene.setGrid(&grid); scene.setPoints(&points);
        scene.fitDomain();
        scene.setResolutionX(3); scene.setResolutionY(3);
    }
};
}

class ScientificGeometry3DTest : public QObject {
    Q_OBJECT
private slots:
    void surfaceUploadMatchesMesh();
    void pointsUploadAndSelectionPatch();
    void frameAndEmptyStates();
};

void ScientificGeometry3DTest::surfaceUploadMatchesMesh()
{
    Fixture f;
    ScientificGeometry3D geometry;
    geometry.setScene(&f.scene);
    QCOMPARE(geometry.part(), ScientificGeometry3D::Surface);
    QCOMPARE(geometry.stride(), Floats * int(sizeof(float)));
    QCOMPARE(geometry.attributeCount(), 3);
    QCOMPARE(geometry.attribute(0).semantic, QQuick3DGeometry::Attribute::PositionSemantic);
    QCOMPARE(geometry.attribute(1).offset, 3 * int(sizeof(float)));
    QCOMPARE(geometry.attribute(2).semantic, QQuick3DGeometry::Attribute::ColorSemantic);
    QCOMPARE(geometry.primitiveType(), QQuick3DGeometry::PrimitiveType::Triangles);
    QCOMPARE(geometry.primitiveCount(), 8);
    QCOMPARE(geometry.primitiveCount(), f.scene.triangleCount());
    const auto &mesh = f.scene.surfaceMesh();
    const QVector<float> v = floats(geometry.vertexData());
    QCOMPARE(v.size(), int(mesh.indices.size()) * Floats); // expanded, non-indexed triangles
    for (int k = 0; k < mesh.indices.size(); ++k) {
        const quint32 index = mesh.indices[k];
        QCOMPARE(position(v, k), mesh.positions[index]);
        QCOMPARE(normal(v, k), mesh.normals[index]);
        QCOMPARE(color(v, k), mesh.colors[index].rgba());
    }
    // The centre node of z = x y (1, 1, 1) is scene (0, -50, 0), and data Z is scene up.
    bool centre = false;
    for (int k = 0; k < mesh.indices.size(); ++k) centre = centre || position(v, k) == QVector3D(0, -50, 0);
    QVERIFY(centre);
    QCOMPARE(geometry.boundsMin(), QVector3D(-110, -110, -110));
    QCOMPARE(geometry.boundsMax(), QVector3D(110, 110, 110));
    // A new lattice is a new upload; a hole removes its triangles.
    QSignalSpy updated(&geometry, &ScientificGeometry3D::geometryUpdated);
    f.scene.setResolutionX(5); f.scene.setResolutionY(5);
    QVERIFY(updated.count() >= 1);
    QCOMPARE(geometry.primitiveCount(), 32);
    QCOMPARE(geometry.vertexData().size(), 32 * 3 * Floats * int(sizeof(float)));
}

void ScientificGeometry3DTest::pointsUploadAndSelectionPatch()
{
    Fixture f;
    ScientificGeometry3D geometry;
    geometry.setPart(ScientificGeometry3D::Points);
    geometry.setScene(&f.scene);
    constexpr int Per = ScientificGeometry3D::VerticesPerPoint;
    QCOMPARE(geometry.primitiveCount(), 2 * 8); // an octahedron per point
    QVector<float> v = floats(geometry.vertexData());
    QCOMPARE(v.size(), 2 * Per * Floats);
    for (int p = 0; p < 2; ++p) {
        QVector3D sum;
        for (int k = 0; k < Per; ++k) {
            sum += position(v, p * Per + k);
            QCOMPARE(color(v, p * Per + k), rgba(Qt::black));
            QVERIFY(QVector3D::dotProduct(normal(v, p * Per + k), position(v, p * Per + k) - f.scene.pointPositions()[p]) > 0);
        }
        QVERIFY((sum / Per - f.scene.pointPositions()[p]).length() < 1e-4f); // centred on the scene point
    }
    QCOMPARE(f.scene.pointPositions()[1], QVector3D(100, 100, -100)); // (2, 2, 4) is the far top corner

    // Selection repaints only the selected points' vertices; nothing is rebuilt.
    QSignalSpy mesh(&f.scene, &ScientificScene3D::meshChanged);
    const QByteArray before = geometry.vertexData();
    QVERIFY(f.scene.selectPoint("p1"));
    QCOMPARE(mesh.count(), 0);
    v = floats(geometry.vertexData());
    QCOMPARE(geometry.vertexData().size(), before.size());
    QCOMPARE(geometry.vertexData().left(Per * Floats * int(sizeof(float))), before.left(Per * Floats * int(sizeof(float))));
    for (int k = 0; k < Per; ++k) QCOMPARE(color(v, Per + k), rgba(Qt::magenta));
    QVERIFY(f.scene.selectPoint("p0"));
    v = floats(geometry.vertexData());
    QCOMPARE(color(v, 0), rgba(Qt::magenta));
    QCOMPARE(color(v, Per), rgba(Qt::black));
    QCOMPARE(mesh.count(), 0);
    // A full rebuild keeps the selection colour.
    f.scene.setResolutionX(4);
    v = floats(geometry.vertexData());
    QCOMPARE(color(v, 0), rgba(Qt::magenta));
    QCOMPARE(color(v, Per), rgba(Qt::black));
}

void ScientificGeometry3DTest::frameAndEmptyStates()
{
    auto f = std::make_unique<Fixture>();
    ScientificGeometry3D geometry;
    geometry.setPart(ScientificGeometry3D::Frame);
    geometry.setScene(&f->scene);
    QCOMPARE(geometry.primitiveType(), QQuick3DGeometry::PrimitiveType::Lines);
    QCOMPARE(geometry.primitiveCount(), 12); // the domain cube's edges
    const QVector<float> v = floats(geometry.vertexData());
    QCOMPARE(v.size(), 24 * Floats);
    const QVector<QVector3D> segments = f->scene.frameSegments();
    for (int k = 0; k < 24; ++k) {
        QCOMPARE(position(v, k), segments[k]);
        QCOMPARE(qAbs(position(v, k).x()), 100.0f);
        QCOMPARE(color(v, k), rgba(Qt::gray));
    }
    // A scene with a problem uploads nothing; so does a destroyed scene.
    ScientificGeometry3D surface;
    surface.setScene(&f->scene);
    QVERIFY(surface.primitiveCount() > 0);
    f->points.setZUnit("mK");
    QCOMPARE(surface.primitiveCount(), 0);
    QVERIFY(surface.vertexData().isEmpty());
    QCOMPARE(geometry.primitiveCount(), 0); // no frame either: the domain is not drawable
    f.reset();
    QCOMPARE(geometry.scene(), nullptr);
    QCOMPARE(geometry.primitiveCount(), 0);
    QVERIFY(geometry.vertexData().isEmpty());
}

QTEST_MAIN(ScientificGeometry3DTest)
#include "tst_scientific_geometry3d.moc"
