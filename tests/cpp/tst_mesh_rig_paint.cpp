// SPDX-License-Identifier: LGPL-3.0-or-later
// Exact CPU reference for mesh texture paint (WP-077, D-154): a 16x16 quad
// split on its diagonal into two UV islands, 1 world unit per texel, so the
// expected texels of a dab follow from plain geometry and are compared
// pixel for pixel. The GPU upload of the same bytes is checked in
// tst_mesh_rig_geometry.
#include "mesh_rig_document.h"
#include "raster_brush_p.h"
#include "undo_service.h"

#include <QSet>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>

using namespace QindaTK;

namespace {
struct Quad {
    QString v0, v1, v2, v3, below, above; // below: x >= y (face A), above: x <= y (face B)
};
// Face A = (v0,v1,v2) keeps uv = position / 32 (left half); face B = (v0,v2,v3)
// sits at uv = position / 32 + (0.5, 0) (right half); the diagonal is a seam.
Quad seamQuad(MeshRigDocument &doc)
{
    Quad q;
    q.v0 = doc.addVertex({0, 0, 0}); q.v1 = doc.addVertex({16, 0, 0});
    q.v2 = doc.addVertex({16, 16, 0}); q.v3 = doc.addVertex({0, 16, 0});
    q.below = doc.addFace(q.v0, q.v1, q.v2);
    q.above = doc.addFace(q.v0, q.v2, q.v3);
    doc.setUV(q.below, 0, {0, 0}); doc.setUV(q.below, 1, {0.5, 0}); doc.setUV(q.below, 2, {0.5, 0.5});
    doc.setUV(q.above, 0, {0.5, 0}); doc.setUV(q.above, 1, {1, 0.5}); doc.setUV(q.above, 2, {0.5, 0.5});
    doc.setSeam(q.v0, q.v2, true);
    doc.resizeTexture(32, 32);
    return q;
}
// Texels whose centre maps (through either island) within `radius` of `center`.
QSet<QPoint> referenceDab(QPointF center, double radius, double *margin)
{
    QSet<QPoint> texels;
    *margin = 1e9;
    for (int y = 0; y < 32; ++y) for (int x = 0; x < 32; ++x) {
        const double px = x + 0.5, py = y + 0.5, wy = 32 - py;
        for (int island = 0; island < 2; ++island) {
            const double wx = island == 0 ? px : px - 16;
            const bool inside = island == 0 ? (0 <= wy && wy <= wx && wx <= 16)
                                            : (0 <= wx && wx <= wy && wy <= 16);
            if (!inside) continue;
            const double d = std::hypot(wx - center.x(), wy - center.y());
            *margin = std::min(*margin, std::abs(d - radius));
            if (d < radius) texels.insert(QPoint(x, y));
        }
    }
    return texels;
}
QSet<QPoint> paintedTexels(const QImage &image, QRgb background)
{
    QSet<QPoint> result;
    for (int y = 0; y < image.height(); ++y) for (int x = 0; x < image.width(); ++x)
        if (image.pixel(x, y) != background) result.insert(QPoint(x, y));
    return result;
}
}

class MeshRigPaintTest : public QObject {
    Q_OBJECT
private slots:
    void dabCrossesSeamExactly();
    void softDabUsesRasterKernel();
    void strokeInterpolatesCameraRays();
    void perspectiveCameraRays();
    void materialChannelsPaintSaveAndReopen();
    void invalidPaintLeavesStateIntact();
};

void MeshRigPaintTest::dabCrossesSeamExactly()
{
    MeshRigDocument doc;
    const Quad q = seamQuad(doc);
    QCOMPARE(doc.uvIslands().size(), 2);
    const QImage white = doc.texture();
    double margin = 0;
    const QSet<QPoint> expected = referenceDab({9.2, 7.1}, 3.0, &margin);
    QVERIFY2(margin > 1e-3, "reference dab must not graze a texel centre");
    QCOMPARE(expected.size(), 33); // 25 texels in island A, 8 across the seam in B

    QVERIFY(doc.beginPaint());
    QVERIFY(doc.paintRay({9.2f, 7.1f, 10}, {0, 0, -1}, QColor(255, 0, 0), 3, 1.0, 1.0));
    QVERIFY(doc.endPaint());
    const QImage painted = doc.texture();
    QCOMPARE(paintedTexels(painted, white.pixel(0, 0)), expected);
    int acrossSeam = 0;
    for (const QPoint &p : expected) {
        QCOMPARE(painted.pixel(p), qRgba(255, 0, 0, 255));
        if (p.x() >= 16) ++acrossSeam;
    }
    QCOMPARE(acrossSeam, 8);
    // One stroke is one undo step; stable IDs survive.
    QVERIFY(doc.undo()); QCOMPARE(doc.texture(), white);
    QVERIFY(doc.redo()); QCOMPARE(doc.texture(), painted);
    QCOMPARE(doc.faces()[1].toMap().value("faceId").toString(), q.above);
}

void MeshRigPaintTest::softDabUsesRasterKernel()
{
    MeshRigDocument doc;
    seamQuad(doc);
    const QRgb white = doc.texture().pixel(0, 0);
    QVERIFY(doc.beginPaint());
    QVERIFY(doc.paintRay({9.2f, 7.1f, 10}, {0, 0, -1}, QColor(0, 0, 255), 3, 0.8, 0.25));
    QVERIFY(doc.endPaint());
    const QImage painted = doc.texture();
    double margin = 0;
    const QSet<QPoint> expected = referenceDab({9.2, 7.1}, 3.0, &margin);
    QCOMPARE(paintedTexels(painted, white), expected);
    for (const QPoint &p : expected) {
        // Map the texel back to the surface and apply the shared kernel.
        const double wx = p.x() >= 16 ? p.x() + 0.5 - 16 : p.x() + 0.5, wy = 32 - (p.y() + 0.5);
        const double d = std::hypot(wx - 9.2, wy - 7.1) / 3.0;
        const QRgb want = RasterBrush::blendPremultiplied(white, QColor(0, 0, 255),
                                                          0.8 * RasterBrush::edgeCoverage(d, 0.25));
        const QRgb got = painted.pixel(p);
        // Float surface positions may move a rounding boundary by one level.
        QVERIFY2(qAbs(qRed(got) - qRed(want)) <= 1 && qAbs(qGreen(got) - qGreen(want)) <= 1
                     && qBlue(got) == qBlue(want) && qAlpha(got) == 255,
                 qPrintable(QStringLiteral("texel %1,%2").arg(p.x()).arg(p.y())));
    }
    // The centre texel is fully covered; the rim is lighter than the centre.
    QCOMPARE(painted.pixel(9, 24), RasterBrush::blendPremultiplied(white, QColor(0, 0, 255), 0.8));
}

void MeshRigPaintTest::strokeInterpolatesCameraRays()
{
    MeshRigDocument doc;
    seamQuad(doc);
    const QImage white = doc.texture();
    QVERIFY(doc.beginPaint());
    QVERIFY(doc.painting());
    QVERIFY(!doc.canUndo());
    QVERIFY(doc.paintRay({2.5f, 1.5f, 10}, {0, 0, -1}, QColor(0, 128, 0), 1));
    QVERIFY(doc.paintRay({14.5f, 1.5f, 10}, {0, 0, -1}, QColor(0, 128, 0), 1));
    QVERIFY(doc.endPaint());
    // Surface x = 8.5, y = 1.5 is texel (8, 30): reached only by interpolated dabs.
    QCOMPARE(doc.texture().pixel(8, 30), qRgba(0, 128, 0, 255));
    for (int x = 2; x <= 14; ++x) QCOMPARE(doc.texture().pixel(x, 30), qRgba(0, 128, 0, 255));
    QVERIFY(doc.undo());
    QCOMPARE(doc.texture(), white);
    QVERIFY(!doc.canUndo() || doc.undoService()->undoText(&doc) != QStringLiteral("Paint texture"));
}

void MeshRigPaintTest::perspectiveCameraRays()
{
    QVector3D origin, direction;
    QVERIFY(MeshRigDocument::perspectiveRay({10, 6, 40}, {}, 60, {100, 100}, {50, 50}, &origin, &direction));
    QCOMPARE(origin, QVector3D(10, 6, 40));
    QVERIFY((direction - QVector3D(0, 0, -1)).length() < 1e-6f);
    QVERIFY(MeshRigDocument::perspectiveRay({}, {}, 60, {200, 100}, {150, 50}, &origin, &direction));
    // Half-width right on a 2:1 viewport: x = 0.5 * tan(30 degrees) * 2 per unit depth.
    const QVector3D side = QVector3D(float(std::tan(M_PI / 6)), 0, -1).normalized();
    QVERIFY((direction - side).length() < 1e-6f);
    QVERIFY(MeshRigDocument::perspectiveRay({}, {0, 90, 0}, 60, {100, 100}, {50, 50}, &origin, &direction));
    QVERIFY((direction - QVector3D(-1, 0, 0)).length() < 1e-6f);
    QVERIFY(!MeshRigDocument::perspectiveRay({}, {}, 60, {0, 100}, {0, 0}, &origin, &direction));

    MeshRigDocument doc;
    const Quad q = seamQuad(doc);
    const QVariantMap ray = doc.cameraRay({10, 6, 40}, {}, 60, QSizeF(100, 100), QPointF(60, 50));
    const QVariantMap hit = doc.pickFace(ray.value("origin").value<QVector3D>(),
                                         ray.value("direction").value<QVector3D>());
    QCOMPARE(hit.value("faceId").toString(), q.below);
    const double expectedX = 10 + 40 * 0.2 * std::tan(M_PI / 6);
    const QPointF uv = hit.value("uv").toPointF();
    QVERIFY(qAbs(uv.x() - expectedX / 32) < 1e-5);
    QVERIFY(qAbs(uv.y() - 6.0 / 32) < 1e-5);
    // Painting through the camera lands on the texel under that surface point.
    QVERIFY(doc.beginPaint());
    QVERIFY(doc.paintRay(ray.value("origin").value<QVector3D>(), ray.value("direction").value<QVector3D>(),
                         QColor(255, 255, 0), 1));
    QVERIFY(doc.endPaint());
    QCOMPARE(doc.texture().pixel(int(expectedX), 32 - 7), qRgba(255, 255, 0, 255));
    QVERIFY(doc.pickFace({100, 100, 40}, {0, 0, -1}).isEmpty());
}

void MeshRigPaintTest::materialChannelsPaintSaveAndReopen()
{
    MeshRigDocument doc;
    seamQuad(doc);
    QCOMPARE(MeshRigDocument::textureChannels(),
             QStringList({"baseColor", "roughness", "metalness"}));
    const QImage base = doc.texture(QStringLiteral("baseColor"));
    QCOMPARE(doc.texture(QStringLiteral("roughness")).pixel(0, 0), qRgb(204, 204, 204));
    QCOMPARE(doc.texture(QStringLiteral("metalness")).pixel(0, 0), qRgb(0, 0, 0));
    QVERIFY(!doc.beginPaint(QStringLiteral("specular")));
    QVERIFY(doc.beginPaint(QStringLiteral("roughness")));
    QVERIFY(doc.paintRay({9.2f, 7.1f, 10}, {0, 0, -1}, QColor(100, 100, 100), 3));
    QVERIFY(doc.endPaint());
    QVERIFY(doc.beginPaint(QStringLiteral("metalness")));
    // Scalar channels store the colour's gray value: qGray(255, 0, 0) == 87.
    QVERIFY(doc.paintRay({9.2f, 7.1f, 10}, {0, 0, -1}, QColor(255, 0, 0), 1));
    QVERIFY(doc.endPaint());
    QCOMPARE(doc.texture(), base);
    QCOMPARE(doc.texture(QStringLiteral("roughness")).pixel(9, 24), qRgb(100, 100, 100));
    QCOMPARE(doc.texture(QStringLiteral("metalness")).pixel(9, 24), qRgb(87, 87, 87));
    QCOMPARE(doc.texture(QStringLiteral("metalness")).pixel(0, 0), qRgb(0, 0, 0));

    QTemporaryDir dir; QVERIFY(dir.isValid());
    QVERIFY(doc.saveToFile(dir.filePath("mesh.json")));
    MeshRigDocument reopened;
    QVERIFY(reopened.loadFromFile(dir.filePath("mesh.json")));
    for (const QString &channel : MeshRigDocument::textureChannels()) {
        QCOMPARE(reopened.texture(channel), doc.texture(channel));
        QVERIFY(doc.exportTexturePng(dir.filePath(channel + ".png"), channel));
        const QImage saved(dir.filePath(channel + ".png"));
        QCOMPARE(saved.convertToFormat(QImage::Format_ARGB32_Premultiplied).pixel(9, 24),
                 doc.texture(channel).pixel(9, 24));
        QVERIFY(reopened.textureDataUrls().value(channel).toString().startsWith("data:image/png;base64,"));
    }
    QVERIFY(!doc.exportTexturePng(dir.filePath("x.png"), QStringLiteral("specular")));
}

void MeshRigPaintTest::invalidPaintLeavesStateIntact()
{
    MeshRigDocument doc;
    seamQuad(doc);
    const QImage white = doc.texture();
    QVERIFY(!doc.paintRay({8, 4, 10}, {0, 0, -1}, Qt::red, 2)); // no stroke
    QVERIFY(doc.beginPaint());
    QVERIFY(!doc.beginPaint());
    QVERIFY(!doc.paintRay({40, 40, 10}, {0, 0, -1}, Qt::red, 2)); // miss keeps the stroke open
    QVERIFY(doc.painting());
    QVERIFY(!doc.paintRay({8, 4, 10}, {0, 0, -1}, Qt::red, 2, 1.0, 1.5)); // hardness out of range
    QVERIFY(!doc.setUV(doc.faces()[0].toMap().value("faceId").toString(), 0, {0.1, 0.1})); // locked while painting
    QVERIFY(doc.paintRay({8, 4, 10}, {0, 0, -1}, Qt::red, 2));
    doc.cancelPaint();
    QVERIFY(!doc.painting());
    QCOMPARE(doc.texture(), white);
    QVERIFY(!doc.endPaint());
}

QTEST_GUILESS_MAIN(MeshRigPaintTest)
#include "tst_mesh_rig_paint.moc"
