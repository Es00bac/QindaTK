// SPDX-License-Identifier: LGPL-3.0-or-later
// Reference tools. Each keeps its gesture state in members that are reset
// by begin() and cancel(); none writes to the document before finish().
#include "hit_test_service.h"
#include "tool_controller.h"

#include <QCoreApplication>
#include <cmath>

namespace QindaTK {

namespace {

QString trTool(const char *text)
{
    return QCoreApplication::translate("QindaTK::CanvasTools", text);
}

// Refuses drawing into a layer the user cannot see or has locked.
bool layerWritable(const CanvasToolContext &ctx, QString *declined)
{
    const CanvasLayer *layer = ctx.document ? ctx.document->layer(ctx.document->activeLayer()) : nullptr;
    if (!layer) {
        *declined = QStringLiteral("no-layer");
        return false;
    }
    if (layer->locked) {
        *declined = QStringLiteral("layer-locked");
        return false;
    }
    if (!layer->visible) {
        *declined = QStringLiteral("layer-hidden");
        return false;
    }
    return true;
}

CanvasObject newObject(CanvasToolContext &ctx, CanvasShape shape)
{
    CanvasObject o;
    o.id = ctx.document->reserveObjectId();
    o.z = ctx.document->reserveZ();
    o.layer = ctx.document->activeLayer();
    o.shape = shape;
    o.fill = ctx.fillColor;
    o.stroke = ctx.strokeColor;
    o.strokeWidth = ctx.strokeWidth;
    return o;
}

class SelectTool final : public CanvasTool {
public:
    QString id() const override { return QStringLiteral("select"); }
    QString label() const override { return trTool("Select"); }
    QString icon() const override { return QStringLiteral("mouse-pointer-2"); }
    bool editsDocument() const override { return true; }

    bool begin(CanvasToolContext &ctx, const CanvasToolInput &in, QString *declined) override
    {
        if (!ctx.document) {
            *declined = QStringLiteral("no-document");
            return false;
        }
        m_before = *ctx.selection;
        m_start = m_current = in.documentPoint;
        m_moving.clear();
        const bool additive = in.modifiers & Qt::ShiftModifier;
        CanvasHitPolicy policy;
        policy.maxResults = 1;
        const auto hits = HitTestService::hitPoint(
            *ctx.document, in.documentPoint,
            HitTestService::documentTolerance(ctx.hitTolerancePixels, in.zoom), policy);
        if (!hits.isEmpty()) {
            const CanvasObjectId hit = hits.first().object;
            if (!ctx.selection->contains(hit)) {
                if (!additive) {
                    ctx.selection->clear();
                }
                ctx.selection->append(hit);
            }
            for (CanvasObjectId id : *ctx.selection) {
                const CanvasObject *o = ctx.document->object(id);
                if (o && !ctx.document->effectiveLocked(*o)) {
                    m_moving.append(id);
                }
            }
            m_mode = Move;
        } else {
            if (!additive) {
                ctx.selection->clear();
            }
            m_mode = Marquee;
        }
        return true;
    }
    void update(CanvasToolContext &, const CanvasToolInput &in) override { m_current = in.documentPoint; }
    CanvasEdit finish(CanvasToolContext &ctx, const CanvasToolInput &in) override
    {
        update(ctx, in);
        CanvasEdit edit;
        const QPointF offset = m_current - m_start;
        if (m_mode == Move && ctx.document && std::hypot(offset.x(), offset.y()) * in.zoom >= 1.0) {
            edit.label = trTool("Move");
            for (CanvasObjectId id : m_moving) {
                const CanvasObject *o = ctx.document->object(id);
                if (!o) {
                    continue;
                }
                CanvasObject next = *o;
                next.transform = next.transform * QTransform::fromTranslate(offset.x(), offset.y());
                edit.changes.append({id, *o, next});
            }
        } else if (m_mode == Marquee && ctx.document) {
            const QRectF r = QRectF(m_start, m_current).normalized();
            if (r.width() * in.zoom >= 2.0 || r.height() * in.zoom >= 2.0) {
                const auto ids = HitTestService::hitRegion(*ctx.document, QPolygonF(r),
                                                           CanvasRegionMode::Intersect);
                for (CanvasObjectId id : ids) {
                    if (!ctx.selection->contains(id)) {
                        ctx.selection->append(id);
                    }
                }
            }
        }
        m_mode = Idle;
        return edit;
    }
    void cancel(CanvasToolContext &ctx) override
    {
        *ctx.selection = m_before; // a canceled click does not change selection
        m_moving.clear();
        m_mode = Idle;
    }
    CanvasToolPreview preview() const override
    {
        CanvasToolPreview p;
        if (m_mode == Marquee) {
            p.kind = CanvasToolPreview::Marquee;
            p.rect = QRectF(m_start, m_current).normalized();
        } else if (m_mode == Move) {
            p.kind = CanvasToolPreview::Move;
            p.offset = m_current - m_start;
            p.ids = m_moving;
        }
        return p;
    }

private:
    enum Mode { Idle, Move, Marquee } m_mode = Idle;
    QPointF m_start;
    QPointF m_current;
    QVector<CanvasObjectId> m_moving;
    QVector<CanvasObjectId> m_before;
};

class PanTool final : public CanvasTool {
public:
    QString id() const override { return QStringLiteral("pan"); }
    QString label() const override { return trTool("Pan"); }
    QString icon() const override { return QStringLiteral("hand"); }
    Qt::CursorShape cursor() const override { return m_active ? Qt::ClosedHandCursor : Qt::OpenHandCursor; }
    bool editsDocument() const override { return false; }
    bool begin(CanvasToolContext &, const CanvasToolInput &in, QString *) override
    {
        m_last = in.viewPoint;
        m_active = true;
        return true;
    }
    void update(CanvasToolContext &ctx, const CanvasToolInput &in) override
    {
        if (ctx.navigator) {
            ctx.navigator->navigatePan(in.viewPoint - m_last);
        }
        m_last = in.viewPoint;
    }
    CanvasEdit finish(CanvasToolContext &ctx, const CanvasToolInput &in) override
    {
        update(ctx, in);
        m_active = false;
        return {};
    }
    void cancel(CanvasToolContext &) override { m_active = false; }

private:
    QPointF m_last;
    bool m_active = false;
};

class ShapeTool final : public CanvasTool {
public:
    explicit ShapeTool(CanvasShape shape) : m_shape(shape) {}
    QString id() const override { return m_shape == CanvasShape::Rect ? QStringLiteral("rectangle") : QStringLiteral("ellipse"); }
    QString label() const override { return m_shape == CanvasShape::Rect ? trTool("Rectangle") : trTool("Ellipse"); }
    QString icon() const override { return m_shape == CanvasShape::Rect ? QStringLiteral("square") : QStringLiteral("circle"); }
    Qt::CursorShape cursor() const override { return Qt::CrossCursor; }
    bool editsDocument() const override { return true; }
    bool begin(CanvasToolContext &ctx, const CanvasToolInput &in, QString *declined) override
    {
        if (!layerWritable(ctx, declined)) {
            return false;
        }
        m_start = m_current = in.documentPoint;
        m_square = false;
        m_active = true;
        return true;
    }
    void update(CanvasToolContext &, const CanvasToolInput &in) override
    {
        m_current = in.documentPoint;
        m_square = in.modifiers & Qt::ShiftModifier;
    }
    CanvasEdit finish(CanvasToolContext &ctx, const CanvasToolInput &in) override
    {
        m_current = in.documentPoint;
        const QRectF r = rect();
        m_active = false;
        CanvasEdit edit;
        if (!ctx.document || (r.width() * in.zoom < 2.0 && r.height() * in.zoom < 2.0)) {
            return edit; // a click, not a drag
        }
        CanvasObject o = newObject(ctx, m_shape);
        o.rect = QRectF(QPointF(0, 0), r.size());
        o.transform = QTransform::fromTranslate(r.x(), r.y());
        edit.label = m_shape == CanvasShape::Rect ? trTool("Draw rectangle") : trTool("Draw ellipse");
        edit.changes.append({o.id, std::nullopt, o});
        return edit;
    }
    void cancel(CanvasToolContext &) override { m_active = false; }
    CanvasToolPreview preview() const override
    {
        CanvasToolPreview p;
        if (m_active) {
            p.kind = m_shape == CanvasShape::Rect ? CanvasToolPreview::Rect : CanvasToolPreview::Ellipse;
            p.rect = rect();
        }
        return p;
    }

private:
    QRectF rect() const
    {
        QPointF end = m_current;
        if (m_square) {
            const qreal side = std::max(std::abs(end.x() - m_start.x()), std::abs(end.y() - m_start.y()));
            end = m_start + QPointF(std::copysign(side, end.x() - m_start.x()),
                                    std::copysign(side, end.y() - m_start.y()));
        }
        return QRectF(m_start, end).normalized();
    }
    CanvasShape m_shape;
    QPointF m_start;
    QPointF m_current;
    bool m_square = false;
    bool m_active = false;
};

// Pen draws a polyline; with an eraser device it removes what it crosses.
class PenTool final : public CanvasTool {
public:
    // AGENT-NOTE: bounded transient state -- a stroke keeps at most this many
    // samples; later samples only move the final point.
    static constexpr int kMaxPoints = 4096;

    QString id() const override { return QStringLiteral("pen"); }
    QString label() const override { return trTool("Pen"); }
    QString icon() const override { return QStringLiteral("pen-tool"); }
    Qt::CursorShape cursor() const override { return Qt::CrossCursor; }
    bool editsDocument() const override { return true; }
    bool begin(CanvasToolContext &ctx, const CanvasToolInput &in, QString *declined) override
    {
        m_device = in.device; // the release event may not report the pen
        m_erase = in.device == CanvasDevice::Eraser;
        if (!m_erase && !layerWritable(ctx, declined)) {
            return false;
        }
        if (m_erase && !ctx.document) {
            *declined = QStringLiteral("no-document");
            return false;
        }
        m_points = {in.documentPoint};
        m_pressureSum = in.pressure;
        m_erased.clear();
        m_active = true;
        sample(ctx, in);
        return true;
    }
    void update(CanvasToolContext &ctx, const CanvasToolInput &in) override
    {
        const QPointF d = in.documentPoint - m_points.constLast();
        if (std::hypot(d.x(), d.y()) * in.zoom < 1.5) {
            return;
        }
        if (m_points.size() < kMaxPoints) {
            m_points << in.documentPoint;
            m_pressureSum += in.pressure;
        } else {
            m_points.last() = in.documentPoint;
        }
        sample(ctx, in);
    }
    CanvasEdit finish(CanvasToolContext &ctx, const CanvasToolInput &in) override
    {
        update(ctx, in);
        m_active = false;
        CanvasEdit edit;
        if (!ctx.document) {
            return edit;
        }
        if (m_erase) {
            edit.label = trTool("Erase");
            for (CanvasObjectId id : m_erased) {
                if (const CanvasObject *o = ctx.document->object(id)) {
                    edit.changes.append({id, *o, std::nullopt});
                }
            }
            return edit;
        }
        if (m_points.size() < 2) {
            return edit;
        }
        CanvasObject o = newObject(ctx, CanvasShape::Polyline);
        const QPointF origin = m_points.boundingRect().topLeft();
        for (const QPointF &p : m_points) {
            o.points << p - origin;
        }
        o.fill = {};
        const qreal pressure = m_pressureSum / m_points.size();
        if (m_device == CanvasDevice::Pen) {
            o.strokeWidth = ctx.strokeWidth * std::clamp(pressure, 0.25, 1.5);
        }
        o.transform = QTransform::fromTranslate(origin.x(), origin.y());
        edit.label = trTool("Draw stroke");
        edit.changes.append({o.id, std::nullopt, o});
        return edit;
    }
    void cancel(CanvasToolContext &) override
    {
        m_active = false;
        m_points.clear();
        m_erased.clear();
    }
    CanvasToolPreview preview() const override
    {
        CanvasToolPreview p;
        if (m_active) {
            p.kind = CanvasToolPreview::Polyline;
            p.points = m_points;
            p.ids = m_erased;
        }
        return p;
    }

private:
    void sample(CanvasToolContext &ctx, const CanvasToolInput &in)
    {
        if (!m_erase || !ctx.document) {
            return;
        }
        const auto hits = HitTestService::hitPoint(
            *ctx.document, in.documentPoint,
            HitTestService::documentTolerance(ctx.hitTolerancePixels, in.zoom));
        for (const CanvasHit &h : hits) {
            if (!m_erased.contains(h.object)) {
                m_erased << h.object;
            }
        }
    }
    QPolygonF m_points;
    QVector<CanvasObjectId> m_erased;
    qreal m_pressureSum = 0;
    CanvasDevice m_device = CanvasDevice::Mouse;
    bool m_erase = false;
    bool m_active = false;
};

} // namespace

std::vector<std::unique_ptr<CanvasTool>> createStandardCanvasTools()
{
    std::vector<std::unique_ptr<CanvasTool>> tools;
    tools.push_back(std::make_unique<SelectTool>());
    tools.push_back(std::make_unique<PanTool>());
    tools.push_back(std::make_unique<ShapeTool>(CanvasShape::Rect));
    tools.push_back(std::make_unique<ShapeTool>(CanvasShape::Ellipse));
    tools.push_back(std::make_unique<PenTool>());
    return tools;
}

} // namespace QindaTK
