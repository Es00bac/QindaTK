// SPDX-License-Identifier: LGPL-3.0-or-later
// WP-044 path operations: union, intersect, subtract, exclude and combine as
// one reversible document edit, with fill rules, self-intersections,
// transforms and persistence.
#include <QtTest>
#include <QtMath>
#include "canvas_document_format.h"
#include "canvas_geometry.h"
#include "canvas_path.h"
#include "canvas_path_boolean.h"
#include "canvas_style_model.h"
#include "canvas_svg_export.h"

using namespace QindaTK;

namespace {
QVariantList ids(std::initializer_list<CanvasObjectId> values)
{
    QVariantList list;
    for (CanvasObjectId id : values) list.append(QString::number(id));
    return list;
}
} // namespace

class TstCanvasPathBoolean : public QObject {
    Q_OBJECT
private slots:
    void operationsCombineOverlappingShapes();
    void resultsAreReversibleAndPersist();
    void selfIntersectionFollowsTheFillRule();
    void transformsMapIntoTheBackmostSpace();
    void combineKeepsCurves();
    void invalidRequestsChangeNothing();
};

void TstCanvasPathBoolean::operationsCombineOverlappingShapes()
{
    struct Case {
        const char *operation;
        bool backOnly;
        bool both;
        bool frontOnly;
        QRectF bounds;
    };
    const Case cases[] = {
        {"union", true, true, true, QRectF(0, 0, 150, 150)},
        {"intersect", false, true, false, QRectF(50, 50, 50, 50)},
        {"subtract", true, false, false, QRectF(0, 0, 100, 100)},
        {"exclude", true, false, true, QRectF(0, 0, 150, 150)},
    };
    for (const Case &c : cases) {
        CanvasDocument doc;
        const CanvasObjectId back = doc.addRect(0, 0, 100, 100, QColor("#3060a0"), QColor("#102030"), 2);
        const CanvasObjectId front = doc.addRect(50, 50, 100, 100, QColor("#c03030"), {}, 0);
        const qulonglong result = doc.combinePaths(ids({back, front}), QLatin1String(c.operation));
        QVERIFY2(result == back, qPrintable(QLatin1String(c.operation) + QLatin1String(": ") + doc.lastError()));
        QCOMPARE(doc.objectCount(), 1);
        QVERIFY(!doc.object(front));
        const CanvasObject &o = *doc.object(back);
        QCOMPARE(o.shape, CanvasShape::Path);
        QCOMPARE(o.fill, QColor("#3060a0")); // the backmost operand's style
        QCOMPARE(o.stroke, QColor("#102030"));
        QCOMPARE(CanvasGeometry::fillContains(o, QPointF(25, 25)), c.backOnly);
        QCOMPARE(CanvasGeometry::fillContains(o, QPointF(75, 75)), c.both);
        QCOMPARE(CanvasGeometry::fillContains(o, QPointF(125, 125)), c.frontOnly);
        QVERIFY(!CanvasGeometry::fillContains(o, QPointF(125, 25)));
        QCOMPARE(CanvasPath::document(o).boundingRect(), c.bounds);
        for (const CanvasPathNode &node : o.pathNodes) {
            QCOMPARE(node.in, node.anchor); // straight segments
            QCOMPARE(node.out, node.anchor);
        }
        QCOMPARE(doc.undoText(), QStringLiteral("Combine paths"));
    }
}

void TstCanvasPathBoolean::resultsAreReversibleAndPersist()
{
    CanvasDocument doc;
    doc.setPageRect(QRectF(0, 0, 200, 200));
    const CanvasObjectId back = doc.addRect(0, 0, 100, 100, QColor("#3060a0"), {}, 0);
    const CanvasObjectId front = doc.addEllipse(50, 50, 100, 100, QColor("#c03030"), {}, 0);
    const CanvasObject backBefore = *doc.object(back);
    const CanvasObject frontBefore = *doc.object(front);
    QCOMPARE(doc.combinePaths(ids({front, back}), QStringLiteral("union")), qulonglong(back));
    const CanvasObject combined = *doc.object(back);
    QVERIFY(doc.undo());
    QCOMPARE(doc.objectCount(), 2);
    QCOMPARE(*doc.object(back), backBefore);
    QCOMPARE(*doc.object(front), frontBefore);
    QVERIFY(doc.redo());
    QCOMPARE(*doc.object(back), combined);
    QVERIFY(!doc.object(front));

    // The curved edge is flattened within 1e-4 of the 150-unit extent.
    QVERIFY(combined.pathPointCount() > 40);
    const qreal diagonal = M_SQRT1_2;
    QVERIFY(CanvasGeometry::fillContains(combined, QPointF(100 + 49.9 * diagonal, 100 + 49.9 * diagonal)));
    QVERIFY(!CanvasGeometry::fillContains(combined, QPointF(100 + 50.1 * diagonal, 100 + 50.1 * diagonal)));
    QVERIFY(CanvasGeometry::fillContains(combined, QPointF(10, 10)));
    QVERIFY(!CanvasGeometry::fillContains(combined, QPointF(140, 10)));

    OperationError error;
    const auto state = CanvasDocumentFormat::decode(CanvasDocumentFormat::encode(doc.state()), {}, &error);
    QVERIFY2(state.has_value(), qPrintable(error.message));
    QCOMPARE(state->objects.value(back), combined);
    const QByteArray svg = CanvasSvgExport::encode(doc, &error);
    QVERIFY2(!svg.isEmpty(), qPrintable(error.message));
    QVERIFY(svg.contains(" L "));
    QVERIFY(!svg.contains(" C "));
}

void TstCanvasPathBoolean::selfIntersectionFollowsTheFillRule()
{
    // A pentagram's centre has winding number 2: nonzero fills it, even-odd
    // leaves a hole. The square over the top tip makes the clipper cut edges.
    for (const bool nonzero : {true, false}) {
        CanvasDocument doc;
        const QPointF centre(100, 100);
        QVariantList nodes;
        for (int k = 0; k < 5; ++k) {
            const qreal angle = 2 * M_PI * ((2 * k) % 5) / 5;
            nodes.append(QVariantMap{{"x", centre.x() + 80 * std::sin(angle)},
                                     {"y", centre.y() - 80 * std::cos(angle)}});
        }
        const CanvasObjectId star = doc.addPath(nodes, true, QColor(Qt::black), {}, 0);
        QVERIFY(star);
        CanvasStyleModel styles;
        styles.setDocument(&doc);
        QVERIFY(styles.apply(ids({star}), {{"fillRule", nonzero ? "nonzero" : "evenodd"}}));
        QCOMPARE(CanvasGeometry::fillContains(*doc.object(star), centre), nonzero);
        const CanvasObjectId square = doc.addRect(85, 10, 30, 30, QColor(Qt::red), {}, 0);
        QCOMPARE(doc.combinePaths(ids({star, square}), QStringLiteral("union")), qulonglong(star));
        const CanvasObject &o = *doc.object(star);
        QCOMPARE(CanvasGeometry::fillContains(o, centre), nonzero);
        QVERIFY(CanvasGeometry::fillContains(o, QPointF(100, 45))); // the top tip, winding 1
        QVERIFY(CanvasGeometry::fillContains(o, QPointF(88, 12)));  // the square
        QVERIFY(!CanvasGeometry::fillContains(o, QPointF(100, 185)));
    }
}

void TstCanvasPathBoolean::transformsMapIntoTheBackmostSpace()
{
    CanvasDocument doc;
    const CanvasObjectId back = doc.addRect(0, 0, 100, 100, QColor(Qt::blue), {}, 0);
    QVERIFY(doc.setObjectTransform(back, {1, 0, 0, 1, 10, 20}));
    const CanvasObjectId front = doc.addRect(0, 0, 50, 50, QColor(Qt::red), {}, 0);
    // A 45-degree diamond: top (140, 50), left (104.6, 85.4), centre (140, 85.4).
    QVERIFY(doc.setObjectTransform(front, {M_SQRT1_2, M_SQRT1_2, -M_SQRT1_2, M_SQRT1_2, 140, 50}));
    QCOMPARE(doc.combinePaths(ids({back, front}), QStringLiteral("union")), qulonglong(back));
    const CanvasObject &o = *doc.object(back);
    QCOMPARE(o.transform, QTransform::fromTranslate(10, 20)); // the result keeps the backmost transform
    QVERIFY(CanvasGeometry::fillContains(o, QPointF(60, 70)));
    QVERIFY(CanvasGeometry::fillContains(o, QPointF(140, 85.36)));
    QVERIFY(CanvasGeometry::fillContains(o, QPointF(170, 85)));
    QVERIFY(!CanvasGeometry::fillContains(o, QPointF(140, 40)));
    QVERIFY(!CanvasGeometry::fillContains(o, QPointF(5, 70)));
    // Local nodes: the back square's top-left corner is the local origin.
    bool origin = false;
    for (const CanvasPathNode &node : o.pathNodes)
        origin = origin || (std::abs(node.anchor.x()) < 1e-9 && std::abs(node.anchor.y()) < 1e-9);
    QVERIFY(origin);
}

void TstCanvasPathBoolean::combineKeepsCurves()
{
    CanvasDocument doc;
    const CanvasObjectId outer = doc.addEllipse(0, 0, 40, 40, QColor(Qt::black), {}, 0);
    const CanvasObjectId inner = doc.addEllipse(10, 10, 20, 20, QColor(Qt::white), {}, 0);
    QCOMPARE(doc.combinePaths(ids({outer, inner}), QStringLiteral("combine")), qulonglong(outer));
    const CanvasObject &o = *doc.object(outer);
    QCOMPARE(doc.pathContourCount(outer), 2);
    QCOMPARE(o.pathNodes.size(), 4);
    QCOMPARE(o.pathNodes.first().anchor, QPointF(40, 20));
    QVERIFY(o.pathNodes.first().out != o.pathNodes.first().anchor); // cubic handles kept
    QCOMPARE(o.fillRule, Qt::OddEvenFill);
    QVERIFY(!CanvasGeometry::fillContains(o, QPointF(20, 20))); // the inner ellipse is a hole
    QVERIFY(CanvasGeometry::fillContains(o, QPointF(3, 20)));
    QVERIFY(doc.undo());
    QCOMPARE(doc.objectCount(), 2);
    QCOMPARE(doc.object(outer)->shape, CanvasShape::Ellipse);
}

void TstCanvasPathBoolean::invalidRequestsChangeNothing()
{
    CanvasDocument doc;
    const CanvasObjectId a = doc.addRect(0, 0, 10, 10, QColor(Qt::blue), {}, 0);
    const CanvasObjectId b = doc.addRect(5, 5, 10, 10, QColor(Qt::red), {}, 0);
    const CanvasObjectId line = doc.addPolyline({QPointF(0, 0), QPointF(10, 10)}, QColor(Qt::black), 1);
    const CanvasObjectId far = doc.addRect(500, 500, 10, 10, QColor(Qt::green), {}, 0);
    QVERIFY(a && b && line && far);
    const QString undo = doc.undoText();
    QCOMPARE(doc.combinePaths(ids({a}), QStringLiteral("union")), qulonglong(0));
    QVERIFY(!doc.lastError().isEmpty());
    QCOMPARE(doc.combinePaths(ids({a, b}), QStringLiteral("melt")), qulonglong(0));
    QCOMPARE(doc.combinePaths(ids({a, line}), QStringLiteral("union")), qulonglong(0));
    QCOMPARE(doc.combinePaths({QString::number(a), QStringLiteral("999")}, QStringLiteral("union")), qulonglong(0));
    QCOMPARE(doc.combinePaths(ids({a, far}), QStringLiteral("intersect")), qulonglong(0));
    QCOMPARE(doc.lastError(), QStringLiteral("The combined shape is empty"));
    QVERIFY(doc.setObjectLocked(b, true));
    QCOMPARE(doc.combinePaths(ids({a, b}), QStringLiteral("union")), qulonglong(0));
    QVERIFY(doc.undo()); // unlock again
    QCOMPARE(doc.objectCount(), 4);
    QCOMPARE(doc.undoText(), undo);
    QVERIFY(!CanvasPathBoolean::operation(QStringLiteral("xor")));
    QVERIFY(CanvasPathBoolean::operation(QStringLiteral("exclude")) == CanvasPathBoolean::Operation::Exclude);
}

QTEST_MAIN(TstCanvasPathBoolean)
#include "tst_canvas_path_boolean.moc"
