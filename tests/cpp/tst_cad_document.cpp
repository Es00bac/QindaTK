// SPDX-License-Identifier: LGPL-3.0-or-later
#include "cad_document.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <cmath>

using namespace QindaTK;

class TstCadDocument : public QObject {
    Q_OBJECT
private slots:
    void referenceSketchConstraintsAndPersistence();
    void arcsDimensionsTangencyAndExports();
};

void TstCadDocument::referenceSketchConstraintsAndPersistence()
{
    CadDocument sketch;
    const QString a = sketch.addPoint(0, 0);
    const QString b = sketch.addPoint(101, 1);
    const QString c = sketch.addPoint(101, 61);
    const QString d = sketch.addPoint(0, 60);
    const QString center = sketch.addPoint(50, 30);
    const QString ab = sketch.addSegment(a, b);
    const QString bc = sketch.addSegment(b, c);
    const QString cd = sketch.addSegment(c, d);
    const QString da = sketch.addSegment(d, a);
    const QString circle = sketch.addCircle(center, 10);
    QVERIFY(!circle.isEmpty());
    QVERIFY(!sketch.addConstraint(QStringLiteral("horizontal"), ab).isEmpty());
    QVERIFY(!sketch.addConstraint(QStringLiteral("vertical"), bc).isEmpty());
    QVERIFY(!sketch.addConstraint(QStringLiteral("horizontal"), cd).isEmpty());
    QVERIFY(!sketch.addConstraint(QStringLiteral("vertical"), da).isEmpty());
    QVERIFY(!sketch.addConstraint(QStringLiteral("distance"), a, b, 100).isEmpty());
    QVERIFY(!sketch.addConstraint(QStringLiteral("distance"), b, c, 60).isEmpty());
    const QString e = sketch.addPoint(200, 200), f = sketch.addPoint(205, 207);
    QVERIFY(!sketch.addConstraint(QStringLiteral("coincident"), e, f).isEmpty());
    const auto joined = sketch.points();
    QCOMPARE(joined.at(5).toMap().value(QStringLiteral("x")),
             joined.at(6).toMap().value(QStringLiteral("x")));
    QCOMPARE(joined.at(5).toMap().value(QStringLiteral("y")),
             joined.at(6).toMap().value(QStringLiteral("y")));
    QVERIFY(std::abs(sketch.measure(ab).value(QStringLiteral("length")).toDouble() - 100) < 1e-5);
    QVERIFY(std::abs(sketch.measure(bc).value(QStringLiteral("length")).toDouble() - 60) < 1e-5);
    QCOMPARE(sketch.measure(circle).value(QStringLiteral("area")).toDouble(),
             3.14159265358979323846 * 100);
    const QVariantMap nearCorner = sketch.snap(0.1, 0.1, 3, 10);
    QCOMPARE(nearCorner.value(QStringLiteral("kind")).toString(), QStringLiteral("point"));
    QCOMPARE(nearCorner.value(QStringLiteral("pointId")).toString(), a);
    const QVariantMap grid = sketch.snap(20.2, 9.9, 1, 10);
    QCOMPARE(grid.value(QStringLiteral("kind")).toString(), QStringLiteral("grid"));
    QCOMPARE(grid.value(QStringLiteral("x")).toDouble(), 20.0);
    QCOMPARE(grid.value(QStringLiteral("y")).toDouble(), 10.0);

    const QVariantList stablePoints = sketch.points();
    const qulonglong beforeRejected = sketch.revision();
    QVERIFY(sketch.addConstraint(QStringLiteral("vertical"), ab).isEmpty());
    QVERIFY(sketch.lastError().contains(QStringLiteral("Unsatisfiable")));
    QCOMPARE(sketch.points(), stablePoints);
    QCOMPARE(sketch.revision(), beforeRejected);
    QVERIFY(sketch.addSegment(a, a).isEmpty());
    QVERIFY(sketch.addCircle(center, 0).isEmpty());

    QVERIFY(sketch.movePoint(a, 10, 10));
    const QVariantList movedPoints = sketch.points();
    QVERIFY(std::abs(sketch.measure(ab).value(QStringLiteral("length")).toDouble() - 100) < 1e-5);
    QVERIFY(sketch.undo());
    QCOMPARE(sketch.points(), stablePoints);
    QVERIFY(sketch.redo());
    QCOMPARE(sketch.points(), movedPoints);

    QVERIFY(sketch.changeUnits(QStringLiteral("in")));
    QVERIFY(std::abs(sketch.measure(ab).value(QStringLiteral("length")).toDouble()
                     - 100.0 / 25.4) < 1e-6);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("reference.qcad"));
    QVERIFY(sketch.saveToFile(path));
    CadDocument reopened;
    QVERIFY(reopened.loadFromFile(path));
    QCOMPARE(reopened.units(), QStringLiteral("in"));
    QCOMPARE(reopened.points(), sketch.points());
    QCOMPARE(reopened.segments(), sketch.segments());
    QCOMPARE(reopened.circles(), sketch.circles());
    QCOMPARE(reopened.constraints(), sketch.constraints());
    QVERIFY(!reopened.canUndo());

    QFile broken(path);
    QVERIFY(broken.open(QIODevice::WriteOnly | QIODevice::Truncate));
    broken.write("{\"format\":\"qindatk.cad/1\",\"version\":1,\"units\":\"mm\","
                 "\"points\":[],\"segments\":[{\"id\":\"dangling\",\"start\":\"x\",\"end\":\"y\"}],"
                 "\"circles\":[],\"constraints\":[]}");
    broken.close();
    const QVariantList beforeBadLoad = reopened.points();
    QVERIFY(!reopened.loadFromFile(path));
    QCOMPARE(reopened.points(), beforeBadLoad);
    QVERIFY(reopened.removeEntity(a));
    QCOMPARE(reopened.segments().size(), 2);
    QVERIFY(reopened.undo());
    QCOMPARE(reopened.segments(), sketch.segments());
}

void TstCadDocument::arcsDimensionsTangencyAndExports()
{
    CadDocument angled;
    const QString ap = angled.addPoint(0, 0), bp = angled.addPoint(10, 0);
    const QString angledLine = angled.addSegment(ap, bp);
    const QString angledDimension = angled.addConstraint(QStringLiteral("angle"), angledLine, {}, 45);
    QVERIFY(!angledDimension.isEmpty());
    QVERIFY(std::abs(angled.measure(angledLine).value(QStringLiteral("angleDegrees")).toDouble()
                     - 45) < 1e-6);
    QVERIFY(angled.setDimension(angledDimension, 30));
    QVERIFY(std::abs(angled.measure(angledLine).value(QStringLiteral("angleDegrees")).toDouble()
                     - 30) < 1e-6);

    CadDocument sketch;
    const QString a = sketch.addPoint(0, 0), b = sketch.addPoint(40, 0);
    const QString line = sketch.addSegment(a, b);
    const QString angle = sketch.addConstraint(QStringLiteral("angle"), line, {}, 0);
    QVERIFY(!angle.isEmpty());
    QVERIFY(!sketch.addConstraint(QStringLiteral("horizontal"), line).isEmpty());
    const QString center = sketch.addPoint(20, 8);
    const QString circle = sketch.addCircle(center, 5);
    const QString tangent = sketch.addConstraint(QStringLiteral("tangent"), line, circle);
    QVERIFY2(!tangent.isEmpty(), qPrintable(sketch.lastError()));
    QVERIFY(std::abs(sketch.measure(center).value(QStringLiteral("y")).toDouble() - 5) < 1e-6);
    QVERIFY(sketch.setCircleRadius(circle, 7));
    QVERIFY(std::abs(sketch.measure(center).value(QStringLiteral("y")).toDouble() - 7) < 1e-6);
    const QVariantList stablePoints = sketch.points();
    QVERIFY(sketch.addConstraint(QStringLiteral("angle"), line, {}, 45).isEmpty());
    QVERIFY(sketch.lastError().contains(QStringLiteral("Unsatisfiable")));
    QCOMPARE(sketch.points(), stablePoints);

    const QString distance = sketch.addConstraint(QStringLiteral("distance"), a, b, 40);
    QVERIFY(!distance.isEmpty());
    QVERIFY(sketch.setDimension(distance, 50));
    QVERIFY(std::abs(sketch.measure(line).value(QStringLiteral("length")).toDouble() - 50) < 1e-5);
    QVERIFY(sketch.undo());
    QVERIFY(std::abs(sketch.measure(line).value(QStringLiteral("length")).toDouble() - 40) < 1e-5);
    QVERIFY(sketch.redo());
    QVERIFY(!sketch.setDimension(angle, 45));
    QVERIFY(sketch.lastError().contains(QStringLiteral("Unsatisfiable")));

    const QString arc = sketch.addArc(center, 10, 0, 90);
    QVERIFY(!arc.isEmpty());
    QVERIFY(std::abs(sketch.measure(arc).value(QStringLiteral("length")).toDouble()
                     - 5 * 3.14159265358979323846) < 1e-9);
    QVERIFY(sketch.setArc(arc, 12, 0, 180));
    QCOMPARE(sketch.arcs().at(0).toMap().value(QStringLiteral("sweepDegrees")).toDouble(), 180.0);
    QVERIFY(sketch.undo());
    QCOMPARE(sketch.arcs().at(0).toMap().value(QStringLiteral("sweepDegrees")).toDouble(), 90.0);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString native = directory.filePath(QStringLiteral("arc.qcad"));
    QVERIFY(sketch.saveToFile(native));
    CadDocument reopened;
    QVERIFY(reopened.loadFromFile(native));
    QCOMPARE(reopened.arcs(), sketch.arcs());
    QCOMPARE(reopened.constraints(), sketch.constraints());

    CadDocument fixture;
    const QString p0 = fixture.addPoint(0, 0), p1 = fixture.addPoint(10, 0);
    const QString pc = fixture.addPoint(5, 5);
    fixture.addSegment(p0, p1);
    fixture.addCircle(pc, 2);
    fixture.addArc(pc, 3, 0, 90);
    const QString svgPath = directory.filePath(QStringLiteral("known.svg"));
    const QString dxfPath = directory.filePath(QStringLiteral("known.dxf"));
    QVERIFY(fixture.exportSvg(svgPath));
    QVERIFY(fixture.exportDxf(dxfPath));
    QFile svg(svgPath), dxf(dxfPath);
    QVERIFY(svg.open(QIODevice::ReadOnly));
    QVERIFY(dxf.open(QIODevice::ReadOnly));
    const QByteArray expectedSvg =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"12mm\" height=\"10mm\" viewBox=\"-1 -1 12 10\">\n"
        "<g fill=\"none\" stroke=\"black\" stroke-width=\"0.25\">\n"
        "<line x1=\"0\" y1=\"0\" x2=\"10\" y2=\"0\"/>\n"
        "<circle cx=\"5\" cy=\"5\" r=\"2\"/>\n"
        "<path d=\"M 8 5 A 3 3 0 0 1 5 8\"/>\n"
        "</g>\n<g fill=\"black\">\n"
        "<circle cx=\"0\" cy=\"0\" r=\"0.5\"/>\n"
        "<circle cx=\"10\" cy=\"0\" r=\"0.5\"/>\n"
        "<circle cx=\"5\" cy=\"5\" r=\"0.5\"/>\n"
        "</g>\n</svg>\n";
    QCOMPARE(svg.readAll(), expectedSvg);
    const QByteArray expectedDxf =
        "0\nSECTION\n2\nHEADER\n9\n$ACADVER\n1\nAC1015\n9\n$INSUNITS\n70\n4\n0\nENDSEC\n"
        "0\nSECTION\n2\nENTITIES\n"
        "0\nPOINT\n10\n0\n20\n0\n30\n0\n"
        "0\nPOINT\n10\n10\n20\n0\n30\n0\n"
        "0\nPOINT\n10\n5\n20\n-5\n30\n0\n"
        "0\nLINE\n10\n0\n20\n0\n30\n0\n11\n10\n21\n0\n31\n0\n"
        "0\nCIRCLE\n10\n5\n20\n-5\n30\n0\n40\n2\n"
        "0\nARC\n10\n5\n20\n-5\n30\n0\n40\n3\n50\n270\n51\n0\n"
        "0\nENDSEC\n0\nEOF\n";
    QCOMPARE(dxf.readAll(), expectedDxf);
}

QTEST_MAIN(TstCadDocument)
#include "tst_cad_document.moc"
