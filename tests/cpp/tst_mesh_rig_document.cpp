// SPDX-License-Identifier: LGPL-3.0-or-later
#include "mesh_rig_document.h"
#include <QtTest>
#include <QFile>
#include <QTemporaryDir>
using namespace QindaTK;

class MeshRigDocumentTest : public QObject {
    Q_OBJECT
private slots:
    void poseUvPaintUndoReopen() {
        MeshRigDocument doc;
        const QString a = doc.addVertex({0, 0, 0});
        const QString b = doc.addVertex({1, 0, 0});
        const QString c = doc.addVertex({1, 1, 0});
        const QString d = doc.addVertex({0, 1, 0});
        const QString first = doc.addFace(a, b, c);
        const QString second = doc.addFace(a, c, d);
        QVERIFY(!first.isEmpty()); QVERIFY(!second.isEmpty());
        const auto stableVertex = doc.vertices()[1].toMap().value("vertexId").toString();
        const auto stableUv = doc.faces()[1].toMap().value("uvIds").toList()[0].toString();
        QVERIFY(doc.setUV(second, 0, {.1, .1}));
        QVERIFY(doc.setUV(second, 1, {.5, .5}));
        QVERIFY(doc.setUV(second, 2, {.1, .5}));
        QVERIFY(doc.setSeam(a, c, true));
        QCOMPARE(doc.uvIslands().size(), 2);
        QVERIFY(doc.translateUvIsland(second, {.1, .1}));
        QCOMPARE(doc.faces()[1].toMap().value("uvIds").toList()[0].toString(), stableUv);
        const QString bone = doc.addBone("Arm", {}, {0, 0, 0}, {1, 0, 0});
        QVERIFY(!bone.isEmpty());
        QVERIFY(doc.setVertexWeights(b, {{bone, 1.0}}));
        QVERIFY(doc.setBoneLimits(bone, {-90, -90, -90}, {90, 90, 90}));
        QVERIFY(!doc.setBonePose(bone, {0, 0, 91}));
        QVERIFY(doc.setBonePose(bone, {0, 0, 90}));
        const auto posed = doc.vertices()[1].toMap().value("posedPosition").value<QVector3D>();
        QVERIFY((posed - QVector3D(0, 1, 0)).length() < .001f);
        QCOMPARE(doc.vertices()[1].toMap().value("vertexId").toString(), stableVertex);

        const auto white = doc.texture();
        QVERIFY(doc.beginPaint());
        QVERIFY(doc.paintRay({.2f, .8f, 1}, {0, 0, -1}, QColor(Qt::red), 3, 1));
        QVERIFY(doc.endPaint());
        const auto painted = doc.texture();
        int red = 0;
        for (int y = 0; y < painted.height(); ++y)
            for (int x = 0; x < painted.width(); ++x)
                if (painted.pixelColor(x, y) == QColor(Qt::red)) ++red;
        QVERIFY(red > 0);
        QVERIFY(doc.undo()); QCOMPARE(doc.texture(), white);
        QVERIFY(doc.redo()); QCOMPARE(doc.texture(), painted);

        QTemporaryDir dir; QVERIFY(dir.isValid());
        QVERIFY(doc.saveToFile(dir.filePath("mesh.json")));
        QVERIFY(doc.exportObj(dir.filePath("mesh.obj")));
        QVERIFY(doc.exportTexturePng(dir.filePath("texture.png")));
        const QImage exported(dir.filePath("texture.png"));
        QVERIFY(!exported.isNull());
        QCOMPARE(exported.convertToFormat(QImage::Format_ARGB32_Premultiplied), painted);
        QFile obj(dir.filePath("mesh.obj")); QVERIFY(obj.open(QIODevice::ReadOnly));
        const auto objText = obj.readAll();
        QVERIFY(objText.contains("vt ")); QVERIFY(objText.contains("f 1/4 3/5 4/6"));
        MeshRigDocument reopened;
        QVERIFY(reopened.loadFromFile(dir.filePath("mesh.json")));
        QCOMPARE(reopened.texture(), painted);
        QCOMPARE(reopened.vertices()[1].toMap().value("vertexId").toString(), stableVertex);
        QCOMPARE(reopened.faces()[1].toMap().value("uvIds").toList()[0].toString(), stableUv);
        QCOMPARE(reopened.uvIslands().size(), 2);
        const auto reopenedPose = reopened.vertices()[1].toMap().value("posedPosition").value<QVector3D>();
        QVERIFY((reopenedPose - posed).length() < .001f);
        const auto saved = reopened.toJson();
        QVERIFY(!reopened.fromJson("{}")); QCOMPARE(reopened.toJson(), saved);
    }
};
QTEST_GUILESS_MAIN(MeshRigDocumentTest)
#include "tst_mesh_rig_document.moc"
