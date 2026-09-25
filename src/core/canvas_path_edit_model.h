// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "canvas_document.h"

#include <QPointer>
#include <functional>
#include <optional>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// One contour with the stable id of each node (parallel to contour.nodes).
struct CanvasPathEditContour {
    CanvasPathContour contour;
    QVector<quint64> ids;
    friend bool operator==(const CanvasPathEditContour &, const CanvasPathEditContour &) = default;
};

// AGENT-CONTRACT (docs/vector-authoring.md#path-editing, CAN-19): node-level
// editing of one path object for PathEditor. Every node of every contour has
// a stable id ("n<k>"); a segment's id is "s" + its start node's id. Ids
// survive this model's own edits (insert, delete, split, join, node type,
// moves), undo/redo of them and edits made elsewhere, so a selection held by
// id survives segment insertion. Ids are editor state, not document data: a
// reopened document numbers its nodes afresh. Each request is one undoable
// CanvasEdit validated like every other object edit; a refusal changes
// nothing and sets lastError. Node edits demote a parametric shape to
// freeform, as the document's node APIs do.
class CanvasPathEditModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QindaTK::CanvasDocumentAdapter *document READ document WRITE setDocument NOTIFY documentChanged)
    Q_PROPERTY(QString objectId READ objectId WRITE setObjectId NOTIFY objectIdChanged)
    // [{id, contour, index, x, y, inX, inY, outX, outY, type, endpoint, selected}]
    Q_PROPERTY(QVariantList nodes READ nodes NOTIFY nodesChanged)
    // [{id, contour, from, to}] with from/to node ids.
    Q_PROPERTY(QVariantList segments READ segments NOTIFY nodesChanged)
    // [{index, closed, nodeCount}]
    Q_PROPERTY(QVariantList contours READ contours NOTIFY nodesChanged)
    Q_PROPERTY(QStringList selectedNodeIds READ selectedNodeIds WRITE setSelectedNodeIds NOTIFY selectionChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)
public:
    explicit CanvasPathEditModel(QObject *parent = nullptr) : QObject(parent) {}
    CanvasDocumentAdapter *document() const { return m_document; }
    void setDocument(CanvasDocumentAdapter *document);
    QString objectId() const { return m_objectId; }
    void setObjectId(const QString &id);
    QVariantList nodes() const { return m_nodes; }
    QVariantList segments() const { return m_segments; }
    QVariantList contours() const { return m_contourList; }
    QStringList selectedNodeIds() const { return m_selected; }
    void setSelectedNodeIds(const QStringList &ids);
    QString lastError() const { return m_lastError; }

    Q_INVOKABLE bool selectNode(const QString &nodeId, bool extend = false);
    Q_INVOKABLE void clearSelection() { setSelectedNodeIds({}); }
    Q_INVOKABLE QVariantMap node(const QString &nodeId) const;
    // Splits the segment's cubic at t (0 < t < 1) without changing the curve;
    // returns the new node's id, or "" with lastError set.
    Q_INVOKABLE QString insertNode(const QString &segmentId, qreal t = 0.5);
    // Removes the nodes (the selection when empty) in one edit.
    Q_INVOKABLE bool deleteNodes(const QStringList &nodeIds = {});
    // "corner" retracts both handles; "smooth" aligns them on the tangent
    // through the neighbours keeping their lengths; "symmetric" also makes
    // their lengths equal. Applies to the nodes (the selection when empty).
    Q_INVOKABLE bool setNodeType(const QString &type, const QStringList &nodeIds = {});
    // Opens a closed contour at the node, or cuts an open contour in two
    // there; the node is duplicated and the copy gets a new id.
    Q_INVOKABLE bool splitAt(const QString &nodeId);
    // Joins two end nodes of open contours: the two ends of one contour close
    // it; ends of two contours become one contour. Coincident ends merge.
    Q_INVOKABLE bool join(const QString &firstNodeId, const QString &secondNodeId);
    // part "anchor" (handles follow), "in" or "out", in local coordinates.
    Q_INVOKABLE bool moveNodePart(const QString &nodeId, const QString &part, qreal x, qreal y);

    // The editable contours with ids (tests and C++ hosts).
    const QVector<CanvasPathEditContour> &editContours() const { return m_path; }

signals:
    void documentChanged();
    void objectIdChanged();
    void nodesChanged();
    void selectionChanged();
    void errorChanged();

private:
    struct Locator { int contour = -1; int index = -1; };
    [[nodiscard]] const CanvasObject *target() const;
    [[nodiscard]] Locator locate(const QString &nodeId) const;
    [[nodiscard]] QStringList requested(const QStringList &nodeIds) const;
    bool commitPath(QVector<CanvasPathEditContour> path, const QString &label);
    bool fail(const QString &message);
    bool succeed();
    quint64 nextId() { return ++m_lastId; }
    void reconcile();
    void publish();

    QPointer<CanvasDocumentAdapter> m_document;
    QString m_objectId;
    QVector<CanvasPathEditContour> m_path;
    // Contours with ids this model has seen, newest last: undo and redo
    // restore geometry exactly, so they find their ids here (bounded).
    QVector<QVector<CanvasPathEditContour>> m_memory;
    std::optional<QVector<CanvasPathEditContour>> m_pending;
    QStringList m_selected;
    QVariantList m_nodes;
    QVariantList m_segments;
    QVariantList m_contourList;
    QString m_lastError;
    quint64 m_lastId = 0;
};

namespace CanvasPathEditing {
// The pure operations behind the model; each returns false (with `error`)
// and leaves `path` alone when the request does not apply.
[[nodiscard]] QString nodeType(const CanvasPathContour &contour, int index);
bool insert(QVector<CanvasPathEditContour> &path, int contour, int index, qreal t, quint64 newId,
            QString *error);
bool setType(QVector<CanvasPathEditContour> &path, int contour, int index, const QString &type,
             QString *error);
bool split(QVector<CanvasPathEditContour> &path, int contour, int index, quint64 newId, QString *error);
bool join(QVector<CanvasPathEditContour> &path, int contourA, int indexA, int contourB, int indexB,
          QString *error);
// Ids for `next` from the ids of `previous`: nodes whose anchors match in
// order keep their ids; an equal-length run of changed nodes between matches
// keeps its ids by position (a moved node); anything else takes a new id.
QVector<CanvasPathEditContour> carryIds(const QVector<CanvasPathEditContour> &previous,
                                        const QVector<CanvasPathContour> &next,
                                        const std::function<quint64()> &newId);
} // namespace CanvasPathEditing

} // namespace QindaTK
