// SPDX-License-Identifier: LGPL-3.0-or-later
#include "cad_document.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>
#include <cmath>

using namespace QindaTK;

class TstCadDocument : public QObject {
    Q_OBJECT
private slots:
    void referenceSketchConstraintsAndPersistence();
    void arcsDimensionsTangencyAndExports();
    void constraintsRemovableAfterReopen();
    void transactionsAreOneUndoStep();
    void reopenAcceptsSketchSavedWithinTolerance();
    void constrainedExportMatchesModel();
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

// WP-083 audit: a constraint could only be dropped by undo (lost on reopen)
// or by deleting the geometry it references.
void TstCadDocument::constraintsRemovableAfterReopen()
{
    CadDocument sketch;
    const QString a = sketch.addPoint(0, 0), b = sketch.addPoint(40, 3);
    const QString line = sketch.addSegment(a, b);
    const QString level = sketch.addConstraint(QStringLiteral("horizontal"), line);
    const QString span = sketch.addConstraint(QStringLiteral("distance"), a, b, 40);
    QVERIFY(!level.isEmpty() && !span.isEmpty());
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("mistake.qcad"));
    QVERIFY(sketch.saveToFile(path));
    CadDocument reopened;
    QVERIFY(reopened.loadFromFile(path));
    QVERIFY(!reopened.canUndo());
    const QVariantList points = reopened.points();
    const qulonglong revision = reopened.revision();
    QVERIFY(reopened.removeConstraint(level));
    QCOMPARE(reopened.revision(), revision + 1);
    QCOMPARE(reopened.constraints().size(), 1);
    QCOMPARE(reopened.points(), points); // removal never moves geometry
    QCOMPARE(reopened.segments().size(), 1);
    // The segment is free to tilt now, while the distance still holds.
    QVERIFY(reopened.movePoint(b, 30, 20));
    QVERIFY(std::abs(reopened.measure(b).value(QStringLiteral("y")).toDouble()) > 1);
    QVERIFY(std::abs(reopened.measure(line).value(QStringLiteral("length")).toDouble() - 40) < 1e-5);
    QVERIFY(reopened.undo());
    QVERIFY(reopened.undo());
    QCOMPARE(reopened.constraints().size(), 2);
    // removeEntity accepts a constraint ID too, as one undo step.
    QVERIFY(reopened.removeEntity(span));
    QCOMPARE(reopened.constraints().size(), 1);
    QCOMPARE(reopened.points(), points);
    QVERIFY(!reopened.removeConstraint(span));
    QCOMPARE(reopened.lastError(), QStringLiteral("Unknown CAD constraint"));
    QVERIFY(!reopened.removeConstraint(line)); // an entity is not a constraint
    QCOMPARE(reopened.segments().size(), 1);
    QVERIFY(reopened.undo());
    QCOMPARE(reopened.constraints().size(), 2);
}

// D-283: one view gesture (two new points plus a segment) is one undo step,
// and a cancelled gesture leaves state and history exactly as they were.
void TstCadDocument::transactionsAreOneUndoStep()
{
    CadDocument sketch;
    const QString seed = sketch.addPoint(-5, -5);
    QVERIFY(sketch.beginTransaction());
    const QString a = sketch.addPoint(0, 0);
    QVERIFY(sketch.beginTransaction()); // nested pairs join the outer one
    const QString b = sketch.addPoint(10, 0);
    QVERIFY(sketch.commitTransaction());
    QVERIFY(!sketch.undo()); // refused while the outer transaction is open
    QVERIFY(sketch.lastError().contains(QStringLiteral("open CAD edit")));
    const QString line = sketch.addSegment(a, b);
    QVERIFY(!line.isEmpty());
    QVERIFY(sketch.commitTransaction());
    QVERIFY(!sketch.commitTransaction());
    QCOMPARE(sketch.points().size(), 3);
    QVERIFY(sketch.undo());
    QCOMPARE(sketch.points().size(), 1);
    QCOMPARE(sketch.segments().size(), 0);
    QVERIFY(sketch.redo());
    QCOMPARE(sketch.segments().size(), 1);
    QVERIFY(sketch.undo());

    // Cancel: restores points, undo stack and redo stack from begin.
    const QVariantList before = sketch.points();
    QVERIFY(sketch.canRedo());
    QVERIFY(sketch.beginTransaction());
    const QString c = sketch.addPoint(1, 1);
    QVERIFY(sketch.addSegment(c, c).isEmpty());
    const QString error = sketch.lastError();
    QVERIFY(sketch.cancelTransaction());
    QCOMPARE(sketch.points(), before);
    QCOMPARE(sketch.lastError(), error); // the reason survives the rollback
    QVERIFY(sketch.canRedo());
    QVERIFY(sketch.canUndo());
    QVERIFY(sketch.undo());
    QVERIFY(sketch.points().isEmpty());
    QVERIFY(!sketch.cancelTransaction());
    QVERIFY(sketch.redo());
    QCOMPARE(sketch.points().at(0).toMap().value(QStringLiteral("entityId")).toString(), seed);
    // An empty transaction records nothing.
    const qulonglong revision = sketch.revision();
    QVERIFY(sketch.beginTransaction());
    QVERIFY(sketch.commitTransaction());
    QCOMPARE(sketch.revision(), revision);
}

namespace {
QByteArray angleFixture(double degrees)
{
    const double radians = degrees * 3.14159265358979323846 / 180.0;
    const QJsonArray points{
        QJsonObject{{QStringLiteral("id"), QStringLiteral("p0")}, {QStringLiteral("xMm"), 0.0},
                    {QStringLiteral("yMm"), 0.0}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("p1")},
                    {QStringLiteral("xMm"), 1000.0 * std::cos(radians)},
                    {QStringLiteral("yMm"), 1000.0 * std::sin(radians)}}};
    return QJsonDocument(QJsonObject{
        {QStringLiteral("format"), QStringLiteral("qindatk.cad/1")},
        {QStringLiteral("version"), 1}, {QStringLiteral("units"), QStringLiteral("mm")},
        {QStringLiteral("points"), points},
        {QStringLiteral("segments"), QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("s")},
            {QStringLiteral("start"), QStringLiteral("p0")}, {QStringLiteral("end"), QStringLiteral("p1")}}}},
        {QStringLiteral("circles"), QJsonArray{}},
        {QStringLiteral("constraints"), QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("k")},
            {QStringLiteral("kind"), QStringLiteral("angle")}, {QStringLiteral("first"), QStringLiteral("s")},
            {QStringLiteral("second"), QString()}, {QStringLiteral("valueMm"), 30.0}}}}}).toJson();
}
}

// WP-083 audit: reopen used to run a projection pass and reject any point
// that moved more than 1e-6 mm. A 1 m segment whose angle residual is inside
// the 1e-6 degree commit tolerance moves ~8e-6 mm under that pass, so a
// sketch the solver accepted could fail to reopen.
void TstCadDocument::reopenAcceptsSketchSavedWithinTolerance()
{
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("tolerance.qcad"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(angleFixture(30.0000009));
    file.close();
    CadDocument sketch;
    QVERIFY2(sketch.loadFromFile(path), qPrintable(sketch.lastError()));
    // Loaded untouched: bit-exact coordinates from the file.
    const double radians = 30.0000009 * 3.14159265358979323846 / 180.0;
    QCOMPARE(sketch.measure(QStringLiteral("p1")).value(QStringLiteral("x")).toDouble(),
             1000.0 * std::cos(radians));
    QCOMPARE(sketch.measure(QStringLiteral("p1")).value(QStringLiteral("y")).toDouble(),
             1000.0 * std::sin(radians));
    // It saves and reopens again byte for byte.
    const QString again = directory.filePath(QStringLiteral("again.qcad"));
    QVERIFY(sketch.saveToFile(again));
    CadDocument reopened;
    QVERIFY(reopened.loadFromFile(again));
    QCOMPARE(reopened.points(), sketch.points());
    // Outside the tolerance the file is still rejected, atomically.
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(angleFixture(30.00001));
    file.close();
    QVERIFY(!reopened.loadFromFile(path));
    QCOMPARE(reopened.lastError(), QStringLiteral("CAD file violates saved constraints"));
    QCOMPARE(reopened.points(), sketch.points());
}

namespace {
struct Lines { QVector<QLineF> svg; QVector<QLineF> dxf; };
Lines exportedLines(CadDocument &sketch, const QString &directory)
{
    Lines result;
    const QString svgPath = directory + QStringLiteral("/constrained.svg");
    const QString dxfPath = directory + QStringLiteral("/constrained.dxf");
    if (!sketch.exportSvg(svgPath) || !sketch.exportDxf(dxfPath)) return result;
    QFile svg(svgPath), dxf(dxfPath);
    if (!svg.open(QIODevice::ReadOnly) || !dxf.open(QIODevice::ReadOnly)) return result;
    static const QRegularExpression line(
        QStringLiteral("<line x1=\"([^\"]+)\" y1=\"([^\"]+)\" x2=\"([^\"]+)\" y2=\"([^\"]+)\"/>"));
    for (auto it = line.globalMatch(QString::fromUtf8(svg.readAll())); it.hasNext();) {
        const auto m = it.next();
        result.svg.append(QLineF(m.captured(1).toDouble(), m.captured(2).toDouble(),
                                 m.captured(3).toDouble(), m.captured(4).toDouble()));
    }
    const QStringList codes = QString::fromUtf8(dxf.readAll()).split(QLatin1Char('\n'));
    for (int i = 0; i + 13 < codes.size(); ++i)
        if (codes.at(i) == QLatin1String("0") && codes.at(i + 1) == QLatin1String("LINE"))
            result.dxf.append(QLineF(codes.at(i + 3).toDouble(), codes.at(i + 5).toDouble(),
                                     codes.at(i + 9).toDouble(), codes.at(i + 11).toDouble()));
    return result;
}
}

// EXT-04 completion check: constrained dimensions survive dimension edits,
// unit display changes and undo into SVG and DXF output (both millimeters).
void TstCadDocument::constrainedExportMatchesModel()
{
    CadDocument sketch;
    const QString a = sketch.addPoint(0, 0), b = sketch.addPoint(118, 2);
    const QString c = sketch.addPoint(121, 83), d = sketch.addPoint(1, 79);
    const QString ab = sketch.addSegment(a, b), bc = sketch.addSegment(b, c);
    const QString cd = sketch.addSegment(c, d), da = sketch.addSegment(d, a);
    QVERIFY(!sketch.addConstraint(QStringLiteral("horizontal"), ab).isEmpty());
    QVERIFY(!sketch.addConstraint(QStringLiteral("vertical"), bc).isEmpty());
    QVERIFY(!sketch.addConstraint(QStringLiteral("horizontal"), cd).isEmpty());
    QVERIFY(!sketch.addConstraint(QStringLiteral("vertical"), da).isEmpty());
    const QString width = sketch.addConstraint(QStringLiteral("distance"), a, b, 120);
    const QString height = sketch.addConstraint(QStringLiteral("distance"), b, c, 80);
    QVERIFY(!width.isEmpty() && !height.isEmpty());
    QVERIFY(sketch.setDimension(width, 75));
    QVERIFY(sketch.changeUnits(QStringLiteral("in")));
    QVERIFY(sketch.setDimension(height, 2)); // 50.8 mm
    QVERIFY(sketch.setDimension(width, 4));  // 101.6 mm, then undone
    QVERIFY(sketch.undo());
    QTemporaryDir directory;
    const Lines lines = exportedLines(sketch, directory.path());
    QCOMPARE(lines.svg.size(), 4);
    QCOMPARE(lines.dxf.size(), 4);
    const double expected[] = {75.0, 50.8, 75.0, 50.8};
    const QString ids[] = {ab, bc, cd, da};
    for (int i = 0; i < 4; ++i) {
        const double model = sketch.measure(ids[i]).value(QStringLiteral("length")).toDouble() * 25.4;
        QVERIFY2(std::abs(model - expected[i]) < 1e-5, qPrintable(QString::number(model, 'g', 17)));
        QVERIFY(std::abs(lines.svg.at(i).length() - model) < 1e-9);
        QVERIFY(std::abs(lines.dxf.at(i).length() - model) < 1e-9);
        // DXF negates Y: same endpoints, mirrored.
        QCOMPARE(lines.dxf.at(i).x1(), lines.svg.at(i).x1());
        QCOMPARE(lines.dxf.at(i).y1(), -lines.svg.at(i).y1() + 0.0);
    }
    QVERIFY(std::abs(lines.svg.at(0).dy()) < 1e-6); // horizontal survives export
    QVERIFY(std::abs(lines.svg.at(1).dx()) < 1e-6); // vertical survives export
}

QTEST_MAIN(TstCadDocument)
#include "tst_cad_document.moc"
