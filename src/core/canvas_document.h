// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "canvas_document_state.h"
#include "canvas_geometry.h"
#include "canvas_object.h"
#include "document_model.h"

#include <QHash>
#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QQmlParserStatus>
#include <QtQml/qqmlregistration.h>
#include <memory>
#include <optional>

namespace QindaTK {
class UndoService;

// One atomic document change. Object `changes` are replacements by id:
// `before` absent = insert, `after` absent = remove. Layer, image, page and
// guide deltas follow the same before/after rule (a null image pointer = absent).
// Tools build one of these at gesture end; nothing reaches the document
// before that. The applied edit is also the recorded undo step, so history
// holds touched-object deltas and shared image pointers, never a scene copy.
struct CanvasEdit {
    struct Change {
        CanvasObjectId id = 0;
        std::optional<CanvasObject> before;
        std::optional<CanvasObject> after;
    };
    struct LayerChange {
        CanvasLayerId id = 0;
        std::optional<CanvasLayer> before;
        std::optional<CanvasLayer> after;
    };
    struct ImageChange {
        CanvasImageId id = 0;
        CanvasTiledImagePtr before;
        CanvasTiledImagePtr after;
    };
    struct PageChange {
        QRectF beforeRect;
        QRectF afterRect;
        QColor beforeColor;
        QColor afterColor;
    };
    // The whole ordered guide list before and after (guides are few).
    struct GuideChange {
        QVector<CanvasGuide> before;
        QVector<CanvasGuide> after;
    };
    // The whole ordered artboard list before and after (boards are few).
    struct ArtboardChange {
        QVector<CanvasArtboard> before;
        QVector<CanvasArtboard> after;
    };
    QString label;
    QVector<Change> changes;
    QVector<LayerChange> layers = {};
    QVector<ImageChange> images = {};
    std::optional<PageChange> page = {};
    // The reset generation the edit was built against, when the builder
    // knows it; a different generation rejects the edit as stale.
    std::optional<quint64> baseResetGeneration = {};
    std::optional<GuideChange> guides = {};
    std::optional<ArtboardChange> artboards = {};
    [[nodiscard]] bool isEmpty() const
    {
        return changes.isEmpty() && layers.isEmpty() && images.isEmpty() && !page && !guides
            && !artboards;
    }
};

// A document-space fill painted below every object (an artboard background).
struct CanvasBackdrop {
    QRectF rect;
    QColor color;
};

// AGENT-CONTRACT (docs/canvas.md#document-adapter): the document side of the
// canvas boundary. Views, hit testing, tools and renderers read documents
// only through this interface, and change them only through commit(). An
// application with its own scene model implements it; CanvasDocument is the
// in-memory reference implementation used by tests and examples.
//
// AGENT-NOTE: CanvasDocument records commit/undo/redo through UndoService
// (docs/document-models.md), the same engine DocumentSession shares; an
// adapter with its own model does the same by implementing DocumentModel.
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
    [[nodiscard]] virtual QByteArray outputProfileIcc() const { return {}; }
    [[nodiscard]] virtual const CanvasObject *object(CanvasObjectId id) const = 0;
    [[nodiscard]] virtual const CanvasLayer *layer(CanvasLayerId id) const = 0;
    [[nodiscard]] virtual CanvasTiledImagePtr image(CanvasImageId id) const = 0;
    // Indexed candidates intersecting `region`, ascending id order.
    [[nodiscard]] virtual QVector<CanvasObjectId> candidates(const QRectF &region) const = 0;
    // Effective flags: an object is visible/locked when it AND its layer are.
    [[nodiscard]] virtual bool effectiveVisible(const CanvasObject &object) const;
    [[nodiscard]] virtual bool effectiveLocked(const CanvasObject &object) const;
    // A layer is effectively visible/locked when it and every ancestor group
    // are (a bounded walk over CanvasLayer::parent); a missing layer is
    // visible and unlocked.
    [[nodiscard]] virtual bool layerEffectivelyVisible(CanvasLayerId id) const;
    [[nodiscard]] virtual bool layerEffectivelyLocked(CanvasLayerId id) const;
    // Total paint order; strictly increasing along draw order. An adapter
    // with layer groups overrides it with the flattened layer rank.
    [[nodiscard]] virtual qint64 paintKey(const CanvasObject &object) const;
    // Fills painted after the page colour and below every object, in order.
    [[nodiscard]] virtual QVector<CanvasBackdrop> backdrops() const { return {}; }
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

// The reference spatial document. It is a DocumentModel: every commit is
// validated whole (prepare) before anything changes, then applied and
// recorded as one step (publish). Standalone it keeps a private
// UndoService; a DocumentSession moves that single history into the shared
// service. Page, layer and image changes are recorded steps too, and every
// content change bumps the revision.
class CanvasDocument : public CanvasDocumentAdapter, public DocumentModel, public QQmlParserStatus {
    Q_OBJECT
    QML_ELEMENT
    Q_INTERFACES(QQmlParserStatus)
    Q_MOC_INCLUDE("undo_service.h")
    Q_PROPERTY(QString documentId READ documentId CONSTANT)
    Q_PROPERTY(qulonglong revision READ revision NOTIFY changed)
    Q_PROPERTY(QString resetGeneration READ resetGenerationString NOTIFY changed)
    Q_PROPERTY(int objectCount READ objectCount NOTIFY changed)
    Q_PROPERTY(int layerCount READ layerCount NOTIFY changed)
    Q_PROPERTY(int imageCount READ imageCount NOTIFY changed)
    Q_PROPERTY(QRectF pageRect READ pageRect WRITE setPageRect NOTIFY changed)
    Q_PROPERTY(QRectF contentBounds READ contentBounds NOTIFY changed)
    Q_PROPERTY(QColor pageColor READ pageColor WRITE setPageColor NOTIFY changed)
    Q_PROPERTY(QByteArray outputProfileIcc READ outputProfileIcc NOTIFY changed)
    Q_PROPERTY(qulonglong activeLayer READ activeLayer WRITE setActiveLayer NOTIFY changed)
    Q_PROPERTY(QVariantList layers READ layerList NOTIFY changed)
    // {id, orientation: "vertical"|"horizontal", position}: the record shape
    // GuideLayer.guideData and SnapModel.guides read.
    Q_PROPERTY(QVariantList guides READ guideList NOTIFY changed)
    // {id, name, rect, background, bleed, exportRect} in board order: the
    // record shape ArtboardView.boardData reads (docs/canvas.md#artboards).
    Q_PROPERTY(QVariantList artboards READ artboardList NOTIFY changed)
    Q_PROPERTY(int artboardCount READ artboardCount NOTIFY changed)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
    Q_PROPERTY(QString undoText READ undoText NOTIFY historyChanged)
    Q_PROPERTY(QString redoText READ redoText NOTIFY historyChanged)
    Q_PROPERTY(int historyLimit READ historyLimit WRITE setHistoryLimit NOTIFY historyChanged)
    Q_PROPERTY(QindaTK::UndoService *undoService READ undoService NOTIFY historyChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY historyChanged)

public:
    static constexpr int DefaultHistoryLimit = 256;
    explicit CanvasDocument(QObject *parent = nullptr);
    ~CanvasDocument() override;
    // A live document from a validated state (CanvasDocumentFormat::decode).
    static std::unique_ptr<CanvasDocument> fromState(const CanvasDocumentState &state,
                                                     const CanvasDocumentLimits &limits = {});

    quint64 revision() const override { return m_revision; }
    QString documentId() const override { return m_documentId; }
    QRectF pageRect() const override { return m_pageRect; }
    void setPageRect(const QRectF &rect);
    QColor pageColor() const override { return m_pageColor; }
    void setPageColor(const QColor &color);
    QByteArray outputProfileIcc() const override { return m_outputProfileIcc; }
    Q_INVOKABLE bool setOutputProfileIcc(const QByteArray &bytes);
    QRectF contentBounds() const override;
    const CanvasObject *object(CanvasObjectId id) const override;
    const CanvasLayer *layer(CanvasLayerId id) const override;
    CanvasTiledImagePtr image(CanvasImageId id) const override;
    QVector<CanvasObjectId> candidates(const QRectF &region) const override;
    qint64 paintKey(const CanvasObject &object) const override;
    QVector<CanvasBackdrop> backdrops() const override;
    bool commit(const CanvasEdit &edit) override { return !commitEdit(edit); }
    // The same commit with the reason for a rejection; nothing changes then.
    OperationError commitEdit(const CanvasEdit &edit);

    quint64 resetGeneration() const { return m_resetGeneration; }
    QString resetGenerationString() const { return QString::number(m_resetGeneration); }
    int objectCount() const { return int(m_objects.size()); }
    int layerCount() const { return int(m_layers.size()); }
    int imageCount() const { return int(m_images.size()); }
    CanvasLayerId activeLayer() const override { return m_activeLayer; }
    CanvasObjectId reserveObjectId() override { return allocateId(); }
    qint64 reserveZ() override;
    // Editor state, not content: where tools insert. It does not change the
    // revision and is not saved; a loaded document starts on its topmost
    // content layer. A layer group can never be the active layer.
    void setActiveLayer(CanvasLayerId id);
    QVariantList layerList() const;
    QVariantList guideList() const;
    const QVector<CanvasGuide> &guideRecords() const { return m_guides; }
    const CanvasGeometry::SpatialIndex &spatialIndex() const { return m_index; }
    QVector<CanvasObjectId> degenerateObjects() const;
    QVector<CanvasObjectId> objectIds() const; // ascending
    CanvasDocumentLimits limits() const { return m_limits; }
    void setLimits(const CanvasDocumentLimits &limits) { m_limits = limits; }
    // O(1) immutable capture of the content (see CanvasDocumentState).
    CanvasDocumentState state() const;

    UndoService *undoService() const;
    bool canUndo() const;
    bool canRedo() const;
    QString undoText() const;
    QString redoText() const;
    int historyLimit() const;
    void setHistoryLimit(int limit);
    QString lastError() const { return m_lastError; }

    // C++ construction helpers. Each allocates a fresh id; `insert` commits a
    // one-change edit (undoable) unless `record` is false (fixtures), which
    // still validates but makes a saved clean state unreachable.
    CanvasObjectId allocateId();
    CanvasObjectId insert(CanvasObject object, bool record = true);
    CanvasLayerId addLayer(const QString &name);
    // Tiles are normalized to ARGB32 premultiplied; returns 0 when rejected.
    CanvasImageId addImage(CanvasTiledImage image);
    bool removeLayer(CanvasLayerId id);
    bool removeImage(CanvasImageId id);
    bool setLayerVisible(CanvasLayerId id, bool visible);
    bool setLayerLocked(CanvasLayerId id, bool locked);

    // Layer hierarchy (docs/canvas.md#layer-hierarchy). `index` counts the
    // destination siblings in paint order (0 = bottom) and is clamped. Each
    // accepted call is one undoable edit; a refusal sets lastError only.
    CanvasLayerId addLayerAt(const QString &name, bool group, CanvasLayerId parent, int index);
    CanvasLayerId addLayerGroup(const QString &name);
    bool moveLayer(CanvasLayerId id, CanvasLayerId parent, int index);
    bool renameLayer(CanvasLayerId id, const QString &name);
    // Wraps sibling layers (one parent) in a new group at the topmost one's place.
    CanvasLayerId groupLayers(const QVector<CanvasLayerId> &ids, const QString &name);
    // Moves a group's children to its parent at its place and removes it.
    bool ungroupLayer(CanvasLayerId id);
    // Removes a layer, its descendant layers and their objects in one edit.
    bool removeLayerTree(CanvasLayerId id);
    // Moves an object to a content layer at `index` of that layer's paint order.
    bool moveObjectToLayer(CanvasObjectId id, CanvasLayerId layer, int index);
    QVector<CanvasLayerId> childLayers(CanvasLayerId parent) const; // paint order
    // Every layer's objects in paint order, in one pass (for tree views).
    QHash<CanvasLayerId, QVector<CanvasObjectId>> objectsByLayer() const;
    QVector<CanvasObjectId> objectsOnLayer(CanvasLayerId layer) const; // paint order
    int layerDepth(CanvasLayerId id) const; // -1 when missing
    bool isLayerAncestor(CanvasLayerId ancestor, CanvasLayerId id) const;
    int layerRank(CanvasLayerId id) const { return m_layerRank.value(id, -1); }

    // Artboards (docs/canvas.md#artboards). Each accepted call is one
    // undoable edit; addArtboard returns the new stable id or "".
    const QVector<CanvasArtboard> &artboardRecords() const { return m_artboards; }
    const CanvasArtboard *artboard(const QString &id) const;
    int artboardCount() const { return int(m_artboards.size()); }
    QVariantList artboardList() const;
    Q_INVOKABLE QString addArtboard(const QString &name, const QRectF &rect,
                                    const QColor &background = QColor(Qt::white));
    Q_INVOKABLE bool setArtboardRect(const QString &id, const QRectF &rect);
    Q_INVOKABLE bool renameArtboard(const QString &id, const QString &name);
    Q_INVOKABLE bool setArtboardBackground(const QString &id, const QColor &color);
    Q_INVOKABLE bool setArtboardBleed(const QString &id, qreal bleed);
    Q_INVOKABLE bool moveArtboard(const QString &id, int index); // board order
    Q_INVOKABLE bool removeArtboard(const QString &id);
    Q_INVOKABLE QVariantMap artboardInfo(const QString &id) const;
    // The topmost board whose rectangle contains the document point, or "".
    Q_INVOKABLE QString artboardAt(qreal x, qreal y) const;
    // SVG whose viewBox is the board's export rectangle over its background.
    Q_INVOKABLE bool exportArtboardSvg(const QString &id, const QString &filePath);

    // QML facade. Colours are document DATA, not chrome.
    Q_INVOKABLE qulonglong addLayerNamed(const QString &name) { return addLayer(name); }
    Q_INVOKABLE qulonglong addRect(qreal x, qreal y, qreal w, qreal h, const QColor &fill,
                                   const QColor &stroke = {}, qreal strokeWidth = 0);
    Q_INVOKABLE qulonglong addEllipse(qreal x, qreal y, qreal w, qreal h, const QColor &fill,
                                      const QColor &stroke = {}, qreal strokeWidth = 0);
    Q_INVOKABLE qulonglong addPolyline(const QVariantList &points, const QColor &stroke,
                                       qreal strokeWidth);
    // Nodes are {x,y,inX,inY,outX,outY} in local coordinates; omitted controls
    // default to the anchor. Every gesture-end call is one undoable edit.
    Q_INVOKABLE qulonglong addPath(const QVariantList &nodes, bool closed,
                                  const QColor &fill, const QColor &stroke, qreal strokeWidth);
    // Parametric paths (docs/vector-authoring.md#parametric-shapes): kind
    // "polygon" {sides}, "star" {points, innerRadius} or "rounded-rectangle"
    // {radius}, each also taking x/y/width/height. Invalid values or another
    // kind's keys return 0/false without an edit; each accepted parameter
    // change regenerates the path as one undoable edit.
    Q_INVOKABLE qulonglong addParametricShape(const QString &kind, qreal x, qreal y,
                                              qreal width, qreal height,
                                              const QVariantMap &parameters,
                                              const QColor &fill, const QColor &stroke,
                                              qreal strokeWidth);
    Q_INVOKABLE QVariantMap parametricParameters(qulonglong id) const;
    Q_INVOKABLE bool setParametricParameters(qulonglong id, const QVariantMap &parameters);
    Q_INVOKABLE QVariantList pathNodes(qulonglong id) const;
    Q_INVOKABLE bool movePathPart(qulonglong id, int index, const QString &part, qreal x, qreal y);
    Q_INVOKABLE bool insertPathNode(qulonglong id, int index, const QVariantMap &node);
    Q_INVOKABLE bool removePathNode(qulonglong id, int index);
    Q_INVOKABLE bool setPathClosed(qulonglong id, bool closed);
    Q_INVOKABLE int pathContourCount(qulonglong id) const;
    Q_INVOKABLE QVariantList pathContourNodes(qulonglong id, int contour) const;
    Q_INVOKABLE bool addPathContour(qulonglong id, const QVariantList &nodes, bool closed = true);
    Q_INVOKABLE bool removePathContour(qulonglong id, int contour);
    Q_INVOKABLE bool movePathContourPart(qulonglong id, int contour, int index,
                                        const QString &part, qreal x, qreal y);
    Q_INVOKABLE bool insertPathContourNode(qulonglong id, int contour, int index, const QVariantMap &node);
    Q_INVOKABLE bool removePathContourNode(qulonglong id, int contour, int index);
    Q_INVOKABLE bool setPathContourClosed(qulonglong id, int contour, bool closed);
    Q_INVOKABLE bool exportSvg(const QString &filePath);
    // matrix = [m11, m12, m21, m22, dx, dy] (QTransform affine order)
    Q_INVOKABLE bool setObjectTransform(qulonglong id, const QVariantList &matrix);
    Q_INVOKABLE bool setObjectVisible(qulonglong id, bool visible);
    Q_INVOKABLE bool setObjectLocked(qulonglong id, bool locked);
    Q_INVOKABLE bool moveObjects(const QVariantList &ids, qreal dx, qreal dy);
    Q_INVOKABLE bool removeObject(qulonglong id);
    Q_INVOKABLE bool setLayerVisibleById(qulonglong id, bool visible) { return setLayerVisible(id, visible); }
    Q_INVOKABLE bool setLayerLockedById(qulonglong id, bool locked) { return setLayerLocked(id, locked); }
    Q_INVOKABLE bool removeLayerById(qulonglong id) { return removeLayer(id); }
    Q_INVOKABLE qulonglong addGroupNamed(const QString &name) { return addLayerGroup(name); }
    Q_INVOKABLE bool moveLayerTo(qulonglong id, qulonglong parent, int index) { return moveLayer(id, parent, index); }
    Q_INVOKABLE bool renameLayerById(qulonglong id, const QString &name) { return renameLayer(id, name); }
    Q_INVOKABLE qulonglong groupLayersById(const QVariantList &ids, const QString &name);
    Q_INVOKABLE bool ungroupLayerById(qulonglong id) { return ungroupLayer(id); }
    Q_INVOKABLE bool removeLayerTreeById(qulonglong id) { return removeLayerTree(id); }
    Q_INVOKABLE bool moveObjectTo(qulonglong id, qulonglong layer, int index) { return moveObjectToLayer(id, layer, index); }
    Q_INVOKABLE QVariantList childLayerIds(qulonglong parent) const;
    Q_INVOKABLE QVariantList layerObjectIds(qulonglong layer) const;
    Q_INVOKABLE QVariantMap objectInfo(qulonglong id) const;
    // Document guides (docs/canvas.md#document-guides). Each accepted call is
    // one undoable edit; addGuide returns the new stable id or "" on refusal.
    Q_INVOKABLE QString addGuide(const QString &orientation, qreal position);
    Q_INVOKABLE bool moveGuide(const QString &id, qreal position);
    Q_INVOKABLE bool removeGuide(const QString &id);
    Q_INVOKABLE bool undo();
    Q_INVOKABLE bool redo();
    // Removes every object and image (a model reset, not an undoable edit).
    Q_INVOKABLE void clear();
    // Deterministic fixture: `count` shapes over a grid plus one tiled image.
    // A fixture is a load, not an edit: it resets the model and its history.
    Q_INVOKABLE qulonglong populateFixture(int count, int seed = 1, bool withImage = true);

    // DocumentModel: history steps carry CanvasEdit deltas.
    QObject *modelObject() const override { return const_cast<CanvasDocument *>(this); }
    DocumentId modelId() const override { return m_id; }
    QString modelKind() const override { return QStringLiteral("canvas"); }
    SnapshotHeader modelHeader() const override;
    QMetaObject::Connection connectModelChanged(QObject *receiver,
                                                std::function<void()> slot) override;
    OperationError prepareCommand(const DocumentCommand &command, bool forward) const override;
    void publishCommand(const DocumentCommand &command, bool forward) override;
    DocumentCommandPtr composeCommands(const DocumentCommand &first,
                                       const DocumentCommand &second) const override;
    // QQmlParserStatus: declarative initial values are not undoable edits.
    void classBegin() override { m_initializing = true; }
    void componentComplete() override { m_initializing = false; }

protected:
    void historyAttached(UndoService *service) override;

signals:
    void changed();
    void historyChanged();

private:
    struct RestoreTag {};
    CanvasDocument(RestoreTag, QObject *parent);
    void attachLocalHistory();
    UndoService *history() const;
    OperationError prepareEdit(const CanvasEdit &edit, bool forward) const;
    void applyEdit(const CanvasEdit &edit, bool forward, QVector<QRectF> *damage, bool *full);
    void applyDirect(const CanvasEdit &edit); // validated, unrecorded
    OperationError reject(const OperationError &error);
    void place(const CanvasObject &object, QVector<QRectF> *damage);
    void unplace(CanvasObjectId id, QVector<QRectF> *damage);
    void placeImage(CanvasImageId id, const CanvasTiledImagePtr &image);
    void placeLayer(const CanvasLayer &layer);
    void unplaceLayer(CanvasLayerId id);
    void finishChange(const QVector<QRectF> &damage, bool full);
    void layerDamage(CanvasLayerId id, QVector<QRectF> *damage) const;
    void imageDamage(CanvasImageId id, QVector<QRectF> *damage) const;
    void resetModel();
    void ensureActiveLayer();
    int nextLayerOrder() const;
    std::optional<CanvasObject> copy(CanvasObjectId id) const;
    // canvas_document_layers.cpp: ranks and child lists after layer changes,
    // and the hierarchy an edit would leave (parents, cycles, depth, groups).
    void refreshLayerTree();
    OperationError prepareLayerTree(const CanvasEdit &edit, bool forward,
                                    const QHash<CanvasLayerId, qsizetype> &layerUse) const;
    // One labelled edit: `layers` are after-states by id (unchanged ones are
    // skipped), `removed` layers go away, `objects` ride along.
    bool commitLayers(const QString &label, const QHash<CanvasLayerId, CanvasLayer> &layers,
                      const QVector<CanvasLayerId> &removed = {},
                      const QVector<CanvasEdit::Change> &objects = {});
    bool commitRecorded(const CanvasEdit &edit); // commitEdit, reason in lastError
    bool refuse(const QString &message);
    bool unchanged(); // a request that changes nothing: false, lastError cleared
    void artboardDamage(const CanvasEdit::ArtboardChange &change, QVector<QRectF> *damage) const;

    DocumentId m_id;
    QString m_documentId;
    QHash<CanvasObjectId, CanvasObject> m_objects;
    QHash<CanvasLayerId, CanvasLayer> m_layers;
    QHash<CanvasImageId, CanvasTiledImagePtr> m_images;
    QVector<CanvasGuide> m_guides;
    quint64 m_nextGuide = 1;
    QVector<CanvasArtboard> m_artboards;
    quint64 m_nextArtboard = 1;
    QHash<CanvasLayerId, int> m_layerRank; // depth-first flattened paint rank
    QHash<CanvasLayerId, QVector<CanvasLayerId>> m_layerChildren; // paint order
    QHash<CanvasObjectId, QRectF> m_bounds;
    QHash<CanvasLayerId, qsizetype> m_layerUse;  // objects per layer
    QHash<CanvasImageId, qsizetype> m_imageUse;  // image objects per image
    CanvasGeometry::SpatialIndex m_index;
    UndoService *m_localHistory = nullptr;
    QMetaObject::Connection m_historyConnection;
    CanvasDocumentLimits m_limits;
    QString m_lastError;
    QRectF m_pageRect{0, 0, 1024, 768};
    QColor m_pageColor{Qt::white};
    QByteArray m_outputProfileIcc;
    quint64 m_revision = 1;
    quint64 m_resetGeneration = 0;
    qsizetype m_totalPoints = 0;
    qsizetype m_imageBytes = 0;
    qsizetype m_encodedBytes = 0; // records only; see CanvasDocumentFormat::FixedBytes
    CanvasObjectId m_nextId = 1;
    CanvasLayerId m_nextLayer = 1;
    CanvasImageId m_nextImage = 1;
    CanvasLayerId m_activeLayer = 0;
    qint64 m_nextZ = 1;
    bool m_initializing = false;
};

} // namespace QindaTK
