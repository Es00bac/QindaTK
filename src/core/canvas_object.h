// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QColor>
#include <QImage>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QTransform>
#include <QVector>
#include <memory>

namespace QindaTK {

// AGENT-CONTRACT (docs/canvas.md#object-model): the reference spatial object
// model. Identity is a stable 64-bit id that is never reused inside one
// CanvasDocument, so a hit result, a selection or an undo step can outlive
// edits to other objects. Geometry is stored in the object's LOCAL space and
// `transform` maps local -> document. Paint order is (layer order, z);
// larger draws later and is hit first; with layer groups the layer order is
// its rank in the depth-first flattening of the layer tree.
using CanvasObjectId = quint64;
using CanvasLayerId = quint64;
using CanvasImageId = quint64;

enum class CanvasShape : quint8 { Rect, Ellipse, Polyline, Image, Path };
enum class CanvasParametricKind : quint8 { None, Polygon, Star, RoundedRect };

// Absolute local-space controls. A control equal to its anchor gives a
// straight endpoint. Node order and the closed flag define one contour.
struct CanvasPathNode {
    QPointF anchor;
    QPointF in;
    QPointF out;
    friend bool operator==(const CanvasPathNode &, const CanvasPathNode &) = default;
};

struct CanvasPathContour {
    QVector<CanvasPathNode> nodes;
    bool closed = true;
    friend bool operator==(const CanvasPathContour &, const CanvasPathContour &) = default;
};

// Subobject parts a hit can resolve to. `index` on a hit names the vertex,
// segment or image tile; it is -1 for Fill/Stroke of a whole outline.
enum class CanvasPart : quint8 { Fill, Stroke, Vertex, Segment, ImageTile };

enum class CanvasStrokeAlignment : quint8 { Center, Inside, Outside };
// The separable W3C compositing modes the QPainter raster engine implements.
enum class CanvasBlendMode : quint8 { Normal, Multiply, Screen, Overlay, Darken, Lighten,
                                      ColorDodge, ColorBurn, HardLight, SoftLight,
                                      Difference, Exclusion };
enum class CanvasPatternKind : quint8 { None, Lines, Grid, Dots, Checker };

// Pattern ink drawn over the fill area in the object's LOCAL space, on cells
// `spacing` wide rotated by `angle` degrees about the local origin. `weight`
// is the line width or dot diameter. Kind None keeps every field default.
struct CanvasPattern {
    CanvasPatternKind kind = CanvasPatternKind::None;
    QColor ink;
    qreal spacing = 0;
    qreal angle = 0;
    qreal weight = 0;
    friend bool operator==(const CanvasPattern &, const CanvasPattern &) = default;
};

// One entry of an object's ordered effect stack. `id` is stable within the
// object; `parameters` is canonical compact JSON (an object), so a kind the
// reference renderer does not know round-trips byte for byte.
struct CanvasEffect {
    QString id;
    QString kind;
    bool enabled = true;
    QByteArray parameters;
    friend bool operator==(const CanvasEffect &, const CanvasEffect &) = default;
};

// AGENT-CONTRACT (docs/vector-authoring.md#appearance, D-106): appearance
// beyond flat fill and stroke. Effects apply in list order (index 0 first) to
// the painted fill, pattern and stroke; opacity and blend then composite the
// result. Validation and the native v6 codec live in canvas_appearance.h.
struct CanvasAppearance {
    CanvasStrokeAlignment strokeAlignment = CanvasStrokeAlignment::Center;
    qreal miterLimit = 4.0; // SVG stroke-miterlimit semantics
    qreal opacity = 1.0;
    CanvasBlendMode blend = CanvasBlendMode::Normal;
    CanvasPattern pattern;
    QVector<CanvasEffect> effects;
    [[nodiscard]] bool isDefault() const { return *this == CanvasAppearance{}; }
    friend bool operator==(const CanvasAppearance &, const CanvasAppearance &) = default;
};

// AGENT-GUARD (D-106): objects hold their appearance through this immutable,
// implicitly shared handle, null for the default. Inline, the ~100-byte
// record grew every object and both copies in every undo step (the WP-044
// round build failed tst_canvas_persistence's history bound on it). Read with
// `->` or `*`; write by assigning a CanvasAppearance or with update(), which
// copies, never touching a record other objects or history steps share.
class CanvasAppearanceRef {
public:
    CanvasAppearanceRef() = default;
    CanvasAppearanceRef(const CanvasAppearance &value) { assign(value); } // NOLINT: implicit by design
    CanvasAppearanceRef &operator=(const CanvasAppearance &value)
    {
        assign(value);
        return *this;
    }
    [[nodiscard]] const CanvasAppearance &get() const { return m_data ? *m_data : defaultValue(); }
    const CanvasAppearance &operator*() const { return get(); }
    const CanvasAppearance *operator->() const { return &get(); }
    [[nodiscard]] bool isDefault() const { return !m_data; }
    template <typename Change> void update(Change &&change)
    {
        CanvasAppearance value = get();
        change(value);
        assign(value);
    }
    friend bool operator==(const CanvasAppearanceRef &a, const CanvasAppearanceRef &b)
    {
        return a.m_data == b.m_data || a.get() == b.get();
    }

private:
    static const CanvasAppearance &defaultValue()
    {
        static const CanvasAppearance value;
        return value;
    }
    void assign(const CanvasAppearance &value)
    {
        m_data = value.isDefault() ? nullptr : std::make_shared<const CanvasAppearance>(value);
    }
    std::shared_ptr<const CanvasAppearance> m_data;
};

struct CanvasObject {
    CanvasObjectId id = 0;
    CanvasLayerId layer = 0;
    CanvasShape shape = CanvasShape::Rect;
    QRectF rect;              // Rect, Ellipse, Image: local rectangle
    QVector<QPointF> points;  // Polyline: local vertices (open path)
    QVector<CanvasPathNode> pathNodes; // Path: cubic nodes in local space
    bool pathClosed = false;
    QVector<CanvasPathContour> extraContours; // additional subpaths; winding follows fillRule
    CanvasParametricKind parametricKind = CanvasParametricKind::None;
    quint32 parametricPoints = 0; // polygon sides or star tips
    qreal parametricInnerRatio = 0; // star radius relative to the outer radius
    qreal parametricCornerRadius = 0; // rounded rectangle local units
    Qt::FillRule fillRule = Qt::OddEvenFill;
    QTransform transform;     // local -> document
    QColor fill;              // invalid or transparent = no fill
    QByteArray managedFill;   // typed solid/gradient plus owned ICC bytes; see canvas_managed_paint.h
    QColor stroke;            // invalid or transparent = no stroke
    qreal strokeWidth = 0.0;  // document units; renderers floor at one device pixel
    Qt::PenCapStyle strokeCap = Qt::FlatCap;
    Qt::PenJoinStyle strokeJoin = Qt::MiterJoin;
    QVector<qreal> dashPattern; // positive even count, in stroke-width units
    CanvasImageId image = 0;  // Image: the document-owned tiled image
    qint64 z = 0;             // order within the layer
    bool visible = true;
    bool locked = false;
    QString name;
    CanvasAppearanceRef appearance; // alignment, opacity, blend, pattern, effects (D-106)

    friend bool operator==(const CanvasObject &, const CanvasObject &) = default;

    [[nodiscard]] qsizetype pathPointCount() const
    { qsizetype count = pathNodes.size(); for (const auto &c : extraContours) count += c.nodes.size(); return count; }
    [[nodiscard]] bool hasClosedContour() const
    { if (pathClosed) return true; for (const auto &c : extraContours) if (c.closed) return true; return false; }
    [[nodiscard]] bool closesArea() const
    { return shape != CanvasShape::Polyline && (shape != CanvasShape::Path || hasClosedContour()); }
    // The fill COLOUR is painted (patterns are separate: hasPattern()).
    [[nodiscard]] bool hasFill() const { return fill.isValid() && fill.alpha() > 0 && closesArea(); }
    [[nodiscard]] bool hasPattern() const
    { return appearance->pattern.kind != CanvasPatternKind::None && appearance->pattern.ink.isValid()
        && appearance->pattern.ink.alpha() > 0 && closesArea(); }
    // Some paint covers the fill area (picking and containment use this).
    [[nodiscard]] bool fillsArea() const { return hasFill() || hasPattern(); }
    [[nodiscard]] bool hasStroke() const { return stroke.isValid() && stroke.alpha() > 0; }
    void clearParametric()
    { parametricKind = CanvasParametricKind::None; parametricPoints = 0;
      parametricInnerRatio = 0; parametricCornerRadius = 0; }
};

// AGENT-CONTRACT (docs/canvas.md#layer-hierarchy, D-092): `order` ranks a
// layer among its siblings (same `parent`, 0 = top level); larger draws
// later. A `group` holds child layers and never objects; a content layer
// holds objects and never child layers, so every parent is a group. Groups
// carry no transform, opacity or blend: hierarchy edits never move geometry,
// and visibility/lock cascade to every descendant.
struct CanvasLayer {
    CanvasLayerId id = 0;
    QString name;
    int order = 0;
    bool visible = true;
    bool locked = false;
    CanvasLayerId parent = 0;
    bool group = false;

    friend bool operator==(const CanvasLayer &, const CanvasLayer &) = default;
};

// AGENT-NOTE: tiled pixel storage for the image kind. Tiles are immutable
// QImages (implicitly shared), so a render tile or a GPU upload can keep a
// reference without copying and without racing a later document edit.
struct CanvasTiledImage {
    CanvasImageId id = 0;
    QSize size;
    int tileSize = 256;
    QVector<QImage> tiles; // row-major, columns() x rows()

    [[nodiscard]] int columns() const { return tileSize > 0 ? (size.width() + tileSize - 1) / tileSize : 0; }
    [[nodiscard]] int rows() const { return tileSize > 0 ? (size.height() + tileSize - 1) / tileSize : 0; }
    [[nodiscard]] QRect tileRect(int column, int row) const
    {
        return QRect(column * tileSize, row * tileSize, tileSize, tileSize)
            .intersected(QRect(QPoint(0, 0), size));
    }
};

using CanvasTiledImagePtr = std::shared_ptr<const CanvasTiledImage>;

} // namespace QindaTK
