// SPDX-License-Identifier: LGPL-3.0-or-later
#include "canvas_path_boolean.h"
#include "canvas_geometry.h"
#include "canvas_path.h"
#include "canvas_transform.h"

#include <QLineF>
#include <algorithm>
#include <cmath>

namespace QindaTK::CanvasPathBoolean {
namespace {
// Relative flattening tolerance: a circle becomes about 160 segments at any
// size, and the result deviates from the true curve by at most 0.01%.
constexpr qreal RelativeTolerance = 1e-4;
constexpr int MaximumDepth = 16;

OperationError failure(ErrorCode code, const QString &message)
{
    return {code, message, QStringLiteral("objects.pathNodes"), QStringLiteral("canvas.boolean")};
}

void flattenCubic(QPolygonF &out, QPointF p0, QPointF p1, QPointF p2, QPointF p3,
                  qreal tolerance, int depth)
{
    // The curve lies in the hull of its controls, so controls within the
    // tolerance of the chord put the whole curve there.
    if (depth >= MaximumDepth
        || std::max(CanvasGeometry::distanceToSegment(p1, p0, p3),
                    CanvasGeometry::distanceToSegment(p2, p0, p3)) <= tolerance) {
        out << p3;
        return;
    }
    const QPointF a = (p0 + p1) / 2, b = (p1 + p2) / 2, c = (p2 + p3) / 2;
    const QPointF ab = (a + b) / 2, bc = (b + c) / 2, mid = (ab + bc) / 2;
    flattenCubic(out, p0, a, ab, mid, tolerance, depth + 1);
    flattenCubic(out, mid, bc, c, p3, tolerance, depth + 1);
}

// A clipper polygon as a closed straight contour in local space: repeated
// and collinear points dropped, fewer than three points rejected.
std::optional<CanvasPathContour> contourFrom(const QPolygonF &polygon, const QTransform &toLocal,
                                             qreal epsilon)
{
    QPolygonF clean;
    for (const QPointF &p : polygon)
        if (clean.isEmpty() || QLineF(clean.last(), p).length() > epsilon) clean << p;
    while (clean.size() > 1 && QLineF(clean.last(), clean.first()).length() <= epsilon)
        clean.removeLast();
    for (qsizetype i = 0; clean.size() >= 3 && i < clean.size();) {
        const QPointF previous = clean[(i + clean.size() - 1) % clean.size()];
        const QPointF next = clean[(i + 1) % clean.size()];
        const QPointF in = clean[i] - previous, out = next - clean[i];
        const qreal cross = in.x() * out.y() - in.y() * out.x();
        const qreal lengths = std::hypot(in.x(), in.y()) * std::hypot(out.x(), out.y());
        if (QPointF::dotProduct(in, out) > 0 && std::abs(cross) <= 1e-9 * lengths)
            clean.removeAt(i);
        else
            ++i;
    }
    if (clean.size() < 3) return std::nullopt;
    CanvasPathContour contour;
    contour.closed = true;
    for (const QPointF &p : std::as_const(clean)) {
        const QPointF local = toLocal.map(p);
        contour.nodes.append({local, local, local});
    }
    return contour;
}

QPainterPath fold(const QVector<QPainterPath> &paths, Operation op)
{
    QPainterPath result = paths.first();
    if (op == Operation::Subtract) {
        QPainterPath others = paths.at(1);
        for (qsizetype i = 2; i < paths.size(); ++i) others = others.united(paths.at(i));
        return result.subtracted(others);
    }
    for (qsizetype i = 1; i < paths.size(); ++i) {
        const QPainterPath &next = paths.at(i);
        if (op == Operation::Union) result = result.united(next);
        else if (op == Operation::Intersect) result = result.intersected(next);
        else if (op == Operation::Exclude) result = result.subtracted(next).united(next.subtracted(result));
    }
    return result;
}
} // namespace

std::optional<Operation> operation(const QString &name)
{
    if (name == QLatin1String("union")) return Operation::Union;
    if (name == QLatin1String("intersect")) return Operation::Intersect;
    if (name == QLatin1String("subtract")) return Operation::Subtract;
    if (name == QLatin1String("exclude")) return Operation::Exclude;
    if (name == QLatin1String("combine")) return Operation::Combine;
    return std::nullopt;
}

bool eligible(const CanvasObject &object)
{
    if (!CanvasGeometry::isRenderable(object)) return false;
    if (object.shape == CanvasShape::Rect || object.shape == CanvasShape::Ellipse) return true;
    if (object.shape != CanvasShape::Path || !object.pathClosed) return false;
    return std::all_of(object.extraContours.cbegin(), object.extraContours.cend(),
                       [](const CanvasPathContour &contour) { return contour.closed; });
}

QPainterPath area(const CanvasObject &object)
{
    QPainterPath local;
    if (object.shape == CanvasShape::Path) {
        local = CanvasPath::local(object);
    } else if (object.shape == CanvasShape::Ellipse) {
        local.addEllipse(object.rect);
    } else {
        local.addRect(object.rect);
    }
    QPainterPath mapped = object.transform.map(local);
    mapped.setFillRule(object.shape == CanvasShape::Path ? object.fillRule : Qt::WindingFill);
    return mapped;
}

QPainterPath flattened(const QPainterPath &path, qreal tolerance)
{
    QPainterPath out;
    out.setFillRule(path.fillRule());
    QPolygonF polygon;
    const auto flush = [&out, &polygon] {
        if (polygon.size() >= 2) {
            out.addPolygon(polygon);
            out.closeSubpath();
        }
        polygon.clear();
    };
    for (int i = 0; i < path.elementCount(); ++i) {
        const QPainterPath::Element e = path.elementAt(i);
        if (e.isMoveTo()) {
            flush();
            polygon << QPointF(e);
        } else if (e.isLineTo()) {
            polygon << QPointF(e);
        } else if (e.isCurveTo() && i + 2 < path.elementCount() && !polygon.isEmpty()) {
            flattenCubic(polygon, polygon.last(), QPointF(e), QPointF(path.elementAt(i + 1)),
                         QPointF(path.elementAt(i + 2)), tolerance, 0);
            i += 2;
        }
    }
    flush();
    return out;
}

std::optional<Result> combine(const QVector<const CanvasObject *> &operands, Operation op,
                              const QTransform &target, OperationError *error)
{
    const auto fail = [error](const OperationError &e) -> std::optional<Result> {
        if (error) *error = e;
        return std::nullopt;
    };
    if (error) *error = {};
    const auto toLocal = CanvasTransform::invert(target);
    if (operands.size() < 2 || !toLocal)
        return fail(failure(ErrorCode::InvalidArgument, QStringLiteral("Select at least two shapes to combine")));
    Result result;
    if (op == Operation::Combine) {
        // Contours keep their cubic handles: affine maps carry Béziers exactly.
        result.fillRule = operands.first()->fillRule;
        for (const CanvasObject *object : operands) {
            const QTransform map = object->transform * *toLocal;
            for (CanvasPathContour contour : CanvasPath::contours(*object)) {
                for (CanvasPathNode &node : contour.nodes)
                    node = {map.map(node.anchor), map.map(node.in), map.map(node.out)};
                result.contours.append(contour);
            }
        }
        return result;
    }
    QVector<QPainterPath> paths;
    QRectF extent;
    for (const CanvasObject *object : operands) {
        paths.append(area(*object));
        extent |= paths.last().boundingRect();
    }
    const qreal tolerance = std::max(1e-9, std::max(extent.width(), extent.height()) * RelativeTolerance);
    for (QPainterPath &path : paths) path = flattened(path, tolerance);
    const QPainterPath combined = fold(paths, op);
    for (const QPolygonF &polygon : combined.toSubpathPolygons())
        if (auto contour = contourFrom(polygon, *toLocal, tolerance * 1e-3)) result.contours.append(*contour);
    if (result.contours.isEmpty())
        return fail(failure(ErrorCode::Validation, QStringLiteral("The combined shape is empty")));
    result.fillRule = combined.fillRule();
    return result;
}

} // namespace QindaTK::CanvasPathBoolean
