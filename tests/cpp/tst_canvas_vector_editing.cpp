// SPDX-License-Identifier: LGPL-3.0-or-later
// WP-044 / CAN-19 node editing: stable node and segment ids, a selection that
// survives segment insertion and undo, insert/delete/split/join/node-type
// requests, degenerate refusals; and stroke bounds that hold miter spikes.
#include <QtTest>
#include <QPainterPathStroker>
#include "canvas_path.h"
#include "canvas_path_edit_model.h"
#include "canvas_render.h"
#include "canvas_style_model.h"

using namespace QindaTK;

namespace {
QVariantMap node(qreal x, qreal y, qreal inX, qreal inY, qreal outX, qreal outY)
{
    return {{"x", x}, {"y", y}, {"inX", inX}, {"inY", inY}, {"outX", outX}, {"outY", outY}};
}

QPointF cubic(QPointF p0, QPointF p1, QPointF p2, QPointF p3, qreal t)
{
    const qreal u = 1 - t;
    return u * u * u * p0 + 3 * u * u * t * p1 + 3 * u * t * t * p2 + t * t * t * p3;
}

bool near(QPointF a, QPointF b) { return QLineF(a, b).length() < 1e-9; }

QStringList nodeIds(const CanvasPathEditModel &model, int contour = 0)
{
    QStringList ids;
    for (const QVariant &value : model.nodes())
        if (value.toMap().value(QStringLiteral("contour")).toInt() == contour)
            ids.append(value.toMap().value(QStringLiteral("id")).toString());
    return ids;
}

QPointF anchorOf(const CanvasPathEditModel &model, const QString &id)
{
    const QVariantMap n = model.node(id);
    return {n.value(QStringLiteral("x")).toDouble(), n.value(QStringLiteral("y")).toDouble()};
}
} // namespace

class TstCanvasVectorEditing : public QObject {
    Q_OBJECT
private slots:
    void selectionSurvivesSegmentInsertion();
    void nodeTypesDeleteAndDegenerateRefusals();
    void splitAndJoinKeepIds();
    void idsFollowEditsMadeElsewhere();
    void miterJoinsStayInsideBounds();
};

void TstCanvasVectorEditing::selectionSurvivesSegmentInsertion()
{
    CanvasDocument doc;
    const CanvasObjectId id = doc.addPath({node(20, 20, 20, 20, 60, 0), node(120, 20, 100, 0, 120, 20),
                                           node(120, 120, 120, 120, 120, 120), node(20, 120, 20, 120, 20, 120)},
                                          true, QColor("#2877c6"), QColor(Qt::black), 2);
    QVERIFY(id);
    CanvasPathEditModel model;
    model.setDocument(&doc);
    model.setObjectId(QString::number(id));
    const QStringList original = nodeIds(model);
    QCOMPARE(original.size(), 4);
    QCOMPARE(QSet<QString>(original.cbegin(), original.cend()).size(), 4);
    QCOMPARE(model.segments().size(), 4);
    QCOMPARE(model.segments().first().toMap().value(QStringLiteral("id")).toString(), QLatin1Char('s') + original.at(0));

    // Select the third node, then insert a node on the first segment.
    QVERIFY(model.selectNode(original.at(2)));
    const QPointF selectedAt = anchorOf(model, original.at(2));
    const CanvasPathNode a = doc.object(id)->pathNodes.at(0), b = doc.object(id)->pathNodes.at(1);
    const QString inserted = model.insertNode(QLatin1Char('s') + original.at(0), 0.5);
    QVERIFY2(!inserted.isEmpty(), qPrintable(model.lastError()));
    QCOMPARE(nodeIds(model), (QStringList{original.at(0), inserted, original.at(1), original.at(2), original.at(3)}));
    QCOMPARE(model.selectedNodeIds(), QStringList{original.at(2)});
    QCOMPARE(anchorOf(model, original.at(2)), selectedAt);
    // The curve is unchanged: the new node sits on it and each half traces it.
    const auto &nodes = doc.object(id)->pathNodes;
    QVERIFY(near(nodes.at(1).anchor, cubic(a.anchor, a.out, b.in, b.anchor, 0.5)));
    QVERIFY(near(cubic(nodes.at(0).anchor, nodes.at(0).out, nodes.at(1).in, nodes.at(1).anchor, 0.5),
                 cubic(a.anchor, a.out, b.in, b.anchor, 0.25)));
    QVERIFY(near(cubic(nodes.at(1).anchor, nodes.at(1).out, nodes.at(2).in, nodes.at(2).anchor, 0.5),
                 cubic(a.anchor, a.out, b.in, b.anchor, 0.75)));
    // Undo and redo keep every id and the selection.
    QVERIFY(doc.undo());
    QCOMPARE(nodeIds(model), original);
    QCOMPARE(model.selectedNodeIds(), QStringList{original.at(2)});
    QVERIFY(doc.redo());
    QCOMPARE(nodeIds(model).at(1), inserted);
    QCOMPARE(model.selectedNodeIds(), QStringList{original.at(2)});
    // Moving the selected node keeps it selected.
    QVERIFY(model.moveNodePart(original.at(2), QStringLiteral("anchor"), 130, 130));
    QCOMPARE(model.selectedNodeIds(), QStringList{original.at(2)});
    QCOMPARE(anchorOf(model, original.at(2)), QPointF(130, 130));
    // Refusals explain themselves and change nothing.
    QVERIFY(model.insertNode(QStringLiteral("snope"), 0.5).isEmpty());
    QVERIFY(!model.lastError().isEmpty());
    QVERIFY(model.insertNode(QLatin1Char('s') + original.at(0), 1.0).isEmpty());
}

void TstCanvasVectorEditing::nodeTypesDeleteAndDegenerateRefusals()
{
    CanvasDocument doc;
    const CanvasObjectId id = doc.addPath({node(0, 0, 0, 0, 0, 0), node(100, 0, 100, 0, 100, 0),
                                           node(100, 100, 100, 100, 100, 100), node(0, 100, 0, 100, 0, 100)},
                                          true, QColor("#2877c6"), {}, 0);
    CanvasPathEditModel model;
    model.setDocument(&doc);
    model.setObjectId(QString::number(id));
    const QStringList ids = nodeIds(model);
    QCOMPARE(model.node(ids.at(1)).value(QStringLiteral("type")).toString(), QStringLiteral("corner"));
    model.setSelectedNodeIds({ids.at(1)});
    QVERIFY(model.setNodeType(QStringLiteral("smooth")));
    // Handles align with the chord through the neighbours, (0,0) -> (100,100).
    const CanvasPathNode smooth = doc.object(id)->pathNodes.at(1);
    QVERIFY(std::abs((smooth.out - smooth.anchor).x() - (smooth.out - smooth.anchor).y()) < 1e-9);
    QCOMPARE(model.node(ids.at(1)).value(QStringLiteral("type")).toString(), QStringLiteral("symmetric"));
    QVERIFY(model.moveNodePart(ids.at(1), QStringLiteral("out"), 130, 30));
    QCOMPARE(model.node(ids.at(1)).value(QStringLiteral("type")).toString(), QStringLiteral("smooth"));
    QVERIFY(model.setNodeType(QStringLiteral("symmetric")));
    const CanvasPathNode symmetric = doc.object(id)->pathNodes.at(1);
    QCOMPARE(QLineF(symmetric.anchor, symmetric.in).length(), QLineF(symmetric.anchor, symmetric.out).length());
    QVERIFY(model.setNodeType(QStringLiteral("corner")));
    QCOMPARE(doc.object(id)->pathNodes.at(1).out, QPointF(100, 0));
    QVERIFY(!model.setNodeType(QStringLiteral("wobbly")));
    QVERIFY(doc.undo());
    QCOMPARE(doc.object(id)->pathNodes.at(1), symmetric);

    // Deleting to below three nodes of a closed contour is refused.
    QVERIFY(model.deleteNodes({ids.at(3)}));
    QCOMPARE(nodeIds(model).size(), 3);
    const CanvasObject before = *doc.object(id);
    QVERIFY(!model.deleteNodes({ids.at(0)}));
    QVERIFY(model.lastError().contains(QStringLiteral("three")));
    QCOMPARE(*doc.object(id), before);
    QVERIFY(!model.deleteNodes()); // nothing selected is explained too
    // A locked path is refused.
    QVERIFY(doc.setObjectLocked(id, true));
    QVERIFY(!model.setNodeType(QStringLiteral("smooth"), {ids.at(1)}));
    QVERIFY(doc.setObjectLocked(id, false));

    // Node edits demote a parametric shape; undo brings the definition back.
    const CanvasObjectId star = doc.addParametricShape(QStringLiteral("star"), 200, 0, 100, 100,
                                                       {{"points", 5}, {"innerRadius", 0.5}}, QColor(Qt::red), {}, 0);
    QVERIFY(star);
    model.setObjectId(QString::number(star));
    QCOMPARE(model.selectedNodeIds(), QStringList{});
    QVERIFY(!model.insertNode(QLatin1Char('s') + nodeIds(model).at(0)).isEmpty());
    QCOMPARE(doc.object(star)->parametricKind, CanvasParametricKind::None);
    QVERIFY(doc.undo());
    QCOMPARE(doc.object(star)->parametricKind, CanvasParametricKind::Star);
}

void TstCanvasVectorEditing::splitAndJoinKeepIds()
{
    CanvasDocument doc;
    const CanvasObjectId id = doc.addPath({node(0, 0, 0, 0, 0, 0), node(100, 0, 100, 0, 100, 0),
                                           node(100, 100, 100, 100, 100, 100), node(0, 100, 0, 100, 0, 100)},
                                          true, {}, QColor(Qt::black), 2);
    CanvasPathEditModel model;
    model.setDocument(&doc);
    model.setObjectId(QString::number(id));
    const QStringList ids = nodeIds(model);
    // Splitting a closed contour opens it at the node, which now starts it.
    QVERIFY(model.splitAt(ids.at(2)));
    QCOMPARE(doc.object(id)->pathClosed, false);
    const QStringList open = nodeIds(model);
    QCOMPARE(open.size(), 5);
    QCOMPARE(open.mid(0, 4), (QStringList{ids.at(2), ids.at(3), ids.at(0), ids.at(1)}));
    QVERIFY(!ids.contains(open.at(4)));
    QCOMPARE(anchorOf(model, open.at(4)), QPointF(100, 100));
    QVERIFY(!model.splitAt(open.at(0))); // already an end
    // Joining its two coincident ends closes it with the original four ids.
    QVERIFY(model.join(open.at(4), open.at(0)));
    QVERIFY(doc.object(id)->pathClosed);
    QCOMPARE(nodeIds(model), open.mid(0, 4));
    // Cutting an open contour in two, then joining the pieces again.
    QVERIFY(model.splitAt(ids.at(0)));
    const QStringList line = nodeIds(model);
    QVERIFY(model.splitAt(line.at(2)));
    QCOMPARE(model.contours().size(), 2);
    const QStringList first = nodeIds(model, 0), second = nodeIds(model, 1);
    QCOMPARE(first.last(), line.at(2));
    QCOMPARE(anchorOf(model, second.first()), anchorOf(model, first.last()));
    QVERIFY(!model.join(first.at(1), second.last())); // an interior node
    QVERIFY(model.join(first.last(), second.first()));
    QCOMPARE(model.contours().size(), 1);
    QCOMPARE(nodeIds(model), line);
    QVERIFY(doc.undo());
    QCOMPARE(model.contours().size(), 2);
    QCOMPARE(nodeIds(model, 1), second);
}

void TstCanvasVectorEditing::idsFollowEditsMadeElsewhere()
{
    CanvasDocument doc;
    const CanvasObjectId id = doc.addPath({node(0, 0, 0, 0, 0, 0), node(100, 0, 100, 0, 100, 0),
                                           node(100, 100, 100, 100, 100, 100)}, true, QColor(Qt::red), {}, 0);
    CanvasPathEditModel model;
    model.setDocument(&doc);
    model.setObjectId(QString::number(id));
    const QStringList ids = nodeIds(model);
    model.setSelectedNodeIds({ids.at(2)});
    // A node moved by another editor keeps its id.
    QVERIFY(doc.movePathPart(id, 1, QStringLiteral("anchor"), 90, 10));
    QCOMPARE(nodeIds(model), ids);
    // A node inserted elsewhere gets a new id; the others and the selection stay.
    QVERIFY(doc.insertPathNode(id, 1, {{"x", 50}, {"y", -20}}));
    const QStringList after = nodeIds(model);
    QCOMPARE(after.size(), 4);
    QCOMPARE(after.at(0), ids.at(0));
    QCOMPARE(after.mid(2), ids.mid(1));
    QVERIFY(!ids.contains(after.at(1)));
    QCOMPARE(model.selectedNodeIds(), QStringList{ids.at(2)});
    // A removed node leaves the selection.
    QVERIFY(doc.removePathNode(id, 3));
    QCOMPARE(model.selectedNodeIds(), QStringList{});
}

void TstCanvasVectorEditing::miterJoinsStayInsideBounds()
{
    CanvasDocument doc;
    doc.setPageRect(QRectF(-20, -120, 260, 260));
    // A sharp spike: its full miter is ~20 half-pens long at the join.
    const CanvasObjectId id = doc.addPath({node(0, 0, 0, 0, 0, 0), node(100, 5, 100, 5, 100, 5),
                                           node(0, 10, 0, 10, 0, 10)}, false, {}, QColor(Qt::black), 10);
    QVERIFY(id);
    CanvasStyleModel styles;
    styles.setDocument(&doc);
    QVERIFY(styles.apply({QString::number(id)}, {{"miterLimit", 100}}));
    const auto outline = [&doc, id] {
        const CanvasObject &o = *doc.object(id);
        return QPainterPathStroker(CanvasPath::pen(o, 0)).createStroke(CanvasPath::document(o)).boundingRect();
    };
    const QRectF bounds = CanvasGeometry::documentBounds(*doc.object(id));
    QVERIFY2(bounds.adjusted(-1e-6, -1e-6, 1e-6, 1e-6).contains(outline()),
             qPrintable(QStringLiteral("bounds %1 outline right %2").arg(bounds.right()).arg(outline().right())));
    QVERIFY(outline().right() > 190);
    // A tile holding only the tip draws it: the index offers the object.
    const QImage tip = CanvasPainter::renderTile(doc, QRectF(140, -10, 20, 20), 20, {});
    QCOMPARE(tip.pixelColor(10, 15), QColor(Qt::black)); // document (150, 5)
    // Under the default limit of 4 the join bevels (SVG semantics): no tip.
    QVERIFY(styles.apply({QString::number(id)}, {{"miterLimit", 4}}));
    QVERIFY(outline().right() < 106);
    QVERIFY(CanvasPainter::renderTile(doc, QRectF(140, -10, 20, 20), 20, {}).pixelColor(10, 15) != QColor(Qt::black));
    // A rotated rectangle's corners stay inside its bounds as well.
    const CanvasObjectId box = doc.addRect(0, 0, 40, 40, {}, QColor(Qt::black), 8);
    QVERIFY(doc.setObjectTransform(box, {0.7071067811865476, 0.7071067811865476, -0.7071067811865476,
                                         0.7071067811865476, 150, 60}));
    const CanvasObject &rotated = *doc.object(box);
    const QRectF rotatedOutline = QPainterPathStroker(CanvasPath::pen(rotated, 0))
                                      .createStroke(rotated.transform.map([&] {
                                          QPainterPath p;
                                          p.addRect(rotated.rect);
                                          return p;
                                      }())).boundingRect();
    QVERIFY(CanvasGeometry::documentBounds(rotated).adjusted(-1e-6, -1e-6, 1e-6, 1e-6).contains(rotatedOutline));
}

QTEST_MAIN(TstCanvasVectorEditing)
#include "tst_canvas_vector_editing.moc"
