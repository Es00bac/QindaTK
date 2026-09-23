// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "canvas_geometry.h"
#include "canvas_object.h"

#include <QHash>
#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
#include <optional>

namespace QindaTK {

// One atomic document change: a list of object replacements. `before`
// absent = insert, `after` absent = remove. Tools build one of these at
// gesture end; nothing reaches the document before that.
struct CanvasEdit {
    struct Change {
        CanvasObjectId id = 0;
        std::optional<CanvasObject> before;
        std::optional<CanvasObject> after;
    };
    QString label;
    QVector<Change> changes;
    [[nodiscard]] bool isEmpty() const { return changes.isEmpty(); }
};

// AGENT-CONTRACT (docs/canvas.md#document-adapter): the document side of the
// canvas boundary. Views, hit testing, tools and renderers read documents
// only through this interface, and change them only through commit(). An
// application with its own scene model implements it; CanvasDocument is the
// in-memory reference implementation used by tests and examples.
//
// AGENT-NOTE: WP-033's shared session (DocumentSession + UndoService over a
// PropertyDocument) is not yet proven for spatial documents, so this seam
// owns its own bounded history. When WP-033 lands, a session adapter binds
// commit()/undo()/redo() to the shared services without changing callers.
class CanvasDocumentAdapter : public QObject {
    Q_OBJECT
    QML_ANONYMOUS
public:
    using QObject::QObject;
    [[nodiscard]] virtual quint64 revision() const = 0;
    [[nodiscard]] virtual QString documentId() const = 0;
    [[nodiscard]] virtual QRectF pageRect() const = 0;
    [[nodiscard]] virtual QRectF contentBounds() const = 0;
    // Paper colour behind the page rectangle (document data, not chrome).
    [[nodiscard]] virtual QColor pageColor() const { return QColor(Qt::white); }
    [[nodiscard]] virtual const CanvasObject *object(CanvasObjectId id) const = 0;
    [[nodiscard]] virtual const CanvasLayer *layer(CanvasLayerId id) const = 0;
    [[nodiscard]] virtual CanvasTiledImagePtr image(CanvasImageId id) const = 0;
    // Indexed candidates intersecting `region`, ascending id order.
    [[nodiscard]] virtual QVector<CanvasObjectId> candidates(const QRectF &region) const = 0;
    // Effective flags: an object is visible/locked when it AND its layer are.
    [[nodiscard]] virtual bool effectiveVisible(const CanvasObject &object) const;
    [[nodiscard]] virtual bool effectiveLocked(const CanvasObject &object) const;
    // Total paint order; strictly increasing along draw order.
    [[nodiscard]] virtual qint64 paintKey(const CanvasObject &object) const;
    // Where tools insert new objects, and fresh identity/paint order for them.
    [[nodiscard]] virtual CanvasLayerId activeLayer() const = 0;
    virtual CanvasObjectId reserveObjectId() = 0;
    virtual qint64 reserveZ() = 0;
    // Applies the whole edit or nothing; false when any change's `before`
    // no longer matches the document (a stale edit).
    virtual bool commit(const CanvasEdit &edit) = 0;

signals:
    // Document-space rectangles whose pixels changed, at `revision`.
    void damaged(const QVector<QRectF> &regions, quint64 revision);
    // Every cached result is invalid (load, clear, layer order change).
    void reset(quint64 revision);
    // Emitted after an edit is applied (any origin: commit, undo, redo).
    void edited(quint64 revision);
};

class CanvasDocument : public CanvasDocumentAdapter {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString documentId READ documentId CONSTANT)
    Q_PROPERTY(qulonglong revision READ revision NOTIFY changed)
    Q_PROPERTY(int objectCount READ objectCount NOTIFY changed)
    Q_PROPERTY(int layerCount READ layerCount NOTIFY changed)
    Q_PROPERTY(QRectF pageRect READ pageRect WRITE setPageRect NOTIFY changed)
    Q_PROPERTY(QRectF contentBounds READ contentBounds NOTIFY changed)
    Q_PROPERTY(QColor pageColor READ pageColor WRITE setPageColor NOTIFY changed)
    Q_PROPERTY(qulonglong activeLayer READ activeLayer WRITE setActiveLayer NOTIFY changed)
    Q_PROPERTY(QVariantList layers READ layerList NOTIFY changed)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY changed)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY changed)
    Q_PROPERTY(QString undoText READ undoText NOTIFY changed)
    Q_PROPERTY(int historyLimit READ historyLimit WRITE setHistoryLimit NOTIFY changed)

public:
    explicit CanvasDocument(QObject *parent = nullptr);

    quint64 revision() const override { return m_revision; }
    QString documentId() const override { return m_documentId; }
    QRectF pageRect() const override { return m_pageRect; }
    void setPageRect(const QRectF &rect);
    QColor pageColor() const override { return m_pageColor; }
    void setPageColor(const QColor &color);
    QRectF contentBounds() const override;
    const CanvasObject *object(CanvasObjectId id) const override;
    const CanvasLayer *layer(CanvasLayerId id) const override;
    CanvasTiledImagePtr image(CanvasImageId id) const override;
    QVector<CanvasObjectId> candidates(const QRectF &region) const override;
    qint64 paintKey(const CanvasObject &object) const override;
    bool commit(const CanvasEdit &edit) override;

    int objectCount() const { return int(m_objects.size()); }
    int layerCount() const { return int(m_layers.size()); }
    CanvasLayerId activeLayer() const override { return m_activeLayer; }
    CanvasObjectId reserveObjectId() override { return allocateId(); }
    qint64 reserveZ() override { return m_nextZ++; }
    void setActiveLayer(CanvasLayerId id);
    QVariantList layerList() const;
    const CanvasGeometry::SpatialIndex &spatialIndex() const { return m_index; }
    QVector<CanvasObjectId> degenerateObjects() const;
    QVector<CanvasObjectId> objectIds() const; // ascending

    bool canUndo() const { return m_cursor > 0; }
    bool canRedo() const { return m_cursor < m_history.size(); }
    QString undoText() const;
    int historyLimit() const { return m_historyLimit; }
    void setHistoryLimit(int limit);

    // C++ construction helpers. Each allocates a fresh id; `insert` commits a
    // one-change edit (undoable) unless `record` is false (fixtures).
    CanvasObjectId allocateId() { return m_nextId++; }
    CanvasObjectId insert(CanvasObject object, bool record = true);
    CanvasLayerId addLayer(const QString &name);
    CanvasImageId addImage(CanvasTiledImage image);
    bool setLayerVisible(CanvasLayerId id, bool visible);
    bool setLayerLocked(CanvasLayerId id, bool locked);

    // QML facade. Colours are document DATA, not chrome.
    Q_INVOKABLE qulonglong addLayerNamed(const QString &name) { return addLayer(name); }
    Q_INVOKABLE qulonglong addRect(qreal x, qreal y, qreal w, qreal h, const QColor &fill,
                                   const QColor &stroke = {}, qreal strokeWidth = 0);
    Q_INVOKABLE qulonglong addEllipse(qreal x, qreal y, qreal w, qreal h, const QColor &fill,
                                      const QColor &stroke = {}, qreal strokeWidth = 0);
    Q_INVOKABLE qulonglong addPolyline(const QVariantList &points, const QColor &stroke,
                                       qreal strokeWidth);
    // matrix = [m11, m12, m21, m22, dx, dy] (QTransform affine order)
    Q_INVOKABLE bool setObjectTransform(qulonglong id, const QVariantList &matrix);
    Q_INVOKABLE bool setObjectVisible(qulonglong id, bool visible);
    Q_INVOKABLE bool setObjectLocked(qulonglong id, bool locked);
    Q_INVOKABLE bool moveObjects(const QVariantList &ids, qreal dx, qreal dy);
    Q_INVOKABLE bool removeObject(qulonglong id);
    Q_INVOKABLE bool setLayerVisibleById(qulonglong id, bool visible) { return setLayerVisible(id, visible); }
    Q_INVOKABLE bool setLayerLockedById(qulonglong id, bool locked) { return setLayerLocked(id, locked); }
    Q_INVOKABLE QVariantMap objectInfo(qulonglong id) const;
    Q_INVOKABLE bool undo();
    Q_INVOKABLE bool redo();
    Q_INVOKABLE void clear();
    // Deterministic fixture: `count` shapes over a grid plus one tiled image.
    // Bypasses history (a fixture is a load, not an edit). Returns image id.
    Q_INVOKABLE qulonglong populateFixture(int count, int seed = 1, bool withImage = true);

signals:
    void changed();

private:
    void apply(const CanvasEdit &edit, bool forward, QVector<QRectF> *damage);
    void place(const CanvasObject &object, QVector<QRectF> *damage);
    void unplace(CanvasObjectId id, QVector<QRectF> *damage);
    void finishChange(const QVector<QRectF> &damage, bool full);
    void layerDamage(CanvasLayerId id, QVector<QRectF> *damage) const;
    std::optional<CanvasObject> copy(CanvasObjectId id) const;

    QString m_documentId;
    QHash<CanvasObjectId, CanvasObject> m_objects;
    QHash<CanvasLayerId, CanvasLayer> m_layers;
    QHash<CanvasImageId, CanvasTiledImagePtr> m_images;
    QHash<CanvasObjectId, QRectF> m_bounds;
    CanvasGeometry::SpatialIndex m_index;
    QVector<CanvasEdit> m_history;
    qsizetype m_cursor = 0;
    int m_historyLimit = 256;
    QRectF m_pageRect{0, 0, 1024, 768};
    QColor m_pageColor{Qt::white};
    quint64 m_revision = 1;
    CanvasObjectId m_nextId = 1;
    CanvasLayerId m_nextLayer = 1;
    CanvasImageId m_nextImage = 1;
    CanvasLayerId m_activeLayer = 0;
    qint64 m_nextZ = 1;
};

} // namespace QindaTK
