// SPDX-License-Identifier: LGPL-3.0-or-later
// The Qt Quick 3D upload path of MeshRigDocument (WP-077, D-154), headless:
// MeshRigGeometry's vertex buffer and three raw RGBA8 texture channels must
// be exactly the document's CPU state, so the GPU draws the data the software
// reference tests check. On-screen drawing is qualified by
// tst_scene_view_3d_render under a display (deferred to WP-088..094).
#include "mesh_rig_document.h"
#include "mesh_rig_geometry.h"

#include <QSignalSpy>
#include <QtTest>
#include <cstring>

using namespace QindaTK;

namespace {
QVector<float> floats(const QByteArray &bytes)
{
    QVector<float> result(bytes.size() / int(sizeof(float)));
    std::memcpy(result.data(), bytes.constData(), size_t(result.size()) * sizeof(float));
    return result;
}
QByteArray texel(const QByteArray &rgba, int width, int x, int y)
{
    return rgba.mid((y * width + x) * 4, 4);
}
QByteArray bytes(int r, int g, int b, int a)
{
    QByteArray result(4, 0);
    result[0] = char(r); result[1] = char(g); result[2] = char(b); result[3] = char(a);
    return result;
}
}

class MeshRigGeometryTest : public QObject {
    Q_OBJECT
private slots:
    void uploadMatchesDocument();
};

void MeshRigGeometryTest::uploadMatchesDocument()
{
    MeshRigDocument doc;
    const QString v0 = doc.addVertex({0, 0, 0}), v1 = doc.addVertex({8, 0, 0});
    const QString v2 = doc.addVertex({8, 8, 0}), v3 = doc.addVertex({0, 8, 0});
    const QString a = doc.addFace(v0, v1, v2), b = doc.addFace(v0, v2, v3);
    QVERIFY(doc.setUV(a, 1, {1, 0}) && doc.setUV(a, 2, {1, 1}));
    QVERIFY(doc.setUV(b, 1, {1, 1}) && doc.setUV(b, 2, {0, 1}));
    QVERIFY(doc.resizeTexture(8, 8));

    MeshRigGeometry geometry;
    QSignalSpy updates(&geometry, &MeshRigGeometry::geometryUpdated);
    geometry.setDocument(&doc);
    QCOMPARE(geometry.triangleCount(), 2);
    QCOMPARE(geometry.stride(), MeshRigGeometry::FloatsPerVertex * int(sizeof(float)));
    QCOMPARE(geometry.vertexData(), MeshRigGeometry::packVertices(&doc));
    const QVector<float> vertex = floats(geometry.vertexData());
    QCOMPARE(vertex.size(), 6 * MeshRigGeometry::FloatsPerVertex);
    // Face A corner 2: position (8,8,0), normal +Z, uv (1,1) uploaded as (1, 1 - 1).
    const float *corner = vertex.constData() + 2 * MeshRigGeometry::FloatsPerVertex;
    QCOMPARE(QVector3D(corner[0], corner[1], corner[2]), QVector3D(8, 8, 0));
    QCOMPARE(QVector3D(corner[3], corner[4], corner[5]), QVector3D(0, 0, 1));
    QCOMPARE(corner[6], 1.0f);
    QCOMPARE(corner[7], 0.0f);

    // Paint one texel red on the base channel and one gray on roughness.
    QVERIFY(doc.beginPaint());
    QVERIFY(doc.paintRay({6.5f, 1.5f, 5}, {0, 0, -1}, QColor(255, 0, 0), 1));
    QVERIFY(doc.endPaint());
    QVERIFY(doc.beginPaint(QStringLiteral("roughness")));
    QVERIFY(doc.paintRay({6.5f, 1.5f, 5}, {0, 0, -1}, QColor(64, 64, 64), 1));
    QVERIFY(doc.endPaint());
    QVERIFY(updates.count() > 0);
    QCOMPARE(geometry.textureData()->size(), QSize(8, 8));
    QCOMPARE(geometry.textureData()->format(), QQuick3DTextureData::RGBA8);
    const QByteArray base = geometry.textureData()->textureData();
    QCOMPARE(base, MeshRigGeometry::packTexture(doc.texture()));
    QCOMPARE(base.size(), 8 * 8 * 4);
    // Surface (6.5, 1.5) is texel (6, 6): rows are uploaded top row first.
    QCOMPARE(texel(base, 8, 6, 6), bytes(255, 0, 0, 255));
    QCOMPARE(texel(base, 8, 0, 0), bytes(255, 255, 255, 255));
    const QByteArray roughness = geometry.roughnessData()->textureData();
    QCOMPARE(roughness, MeshRigGeometry::packTexture(doc.texture(QStringLiteral("roughness"))));
    QCOMPARE(texel(roughness, 8, 6, 6), bytes(64, 64, 64, 255));
    QCOMPARE(texel(roughness, 8, 0, 0), bytes(204, 204, 204, 255));
    QCOMPARE(texel(geometry.metalnessData()->textureData(), 8, 3, 3), bytes(0, 0, 0, 255));

    // A pose change re-uploads posed positions; undo restores the same bytes.
    const QByteArray rest = geometry.vertexData();
    const QString bone = doc.addBone("Lift", {}, {0, 0, 0}, {0, 8, 0});
    QVERIFY(doc.setVertexWeights(v2, {{bone, 1.0}}));
    QVERIFY(doc.setBonePose(bone, {90, 0, 0}));
    QVERIFY(geometry.vertexData() != rest);
    QCOMPARE(geometry.vertexData(), MeshRigGeometry::packVertices(&doc));
    QVERIFY(doc.undo());
    QVERIFY(doc.undo());
    QVERIFY(doc.undo());
    QCOMPARE(geometry.vertexData(), rest);
    QVERIFY(doc.undo()); // roughness stroke
    QCOMPARE(texel(geometry.roughnessData()->textureData(), 8, 6, 6), bytes(204, 204, 204, 255));

    geometry.setDocument(nullptr);
    QCOMPARE(geometry.triangleCount(), 0);
    QVERIFY(geometry.textureData()->textureData().isEmpty());
}

QTEST_MAIN(MeshRigGeometryTest)
#include "tst_mesh_rig_geometry.moc"
