// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "canvas_document.h"

#include <QHash>
#include <QPointer>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {
class SlideDeck;

// AGENT-CONTRACT (docs/slides.md#canvas, docs/canvas.md#document-adapter):
// one slide of a SlideDeck seen through the WP-042 canvas boundary, so
// SlideCanvas uses CanvasView, ToolController, SpatialTransform, SnapModel,
// SelectionOverlay and GuideLayer unchanged. Each resolved slide object is
// an image object whose pixels come from Slides::renderObjectImage (the
// output painter's own object code), so the canvas shows the master
// background, font sizes and rich formatting the audience sees. commit()
// accepts only what a slide can hold: object moves, resizes and rotations
// (projected to bounds + rotation) and removals; it applies them to the deck
// as ONE SlideDeck undo step. Inserts, layer, page and guide edits are
// refused (insert through SlideDeck.insertObject).
class SlideCanvasDocument : public CanvasDocumentAdapter {
    Q_OBJECT
    QML_ELEMENT
    Q_MOC_INCLUDE("slide_deck.h")
    Q_PROPERTY(QindaTK::SlideDeck *deck READ deck WRITE setDeck NOTIFY sourceChanged)
    Q_PROPERTY(QString slideId READ slideId WRITE setSlideId NOTIFY sourceChanged)
    // Paint placeholder prompts on empty placeholders (editing only).
    Q_PROPERTY(bool showPrompts READ showPrompts WRITE setShowPrompts NOTIFY sourceChanged)
    // Object image pixels per slide pixel (sharpness when zoomed in).
    Q_PROPERTY(qreal renderScale READ renderScale WRITE setRenderScale NOTIFY sourceChanged)
    Q_PROPERTY(qulonglong revision READ revision NOTIFY changed)
    Q_PROPERTY(int objectCount READ objectCount NOTIFY changed)
    Q_PROPERTY(QRectF pageRect READ pageRect NOTIFY changed)
    Q_PROPERTY(QColor pageColor READ pageColor NOTIFY changed)
    // Slide centre lines, {id, orientation, position}: GuideLayer.guideData
    // and SnapModel.guides read this shape.
    Q_PROPERTY(QVariantList guides READ guides NOTIFY changed)
    Q_PROPERTY(int imageRenders READ imageRenders NOTIFY changed)
public:
    explicit SlideCanvasDocument(QObject *parent = nullptr);

    SlideDeck *deck() const { return m_deck; }
    void setDeck(SlideDeck *deck);
    QString slideId() const { return m_slideId; }
    void setSlideId(const QString &id);
    bool showPrompts() const { return m_showPrompts; }
    void setShowPrompts(bool show);
    qreal renderScale() const { return m_renderScale; }
    void setRenderScale(qreal scale);
    int objectCount() const { return int(m_objects.size()); }
    QVariantList guides() const;
    int imageRenders() const { return m_imageRenders; }

    // Canvas <-> slide object identity (stable for the adapter's lifetime).
    Q_INVOKABLE QString slideObjectId(qulonglong canvasId) const { return m_slideIds.value(canvasId); }
    Q_INVOKABLE qulonglong canvasObjectId(const QString &slideObjectId) const { return m_canvasIds.value(slideObjectId); }
    Q_INVOKABLE QStringList slideObjectIds(const QVariantList &canvasIds) const;
    Q_INVOKABLE QVariantList canvasObjectIds(const QStringList &slideObjectIds) const;

    // CanvasDocumentAdapter
    quint64 revision() const override { return m_revision; }
    QString documentId() const override;
    QRectF pageRect() const override;
    QRectF contentBounds() const override;
    QColor pageColor() const override;
    const CanvasObject *object(CanvasObjectId id) const override;
    const CanvasLayer *layer(CanvasLayerId id) const override;
    CanvasTiledImagePtr image(CanvasImageId id) const override;
    QVector<CanvasObjectId> candidates(const QRectF &region) const override;
    CanvasLayerId activeLayer() const override { return m_layer.id; }
    CanvasObjectId reserveObjectId() override { return m_nextId++; }
    qint64 reserveZ() override { return qint64(m_objects.size()); }
    bool commit(const CanvasEdit &edit) override;

    // The canvas object for a slide object with `bounds` and `rotation`, and
    // the inverse projection of any transformed object (shear is dropped:
    // a slide object is a rotated rectangle).
    static QTransform objectTransform(const QRectF &bounds, double rotation);
    static void projectTransform(const QRectF &localRect, const QTransform &transform,
                                 QRectF *bounds, double *rotation);

signals:
    void sourceChanged();
    void changed();

private:
    struct CachedImage {
        QString key;
        CanvasTiledImagePtr image;
    };
    void rebuild();
    CanvasObjectId idFor(const QString &slideObjectId);

    QPointer<SlideDeck> m_deck;
    QMetaObject::Connection m_deckConnection;
    QString m_slideId;
    bool m_showPrompts = true;
    qreal m_renderScale = 2;
    quint64 m_revision = 1;
    CanvasObjectId m_nextId = 1;
    CanvasLayer m_layer;
    QVector<CanvasObject> m_objects; // paint order
    QHash<CanvasObjectId, int> m_index;
    QHash<QString, CanvasObjectId> m_canvasIds;
    QHash<CanvasObjectId, QString> m_slideIds;
    QHash<CanvasObjectId, CachedImage> m_images;
    int m_imageRenders = 0;
};

} // namespace QindaTK
