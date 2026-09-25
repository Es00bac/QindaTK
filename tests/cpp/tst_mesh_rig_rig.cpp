// SPDX-License-Identifier: LGPL-3.0-or-later
// Rig constraints and animation keys, UV topology operations, the shared
// undo history and version 2 persistence of MeshRigDocument (WP-077).
#include "mesh_rig_document.h"
#include "scene_model_3d.h"
#include "undo_service.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRectF>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>

using namespace QindaTK;

namespace {
bool near(const QVector3D &a, const QVector3D &b, float tolerance = 1e-5f) { return (a - b).length() < tolerance; }
bool near(const QPointF &a, const QPointF &b, double tolerance = 1e-9) {
    return std::abs(a.x() - b.x()) < tolerance && std::abs(a.y() - b.y()) < tolerance;
}
QVector3D posed(const MeshRigDocument &doc, int vertex) {
    return doc.vertices().at(vertex).toMap().value("posedPosition").value<QVector3D>();
}
QVector3D onCircle(double degrees) {
    return {float(std::cos(qDegreesToRadians(degrees))), float(std::sin(qDegreesToRadians(degrees))), 0};
}
QVariantMap bone(const MeshRigDocument &doc, int index) { return doc.bones().at(index).toMap(); }
QPointF faceUv(const MeshRigDocument &doc, int face, int corner) {
    return doc.faces().at(face).toMap().value("uv").toList().at(corner).toPointF();
}
struct Quad { QString v0, v1, v2, v3, a, b; };
Quad quad(MeshRigDocument &doc, QVector3D p0, QVector3D p1, QVector3D p2, QVector3D p3) {
    Quad q;
    q.v0 = doc.addVertex(p0); q.v1 = doc.addVertex(p1); q.v2 = doc.addVertex(p2); q.v3 = doc.addVertex(p3);
    q.a = doc.addFace(q.v0, q.v1, q.v2); q.b = doc.addFace(q.v0, q.v2, q.v3);
    return q;
}
}

class MeshRigRigTest : public QObject {
    Q_OBJECT
private slots:
    void hierarchyKnownPose();
    void constraintsKeysAndCycles();
    void uvStitchTransformAndPack();
    void unwrapProjectsIslands();
    void sharedHistoryWithScene();
    void versionTwoPersistence();
};

void MeshRigRigTest::hierarchyKnownPose()
{
    MeshRigDocument doc;
    const QString tip = doc.addVertex({2, 0, 0});
    const QString parent = doc.addBone("Parent", {}, {0, 0, 0}, {1, 0, 0});
    const QString child = doc.addBone("Child", parent, {1, 0, 0}, {2, 0, 0});
    QVERIFY(doc.setVertexWeights(tip, {{child, 1.0}}));
    QVERIFY(doc.setBonePose(parent, {0, 0, 90}));
    QVERIFY(doc.setBonePose(child, {0, 0, 90}));
    // Child rotates the tip about (1,0,0) to (1,1,0); the parent then turns that to (-1,1,0).
    QVERIFY(near(posed(doc, 0), {-1, 1, 0}));
    QTemporaryDir dir; QVERIFY(dir.isValid());
    QVERIFY(doc.exportObj(dir.filePath("pose.obj")));
    QFile obj(dir.filePath("pose.obj")); QVERIFY(obj.open(QIODevice::ReadOnly | QIODevice::Text));
    QVector3D exported; bool found = false;
    for (const QByteArray &line : obj.readAll().split('\n')) {
        if (!line.startsWith("v ")) continue;
        const auto parts = line.split(' ');
        exported = {parts[1].toFloat(), parts[2].toFloat(), parts[3].toFloat()};
        found = true; break;
    }
    QVERIFY(found);
    QVERIFY(near(exported, {-1, 1, 0}, 1e-4f));
    QVERIFY(doc.resetPose());
    QVERIFY(near(posed(doc, 0), {2, 0, 0}));
}

void MeshRigRigTest::constraintsKeysAndCycles()
{
    MeshRigDocument doc;
    const QString tip = doc.addVertex({1, 0, 0});
    const QString a = doc.addBone("A", {}, {0, 0, 0}, {1, 0, 0});
    const QString b = doc.addBone("B", {}, {0, 0, 0}, {1, 0, 0});
    const QString c = doc.addBone("C", {}, {0, 0, 0}, {1, 0, 0});
    QVERIFY(doc.setVertexWeights(tip, {{b, 1.0}}));
    QVERIFY(doc.setBonePose(a, {0, 0, 60}));
    QVERIFY(doc.setBonePose(b, {0, 0, 10}));
    const QString bc = doc.addConstraint(b, "copyRotation", a, 0.5);
    QVERIFY(!bc.isEmpty());
    // B = own 10 + 0.5 x A's 60.
    QVERIFY(near(doc.evaluatedPose(b), {0, 0, 40}));
    QVERIFY(near(posed(doc, 0), onCircle(40)));
    QCOMPARE(bone(doc, 1).value("pose").value<QVector3D>(), QVector3D(0, 0, 10)); // not baked

    QVERIFY(doc.addConstraint(a, "copyRotation", b).isEmpty());
    QVERIFY(doc.lastError().contains("cycle"));
    QVERIFY(doc.addConstraint(a, "copyRotation", a).isEmpty());
    QVERIFY(doc.addConstraint(b, "inverseKinematics", c).isEmpty());
    QVERIFY(doc.addConstraint(b, "copyRotation", c, 1.5).isEmpty());
    QVERIFY(!doc.addConstraint(c, "copyRotation", b, 1.0).isEmpty());
    QVERIFY(doc.addConstraint(a, "copyRotation", c).isEmpty()); // A <- C <- B <- A
    QVERIFY(doc.lastError().contains("cycle"));
    QVERIFY(near(doc.evaluatedPose(c), {0, 0, 40}));

    // Limits clamp the evaluated rotation only.
    QVERIFY(doc.setBoneLimits(b, {-180, -180, -30}, {180, 180, 30}));
    QVERIFY(near(doc.evaluatedPose(b), {0, 0, 30}));
    QVERIFY(near(doc.evaluatedPose(c), {0, 0, 30}));
    QVERIFY(near(posed(doc, 0), onCircle(30)));

    // Keys drive A; linear interpolation at frame 5 of 0..10.
    QVERIFY(doc.setPoseKey(a, 0, {0, 0, 0}));
    QVERIFY(doc.setPoseKey(a, 10, {0, 0, 40}));
    doc.setCurrentFrame(5);
    QVERIFY(near(doc.evaluatedPose(a), {0, 0, 20}));
    QVERIFY(near(doc.evaluatedPose(b), {0, 0, 20}));
    QVERIFY(near(posed(doc, 0), onCircle(20)));
    QVERIFY(bone(doc, 0).value("animated").toBool());
    QVERIFY(near(bone(doc, 0).value("sourcePose").value<QVector3D>(), {0, 0, 20}));
    // Posing an animated bone keys the current frame; the static pose is kept.
    QVERIFY(doc.setBonePose(a, {0, 0, 30}));
    QCOMPARE(bone(doc, 0).value("keys").toList().size(), 3);
    QCOMPARE(bone(doc, 0).value("pose").value<QVector3D>(), QVector3D(0, 0, 60));
    QVERIFY(near(doc.evaluatedPose(a), {0, 0, 30}));
    // A rig edit that would exclude animation data is refused, not clamped.
    QVERIFY(!doc.setBoneLimits(a, {-180, -180, -10}, {180, 180, 10}));
    QCOMPARE(bone(doc, 0).value("keys").toList().size(), 3);
    QVERIFY(!doc.setPoseKey(b, 3, {0, 0, 45}));

    // Reset clears static poses and keeps keys.
    QVERIFY(doc.resetPose());
    QVERIFY(near(doc.evaluatedPose(a), {0, 0, 30}));
    QVERIFY(near(doc.evaluatedPose(b), {0, 0, 15}));
    QVERIFY(doc.removeConstraint(bc));
    QVERIFY(near(doc.evaluatedPose(b), {0, 0, 0}));
    QVERIFY(doc.undo());
    QVERIFY(near(doc.evaluatedPose(b), {0, 0, 15}));
    QVERIFY(doc.removePoseKey(a, 5));
    QVERIFY(near(doc.evaluatedPose(a), {0, 0, 20}));
    QVERIFY(!doc.removePoseKey(a, 5));
    doc.setCurrentFrame(40); // holds the last key
    QVERIFY(near(doc.evaluatedPose(a), {0, 0, 40}));
}

void MeshRigRigTest::uvStitchTransformAndPack()
{
    MeshRigDocument doc;
    const Quad q = quad(doc, {0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0});
    const QPointF a[3] = {{0.1, 0.1}, {0.4, 0.1}, {0.4, 0.4}};
    const QPointF b[3] = {{0.6, 0.6}, {0.75, 0.75}, {0.6, 0.75}}; // half size, elsewhere
    for (int k = 0; k < 3; ++k) { QVERIFY(doc.setUV(q.a, k, a[k])); QVERIFY(doc.setUV(q.b, k, b[k])); }
    QVERIFY(doc.setSeam(q.v0, q.v2, true));
    QCOMPARE(doc.uvIslands().size(), 2);
    const QString uvId = doc.faces()[1].toMap().value("uvIds").toList()[2].toString();

    // Packing preview, then commit: tallest island first, one uniform scale.
    const QVariantList layout = doc.packUvLayout(0.02);
    QCOMPARE(layout.size(), 2);
    const double scale = 0.94 / 0.45;
    QVERIFY(std::abs(layout[0].toMap().value("scale").toDouble() - scale) < 1e-9);
    const QRectF first = layout[0].toMap().value("bounds").toRectF();
    const QRectF second = layout[1].toMap().value("bounds").toRectF();
    QVERIFY(near(first.topLeft(), {0.02, 0.02}));
    QVERIFY(std::abs(first.width() - 0.3 * scale) < 1e-9);
    QVERIFY(near(second.topLeft(), {0.02, 0.04 + 0.3 * scale}));
    QVERIFY(!first.intersects(second));
    QVERIFY(second.bottom() <= 1.0 + 1e-12);
    QVERIFY(doc.packUvLayout(0.3).isEmpty());
    QVERIFY(!doc.packUvIslands(0.3));
    const QString before = doc.toJson();
    QVERIFY(doc.packUvIslands(0.02));
    const QVariantList islands = doc.uvIslands();
    QVERIFY(near(islands[0].toMap().value("bounds").toRectF().topLeft(), first.topLeft()));
    QVERIFY(near(islands[1].toMap().value("bounds").toRectF().topLeft(), second.topLeft()));
    QVERIFY(doc.undo());
    QCOMPARE(doc.toJson(), before);

    // Stitch: the second island lands on the first island's seam edge.
    QVERIFY(!doc.stitchSeam(q.v0, q.v1));
    QVERIFY(doc.stitchSeam(q.v0, q.v2));
    QCOMPARE(doc.uvIslands().size(), 1);
    QVERIFY(near(faceUv(doc, 1, 0), {0.1, 0.1}));
    QVERIFY(near(faceUv(doc, 1, 1), {0.4, 0.4}));
    QVERIFY(near(faceUv(doc, 1, 2), {0.1, 0.4}));
    QCOMPARE(doc.faces()[1].toMap().value("uvIds").toList()[2].toString(), uvId);
    QVERIFY(!doc.faces()[0].toMap().value("seams").toList()[2].toBool());

    // Rotate the merged island 90 degrees about its bounds centre (0.25, 0.25).
    QVERIFY(doc.transformUvIsland(q.a, {0, 0}, 90, 1));
    QVERIFY(near(faceUv(doc, 0, 1), {0.4, 0.4}));
    QVERIFY(near(faceUv(doc, 1, 2), {0.1, 0.1}));
    QVERIFY(!doc.transformUvIsland(q.a, {0, 0}, 0, 10)); // would leave [0,1]
    QVERIFY(doc.translateUvIsland(q.a, {0.5, 0.25}));
    QVERIFY(near(faceUv(doc, 0, 1), {0.9, 0.65}));
    QCOMPARE(doc.faces().size(), 2); // topology untouched throughout
}

void MeshRigRigTest::unwrapProjectsIslands()
{
    MeshRigDocument doc;
    // A 2*sqrt(2) x 1 rectangle tilted 45 degrees about Y.
    const Quad q = quad(doc, {0, 0, 0}, {2, 0, 2}, {2, 1, 2}, {0, 1, 0});
    QVERIFY(doc.unwrapUvIsland(q.a));
    const double short_ = 1.0 / (2.0 * std::sqrt(2.0));
    QVERIFY(near(faceUv(doc, 0, 0), {0, 0}, 1e-6));
    QVERIFY(near(faceUv(doc, 0, 1), {1, 0}, 1e-6));
    QVERIFY(near(faceUv(doc, 0, 2), {1, short_}, 1e-6));
    QVERIFY(near(faceUv(doc, 1, 2), {0, short_}, 1e-6));

    // A closed tetrahedron has no projection plane: refused, UVs untouched.
    MeshRigDocument closed;
    const QString p0 = closed.addVertex({0, 0, 0}), p1 = closed.addVertex({1, 0, 0});
    const QString p2 = closed.addVertex({0, 1, 0}), p3 = closed.addVertex({0, 0, 1});
    const QString f = closed.addFace(p0, p2, p1);
    QVERIFY(!closed.addFace(p0, p1, p3).isEmpty());
    QVERIFY(!closed.addFace(p0, p3, p2).isEmpty());
    QVERIFY(!closed.addFace(p1, p2, p3).isEmpty());
    const QString json = closed.toJson();
    QVERIFY(!closed.unwrapUvIsland(f));
    QCOMPARE(closed.toJson(), json);
}

void MeshRigRigTest::sharedHistoryWithScene()
{
    UndoService shared;
    SceneModel3D scene;
    MeshRigDocument mesh;
    QVERIFY(mesh.undoService() != &shared);
    QVERIFY(shared.adoptModel(&scene));
    QVERIFY(shared.adoptModel(&mesh));
    QCOMPARE(mesh.undoService(), &shared);
    const QString vertex = mesh.addVertex({0, 0, 0});
    QVERIFY(!vertex.isEmpty());
    QVERIFY(!scene.addEntity("Cube", "cube", {0, 0, 0}, QColor(Qt::cyan)).isEmpty());
    QVERIFY(shared.canUndo(&mesh));
    QVERIFY(shared.canUndo(&scene));
    QCOMPARE(shared.undoText(&mesh), QStringLiteral("Add vertex"));
    QVERIFY(mesh.undo());
    QVERIFY(mesh.vertices().isEmpty());
    QCOMPARE(scene.entities().size(), 1);
    shared.setActiveModel(&mesh);
    QVERIFY(shared.redo());
    QCOMPARE(mesh.vertices().size(), 1);
    QCOMPARE(mesh.vertices()[0].toMap().value("vertexId").toString(), vertex);
}

void MeshRigRigTest::versionTwoPersistence()
{
    MeshRigDocument doc;
    const Quad q = quad(doc, {0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0});
    const QString a = doc.addBone("A", {}, {0, 0, 0}, {1, 0, 0});
    const QString b = doc.addBone("B", a, {1, 0, 0}, {1, 1, 0});
    QVERIFY(doc.setVertexWeights(q.v2, {{b, 1.0}}));
    QVERIFY(!doc.addConstraint(b, "copyRotation", a, 0.25).isEmpty());
    QVERIFY(doc.setPoseKey(a, 0, {0, 0, 0}));
    QVERIFY(doc.setPoseKey(a, 8, {0, 0, 16}));
    doc.setCurrentFrame(4);
    QTemporaryDir dir; QVERIFY(dir.isValid());
    QVERIFY(doc.saveToFile(dir.filePath("rig.json")));
    MeshRigDocument reopened;
    QVERIFY(reopened.loadFromFile(dir.filePath("rig.json")));
    reopened.setCurrentFrame(4);
    QCOMPARE(reopened.bones(), doc.bones());
    QCOMPARE(reopened.vertices(), doc.vertices());
    QCOMPARE(reopened.faces(), doc.faces());
    QVERIFY(reopened.canUndo()); // the load is one step in the new document's history

    QJsonObject root = QJsonDocument::fromJson(doc.toJson().toUtf8()).object();
    QCOMPARE(root.value("version").toInt(), 2);
    // A hand-edited file with a constraint loop is refused; the document is kept.
    QJsonObject loop = root;
    QJsonArray bones = loop.value("bones").toArray();
    QJsonObject first = bones[0].toObject();
    first.insert("constraints", QJsonArray{QJsonObject{{"id", "loop-constraint"}, {"type", "copyRotation"},
                                                       {"targetId", b}, {"influence", 1.0}}});
    bones[0] = first; loop.insert("bones", bones);
    const QString kept = reopened.toJson();
    QVERIFY(!reopened.fromJson(QString::fromUtf8(QJsonDocument(loop).toJson())));
    QVERIFY(reopened.lastError().contains("cycle"));
    QCOMPARE(reopened.toJson(), kept);

    // Version 1 files (one texture, no constraints or keys) still open.
    QJsonObject v1 = root;
    v1.insert("version", 1);
    v1.insert("texturePng", root.value("textures").toObject().value("baseColor"));
    v1.remove("textures");
    MeshRigDocument legacy;
    QVERIFY(legacy.fromJson(QString::fromUtf8(QJsonDocument(v1).toJson())));
    QCOMPARE(legacy.texture(), doc.texture());
    QCOMPARE(legacy.texture(QStringLiteral("roughness")).pixel(0, 0), qRgb(204, 204, 204));
    QVERIFY(legacy.bones()[1].toMap().value("constraints").toList().isEmpty());
    QVERIFY(legacy.bones()[0].toMap().value("keys").toList().isEmpty());

    // A version 1 file without its texture is refused: loading it used to
    // give null channels whose saved file could never be reopened.
    QJsonObject noTexture = v1;
    noTexture.remove("texturePng");
    MeshRigDocument refused;
    const QString untouched = refused.toJson();
    QVERIFY(!refused.fromJson(QString::fromUtf8(QJsonDocument(noTexture).toJson())));
    QVERIFY(refused.lastError().contains("texture"));
    QCOMPARE(refused.toJson(), untouched);
    // A version 2 file missing one channel is refused the same way.
    QJsonObject missing = root;
    QJsonObject channels = root.value("textures").toObject();
    channels.remove("metalness");
    missing.insert("textures", channels);
    QVERIFY(!refused.fromJson(QString::fromUtf8(QJsonDocument(missing).toJson())));
    // Everything the document saves reopens.
    MeshRigDocument roundTrip;
    QVERIFY(roundTrip.fromJson(legacy.toJson()));
    QCOMPARE(roundTrip.texture(), legacy.texture());
}

QTEST_GUILESS_MAIN(MeshRigRigTest)
#include "tst_mesh_rig_rig.moc"
