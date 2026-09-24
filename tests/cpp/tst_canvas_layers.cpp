// SPDX-License-Identifier: LGPL-3.0-or-later
// WP-042 layer hierarchy: groups and reorder with parent, cycle and depth
// checks, one undo step per structural change, rendered stacking, hidden and
// locked groups, nested transforms, many layers through save/reopen, and the
// layer tree model sharing selection without feedback.
#include "canvas_document.h"
#include "canvas_document_format.h"
#include "canvas_layer_tree_model.h"
#include "canvas_render.h"
#include "canvas_selection_model.h"
#include "hit_test_service.h"
#include "tool_controller.h"

#include <QSignalSpy>
#include <QtTest>
#include <functional>

using namespace QindaTK;

namespace {
CanvasObjectId square(CanvasDocument &document, CanvasLayerId layer, const QRectF &rect,
                      const QColor &fill)
{
    CanvasObject object;
    object.shape = CanvasShape::Rect;
    object.layer = layer;
    object.rect = QRectF(QPointF(0, 0), rect.size());
    object.transform = QTransform::fromTranslate(rect.x(), rect.y());
    object.fill = fill;
    return document.insert(object, false);
}

QRgb pixel(const CanvasDocument &document, int x, int y)
{
    return CanvasPainter::renderTile(document, QRectF(0, 0, 64, 64), 64, {}).pixel(x, y);
}

QString key(const char *kind, quint64 id)
{
    return QStringLiteral("%1:%2").arg(QLatin1String(kind)).arg(id);
}

using Ids = QVector<CanvasLayerId>;
} // namespace

class TstCanvasLayers final : public QObject {
    Q_OBJECT
private slots:
    void groupsReorderAndUndoAsSingleSteps();
    void hierarchyRejectsInvalidParents();
    void groupUngroupAndDeleteTrees();
    void hiddenAndLockedGroupsCascade();
    void nestedTransformsSurviveHierarchyEdits();
    void manyLayersRoundTrip();
    void invalidLayerTreesAreRejectedOnOpen();
    void treeModelSharesSelectionWithoutFeedback();
};

void TstCanvasLayers::groupsReorderAndUndoAsSingleSteps()
{
    CanvasDocument doc;
    const CanvasLayerId base = doc.activeLayer();
    const CanvasObjectId red = square(doc, base, QRectF(16, 16, 32, 32), Qt::red);
    const CanvasLayerId top = doc.addLayer(QStringLiteral("Top"));
    const CanvasObjectId blue = square(doc, top, QRectF(16, 16, 32, 32), Qt::blue);
    QCOMPARE(pixel(doc, 32, 32), qRgb(0, 0, 255)); // Top paints last

    const CanvasLayerId group = doc.addLayerGroup(QStringLiteral("Group"));
    QVERIFY(group != 0);
    QCOMPARE(doc.undoText(), QStringLiteral("Add layer group"));
    QCOMPARE(doc.childLayers(0), (Ids{base, top, group}));
    QVERIFY(doc.layer(group)->group);

    const quint64 revision = doc.revision();
    QVERIFY(doc.moveLayer(base, group, 0));
    QCOMPARE(doc.revision(), revision + 1);
    QCOMPARE(doc.undoText(), QStringLiteral("Move layer"));
    QCOMPARE(doc.childLayers(0), (Ids{top, group}));
    QCOMPARE(doc.childLayers(group), (Ids{base}));
    QCOMPARE(doc.layerDepth(base), 1);
    QCOMPARE(doc.layerRank(top), 0);
    QCOMPARE(doc.layerRank(group), 1);
    QCOMPARE(doc.layerRank(base), 2);
    QVERIFY(doc.paintKey(*doc.object(red)) > doc.paintKey(*doc.object(blue)));
    QCOMPARE(pixel(doc, 32, 32), qRgb(255, 0, 0)); // the group now paints above Top
    const auto hits = HitTestService::hitPoint(doc, QPointF(32, 32), 0.5);
    QCOMPARE(hits.size(), 2);
    QCOMPARE(hits.constFirst().object, red);

    QVERIFY(doc.undo());
    QCOMPARE(doc.childLayers(0), (Ids{base, top, group}));
    QCOMPARE(doc.layer(base)->parent, CanvasLayerId(0));
    QCOMPARE(pixel(doc, 32, 32), qRgb(0, 0, 255));
    QVERIFY(doc.redo());
    QCOMPARE(doc.childLayers(group), (Ids{base}));
    QCOMPARE(pixel(doc, 32, 32), qRgb(255, 0, 0));

    // Reorder at the top level: Top above the group again, then a no-op.
    QVERIFY(doc.moveLayer(top, 0, 1));
    QCOMPARE(doc.childLayers(0), (Ids{group, top}));
    QCOMPARE(pixel(doc, 32, 32), qRgb(0, 0, 255));
    const quint64 moved = doc.revision();
    QVERIFY(!doc.moveLayer(top, 0, 1));
    QVERIFY(doc.lastError().isEmpty());
    QCOMPARE(doc.revision(), moved);
    QVERIFY(doc.undo());
    QCOMPARE(doc.childLayers(0), (Ids{top, group}));
}

void TstCanvasLayers::hierarchyRejectsInvalidParents()
{
    CanvasDocumentLimits limits;
    limits.maximumLayerDepth = 3;
    CanvasDocument doc;
    doc.setLimits(limits);
    const CanvasLayerId base = doc.activeLayer();
    const CanvasObjectId shape = square(doc, base, QRectF(0, 0, 8, 8), Qt::red);
    const CanvasLayerId outer = doc.addLayerGroup(QStringLiteral("Outer"));
    const CanvasLayerId middle = doc.addLayerAt(QStringLiteral("Middle"), true, outer, 0);
    const CanvasLayerId inner = doc.addLayerAt(QStringLiteral("Inner"), true, middle, 0);
    QVERIFY(outer && middle && inner);
    QCOMPARE(doc.layerDepth(inner), 2);
    const quint64 revision = doc.revision();

    // Depth: a fourth level exceeds maximumLayerDepth = 3.
    QCOMPARE(doc.addLayerAt(QStringLiteral("Too deep"), false, inner, 0), CanvasLayerId(0));
    QVERIFY(!doc.lastError().isEmpty());
    // Cycles: a group into itself or into its own descendant.
    QVERIFY(!doc.moveLayer(outer, outer, 0));
    QVERIFY(!doc.moveLayer(outer, inner, 0));
    // Parents must be groups, and groups never hold objects.
    QVERIFY(!doc.moveLayer(outer, base, 0));
    QVERIFY(!doc.moveObjectToLayer(shape, outer, 0));
    CanvasObject stray;
    stray.shape = CanvasShape::Rect;
    stray.rect = QRectF(0, 0, 4, 4);
    stray.fill = Qt::green;
    stray.layer = middle;
    QCOMPARE(doc.insert(stray), CanvasObjectId(0));

    // The commit check is the backstop for raw edits as well.
    CanvasLayer cycle = *doc.layer(outer);
    cycle.parent = inner;
    CanvasEdit loop;
    loop.label = QStringLiteral("Loop");
    loop.layers.append({outer, *doc.layer(outer), cycle});
    OperationError error = doc.commitEdit(loop);
    QCOMPARE(error.code, ErrorCode::Validation);
    QCOMPARE(error.fieldPath, QStringLiteral("layers.parent"));
    CanvasLayer filled = *doc.layer(base);
    filled.group = true;
    CanvasEdit content;
    content.label = QStringLiteral("Group with objects");
    content.layers.append({base, *doc.layer(base), filled});
    error = doc.commitEdit(content);
    QCOMPARE(error.code, ErrorCode::Validation);
    QCOMPARE(error.fieldPath, QStringLiteral("layers.group"));
    QCOMPARE(doc.revision(), revision); // every refusal left the document as it was
    QCOMPARE(doc.layerDepth(inner), 2);
    QCOMPARE(doc.object(shape)->layer, base);
}

void TstCanvasLayers::groupUngroupAndDeleteTrees()
{
    CanvasDocument doc;
    const CanvasLayerId base = doc.activeLayer();
    const CanvasLayerId top = doc.addLayer(QStringLiteral("Top"));
    const CanvasObjectId red = square(doc, base, QRectF(0, 0, 8, 8), Qt::red);
    const CanvasObjectId blue = square(doc, top, QRectF(4, 4, 8, 8), Qt::blue);

    const CanvasLayerId wrapper = doc.groupLayers({base, top}, QStringLiteral("Wrapper"));
    QVERIFY(wrapper != 0);
    QCOMPARE(doc.undoText(), QStringLiteral("Group layers"));
    QCOMPARE(doc.childLayers(0), (Ids{wrapper}));
    QCOMPARE(doc.childLayers(wrapper), (Ids{base, top}));
    QVERIFY(doc.ungroupLayer(wrapper));
    QCOMPARE(doc.undoText(), QStringLiteral("Ungroup layers"));
    QCOMPARE(doc.childLayers(0), (Ids{base, top}));
    QVERIFY(!doc.layer(wrapper));
    QVERIFY(doc.undo());
    QCOMPARE(doc.childLayers(wrapper), (Ids{base, top}));
    QVERIFY(!doc.ungroupLayer(base)); // a content layer is not a group

    // A locked nested layer blocks deleting its group; so does losing the
    // last layer. Otherwise the whole tree and its objects go in one step.
    QVERIFY(doc.setLayerLocked(top, true));
    QVERIFY(!doc.removeLayerTree(wrapper));
    QVERIFY(doc.object(blue));
    QVERIFY(doc.setLayerLocked(top, false));
    QVERIFY(!doc.removeLayerTree(wrapper));
    const CanvasLayerId spare = doc.addLayer(QStringLiteral("Spare"));
    QVERIFY(doc.removeLayerTree(wrapper));
    QCOMPARE(doc.undoText(), QStringLiteral("Delete layer group"));
    QVERIFY(!doc.object(red) && !doc.object(blue));
    QCOMPARE(doc.childLayers(0), (Ids{spare}));
    QCOMPARE(doc.activeLayer(), spare);
    QVERIFY(doc.undo());
    QVERIFY(doc.object(red) && doc.object(blue));
    QCOMPARE(doc.childLayers(wrapper), (Ids{base, top}));
    QCOMPARE(doc.object(red)->layer, base);
}

void TstCanvasLayers::hiddenAndLockedGroupsCascade()
{
    CanvasDocument doc;
    const CanvasLayerId base = doc.activeLayer();
    const CanvasObjectId red = square(doc, base, QRectF(16, 16, 32, 32), Qt::red);
    const CanvasLayerId group = doc.groupLayers({base}, QStringLiteral("Group"));
    QVERIFY(group != 0);
    CanvasSelectionModel selection;
    selection.setDocument(&doc);
    ToolController tools;
    tools.setDocument(&doc);
    tools.setSelectionModel(&selection);
    tools.setSelection({red});
    QCOMPARE(selection.selection(), (QVector<CanvasObjectId>{red}));

    QVERIFY(doc.setLayerVisible(group, false));
    QVERIFY(!doc.layerEffectivelyVisible(base));
    QVERIFY(doc.layer(base)->visible); // the layer's own flag is unchanged
    QVERIFY(!doc.effectiveVisible(*doc.object(red)));
    QCOMPARE(pixel(doc, 32, 32), qRgb(255, 255, 255)); // hidden with its group
    QVERIFY(HitTestService::hitPoint(doc, QPointF(32, 32), 0.5).isEmpty());
    QVERIFY(selection.selection().isEmpty()); // pruned like any hidden object
    tools.setActiveTool(QStringLiteral("rectangle"));
    QVERIFY(!tools.begin(QStringLiteral("v"), 0, 0));
    QCOMPARE(tools.lastOutcome(), QStringLiteral("declined:layer-hidden"));

    QVERIFY(doc.undo());
    QCOMPARE(pixel(doc, 32, 32), qRgb(255, 0, 0));
    QVERIFY(doc.setLayerLocked(group, true));
    QVERIFY(doc.layerEffectivelyLocked(base));
    QVERIFY(doc.effectiveLocked(*doc.object(red)));
    QVERIFY(!tools.begin(QStringLiteral("v"), 0, 0));
    QCOMPARE(tools.lastOutcome(), QStringLiteral("declined:layer-locked"));
    QVERIFY(!doc.moveObjectToLayer(red, base, 0)); // locked objects keep their place
    QVERIFY(HitTestService::hitPoint(doc, QPointF(32, 32), 0.5).isEmpty()); // skip policy
    CanvasHitPolicy report;
    report.locked = CanvasLockPolicy::Report;
    const auto hits = HitTestService::hitPoint(doc, QPointF(32, 32), 0.5, report);
    QCOMPARE(hits.size(), 1);
    QVERIFY(hits.constFirst().locked);
}

void TstCanvasLayers::nestedTransformsSurviveHierarchyEdits()
{
    CanvasDocument doc;
    const CanvasLayerId base = doc.activeLayer();
    CanvasObject turned;
    turned.shape = CanvasShape::Rect;
    turned.layer = base;
    turned.rect = QRectF(0, 0, 20, 10);
    turned.transform = QTransform().rotate(30) * QTransform::fromScale(2, 1.5)
        * QTransform::fromTranslate(40, 25);
    turned.fill = Qt::red;
    const CanvasObjectId id = doc.insert(turned, false);
    QVERIFY(id != 0);
    const CanvasObject before = *doc.object(id);
    const QRectF bounds = doc.objectInfo(id).value(QStringLiteral("bounds")).toRectF();

    const CanvasLayerId outer = doc.groupLayers({base}, QStringLiteral("Outer"));
    const CanvasLayerId inner = doc.groupLayers({base}, QStringLiteral("Inner"));
    QCOMPARE(doc.layer(inner)->parent, outer);
    QCOMPARE(doc.layerDepth(base), 2);
    const CanvasLayerId other = doc.addLayer(QStringLiteral("Other"));
    QVERIFY(doc.moveObjectToLayer(id, other, 0));
    QCOMPARE(doc.undoText(), QStringLiteral("Move object to layer"));
    QVERIFY(doc.moveLayer(other, inner, 1));
    QCOMPARE(doc.childLayers(inner), (Ids{base, other}));
    QVERIFY(doc.ungroupLayer(outer));
    QCOMPARE(doc.childLayers(0), (Ids{inner}));

    // Hierarchy edits change only the object's layer and paint order.
    const CanvasObject after = *doc.object(id);
    QCOMPARE(after.layer, other);
    QCOMPARE(after.transform, before.transform);
    QCOMPARE(after.rect, before.rect);
    QCOMPARE(doc.objectInfo(id).value(QStringLiteral("bounds")).toRectF(), bounds);
    for (int step = 0; step < 6; ++step) QVERIFY(doc.undo());
    QCOMPARE(*doc.object(id), before);
    QCOMPARE(doc.childLayers(0), (Ids{base}));
    for (int step = 0; step < 6; ++step) QVERIFY(doc.redo());
    QCOMPARE(doc.object(id)->layer, other);
    QCOMPARE(doc.object(id)->transform, before.transform);
    QCOMPARE(doc.childLayers(inner), (Ids{base, other}));
}

void TstCanvasLayers::manyLayersRoundTrip()
{
    CanvasDocument doc;
    Ids groups;
    for (int g = 0; g < 16; ++g) {
        const CanvasLayerId group = doc.addLayerGroup(QStringLiteral("Group %1").arg(g));
        QVERIFY(group != 0);
        groups.append(group);
        for (int l = 0; l < 12; ++l) {
            const CanvasLayerId layer = doc.addLayerAt(QStringLiteral("Layer %1.%2").arg(g).arg(l),
                                                       false, group, l);
            QVERIFY(layer != 0);
            QVERIFY(square(doc, layer, QRectF(l, g, 1, 1), QColor::fromRgb(g * 10, l * 20, 100)) != 0);
        }
    }
    QCOMPARE(doc.layerCount(), 1 + 16 + 16 * 12);

    // Ranks are the depth-first flattening of the tree, bottom to top.
    const auto flatten = [](const CanvasDocument &document) {
        Ids flat;
        std::function<void(CanvasLayerId)> walk = [&](CanvasLayerId parent) {
            for (const CanvasLayerId id : document.childLayers(parent)) {
                flat.append(id);
                walk(id);
            }
        };
        walk(0);
        return flat;
    };
    const Ids flattened = flatten(doc);
    QCOMPARE(flattened.size(), qsizetype(doc.layerCount()));
    for (int i = 0; i < flattened.size(); ++i) QCOMPARE(doc.layerRank(flattened.at(i)), i);

    // Moving a whole group to the bottom is one step; undo restores every rank.
    QVERIFY(doc.moveLayer(groups.last(), 0, 0));
    QCOMPARE(doc.childLayers(0).constFirst(), groups.last());
    const CanvasLayerId firstNested = doc.childLayers(groups.last()).constFirst();
    QCOMPARE(doc.layerRank(firstNested), 1);
    QVERIFY(doc.undo());
    QCOMPARE(flatten(doc), flattened);

    OperationError error;
    const QByteArray bytes = CanvasDocumentFormat::encode(doc.state(), &error);
    QVERIFY2(!bytes.isEmpty(), qPrintable(error.message));
    const auto decoded = CanvasDocumentFormat::decode(bytes, {}, &error);
    QVERIFY2(decoded.has_value(), qPrintable(error.message));
    const auto reopened = CanvasDocument::fromState(*decoded);
    QCOMPARE(flatten(*reopened), flattened);
    for (const CanvasLayerId id : flattened) {
        QVERIFY(*reopened->layer(id) == *doc.layer(id));
        QCOMPARE(reopened->layerRank(id), doc.layerRank(id));
    }
    QCOMPARE(CanvasDocumentFormat::encode(reopened->state()), bytes); // equal content, equal bytes

    // The tree model lists front-most rows first over the same hierarchy.
    CanvasLayerTreeModel tree;
    tree.setDocument(reopened.get());
    QCOMPARE(tree.nodeCount(), reopened->layerCount() + 16 * 12);
    QCOMPARE(tree.rowCount(), 17);
    const QModelIndex front = tree.index(0, 0);
    QCOMPARE(front.data(CanvasLayerTreeModel::KeyRole).toString(), key("layer", groups.last()));
    QCOMPARE(tree.rowCount(front), 12);
    QCOMPARE(tree.index(0, 0, front).data(CanvasLayerTreeModel::KeyRole).toString(),
             key("layer", reopened->childLayers(groups.last()).constLast()));

    // A flat document saves no layer-tree or artboard record at all.
    CanvasDocument flat;
    QVERIFY(flat.addLayer(QStringLiteral("Second")) != 0);
    const QJsonObject saved = CanvasDocumentFormat::savedExtensions(flat.state());
    QVERIFY(!saved.contains(QLatin1String(CanvasDocumentFormat::LayerTreeExtensionKey)));
    QVERIFY(!saved.contains(QLatin1String(CanvasDocumentFormat::ArtboardExtensionKey)));
}

void TstCanvasLayers::invalidLayerTreesAreRejectedOnOpen()
{
    CanvasDocument doc;
    const CanvasLayerId base = doc.activeLayer();
    QVERIFY(square(doc, base, QRectF(0, 0, 4, 4), Qt::red) != 0);
    const CanvasLayerId group = doc.addLayerGroup(QStringLiteral("Group"));
    const CanvasLayerId child = doc.addLayerAt(QStringLiteral("Child"), false, group, 0);
    QVERIFY(group && child);
    const CanvasDocumentState good = doc.state();
    const auto open = [](const CanvasDocumentState &state) {
        OperationError error;
        const QByteArray bytes = CanvasDocumentFormat::encode(state, &error);
        if (bytes.isEmpty()) return QStringLiteral("encode failed");
        return CanvasDocumentFormat::decode(bytes, {}, &error) ? QString() : error.fieldPath;
    };
    QCOMPARE(open(good), QString());

    CanvasDocumentState contentParent = good;
    contentParent.layers[child].parent = base; // parents must be groups
    QCOMPARE(open(contentParent), QStringLiteral("layers.parent"));
    CanvasDocumentState missingParent = good;
    missingParent.layers[child].parent = 999;
    QCOMPARE(open(missingParent), QStringLiteral("layers.parent"));
    CanvasDocumentState cycle = good;
    cycle.layers[child].group = true;
    cycle.layers[group].parent = child;
    QCOMPARE(open(cycle), QStringLiteral("layers.parent"));
    CanvasDocumentState groupWithObjects = good;
    groupWithObjects.layers[base].group = true;
    QCOMPARE(open(groupWithObjects), QStringLiteral("objects.layer"));
}

void TstCanvasLayers::treeModelSharesSelectionWithoutFeedback()
{
    CanvasDocument doc;
    const CanvasLayerId base = doc.activeLayer();
    const CanvasObjectId red = square(doc, base, QRectF(0, 0, 8, 8), Qt::red);
    const CanvasObjectId hidden = square(doc, base, QRectF(10, 0, 8, 8), Qt::green);
    QVERIFY(doc.setObjectVisible(hidden, false));
    const CanvasLayerId top = doc.addLayer(QStringLiteral("Top"));
    const CanvasObjectId blue = square(doc, top, QRectF(4, 4, 8, 8), Qt::blue);
    const CanvasLayerId group = doc.groupLayers({base}, QStringLiteral("Group"));
    QVERIFY(group != 0);

    CanvasSelectionModel selection;
    selection.setDocument(&doc);
    ToolController tools;
    tools.setDocument(&doc);
    tools.setSelectionModel(&selection);
    CanvasLayerTreeModel tree;
    tree.setDocument(&doc);
    tree.setSelectionModel(&selection);
    QSignalSpy reveal(&tree, &CanvasLayerTreeModel::revealRequested);

    // Rows list the front-most first; groups hold layers, layers hold objects.
    QCOMPARE(tree.rowCount(), 2);
    QCOMPARE(tree.index(0, 0).data(CanvasLayerTreeModel::KeyRole).toString(), key("layer", top));
    const QModelIndex groupRow = tree.index(1, 0);
    QCOMPARE(groupRow.data(CanvasLayerTreeModel::KindRole).toString(), QStringLiteral("group"));
    QCOMPARE(tree.rowCount(groupRow), 1);
    const QModelIndex baseRow = tree.index(0, 0, groupRow);
    QCOMPARE(tree.parent(baseRow), groupRow);
    QCOMPARE(tree.rowCount(baseRow), 2);
    QCOMPARE(tree.index(0, 0, baseRow).data(CanvasLayerTreeModel::KeyRole).toString(),
             key("object", hidden));
    QCOMPARE(tree.ancestorKeys(key("object", red)),
             (QStringList{key("layer", group), key("layer", base)}));

    // Tree clicks write the shared selection and never ask to reveal themselves.
    QVERIFY(tree.select(key("object", red)));
    QCOMPARE(selection.selection(), (QVector<CanvasObjectId>{red}));
    QCOMPARE(tools.selection(), (QVector<CanvasObjectId>{red}));
    QCOMPARE(doc.activeLayer(), base);
    QVERIFY(tree.nodeInfo(key("object", red)).value(QStringLiteral("selected")).toBool());
    QVERIFY(tree.select(key("object", blue), true));
    QCOMPARE(selection.selection(), (QVector<CanvasObjectId>{red, blue}));
    QCOMPARE(doc.activeLayer(), top);
    QVERIFY(!tree.select(key("object", hidden))); // hidden objects are not selectable
    QVERIFY(!tree.lastError().isEmpty());
    QCOMPARE(reveal.count(), 0);

    // A canvas-side selection asks the tree to reveal the new row, once.
    tools.setSelection({});
    tools.setSelection({red});
    QCOMPARE(reveal.count(), 1);
    QCOMPARE(reveal.constFirst().constFirst().toString(), key("object", red));
    QCOMPARE(selection.selection(), (QVector<CanvasObjectId>{red}));
    QCOMPARE(tree.selectedKeys(), (QStringList{key("object", red)}));

    // Row requests are document commands with one undo step each.
    QVERIFY(tree.setShown(key("layer", group), false));
    QCOMPARE(doc.undoText(), QStringLiteral("Hide layer"));
    QVERIFY(!tree.nodeInfo(key("object", red)).value(QStringLiteral("effectiveVisible")).toBool());
    QVERIFY(doc.undo());
    QVERIFY(tree.rename(key("layer", top), QStringLiteral("Front")));
    QCOMPARE(doc.layer(top)->name, QStringLiteral("Front"));
    QCOMPARE(doc.undoText(), QStringLiteral("Rename layer"));
    QVERIFY(tree.canMove(key("object", red), key("layer", top), QStringLiteral("inside")));
    QVERIFY(!tree.canMove(key("layer", group), key("object", blue), QStringLiteral("above")));
    QVERIFY(tree.move(key("object", red), key("object", blue), QStringLiteral("above")));
    QCOMPARE(doc.object(red)->layer, top);
    QCOMPARE(doc.objectsOnLayer(top), (QVector<CanvasObjectId>{blue, red}));
    QCOMPARE(doc.undoText(), QStringLiteral("Move object to layer"));
    QVERIFY(doc.undo());
    QCOMPARE(doc.object(red)->layer, base);

    // Step, indent and outdent move rows among siblings and groups.
    QVERIFY(tree.step(key("layer", top), 1)); // one row down: behind the group
    QCOMPARE(doc.childLayers(0), (Ids{top, group}));
    QVERIFY(tree.indent(key("layer", top))); // into the group shown above it
    QCOMPARE(doc.childLayers(group), (Ids{top, base}));
    QVERIFY(tree.outdent(key("layer", top)));
    QCOMPARE(doc.childLayers(0), (Ids{group, top}));
    QVERIFY(!tree.canMove(key("layer", group), key("layer", group), QStringLiteral("inside")));
    const QString added = tree.addLayer(QStringLiteral("Added"), key("layer", base));
    QVERIFY(!added.isEmpty());
    QCOMPARE(doc.childLayers(group).constLast(), added.section(QLatin1Char(':'), 1).toULongLong());
    QCOMPARE(tree.activeKey(), added);
}

QTEST_MAIN(TstCanvasLayers)
#include "tst_canvas_layers.moc"
