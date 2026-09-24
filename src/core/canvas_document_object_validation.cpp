// SPDX-License-Identifier: LGPL-3.0-or-later
#include "canvas_document_format.h"
#include "canvas_transform.h"
#include "canvas_managed_paint.h"
#include <cmath>
#include <limits>

namespace QindaTK::CanvasDocumentFormat {
namespace {
OperationError problem(ErrorCode code, const QString &message, const QString &field)
{ return {code, message, field, QStringLiteral("canvas.format")}; }
bool finite(const QRectF &r)
{ return std::isfinite(r.x()) && std::isfinite(r.y()) && std::isfinite(r.width()) && std::isfinite(r.height()); }
bool validColor(const QColor &color)
{
    const int spec = int(color.spec());
    if (spec < int(QColor::Invalid) || spec > int(QColor::ExtendedRgb)) return false;
    if (spec == int(QColor::Invalid)) return true;
    float r = 0, g = 0, b = 0, a = 0;
    color.getRgbF(&r, &g, &b, &a);
    return std::isfinite(r) && std::isfinite(g) && std::isfinite(b) && std::isfinite(a);
}
constexpr quint64 idCeiling = IdCeiling;
}

OperationError validateObject(const CanvasObject &o, CanvasObjectId id, const CanvasDocumentLimits &limits)
{
    if (id == 0 || id >= idCeiling || o.id != id)
        return problem(ErrorCode::InvalidArgument, QStringLiteral("Invalid object identity"), QStringLiteral("objects.id"));
    if (quint8(o.shape) > quint8(CanvasShape::Path))
        return problem(ErrorCode::Validation, QStringLiteral("Unknown object shape"), QStringLiteral("objects.shape"));
    if (o.layer == 0)
        return problem(ErrorCode::Validation, QStringLiteral("Object has no layer"), QStringLiteral("objects.layer"));
    if (!finite(o.rect) || !CanvasTransform::isFinite(o.transform) || !std::isfinite(o.strokeWidth)
        || o.strokeWidth < 0)
        return problem(ErrorCode::Validation, QStringLiteral("Object geometry must be finite"), QStringLiteral("objects.geometry"));
    if (o.points.size() + o.pathPointCount() > limits.maximumPointsPerObject)
        return problem(ErrorCode::Unsupported, QStringLiteral("Too many points in one object"), QStringLiteral("objects.points"));
    for (const QPointF &point : o.points)
        if (!std::isfinite(point.x()) || !std::isfinite(point.y()))
            return problem(ErrorCode::Validation, QStringLiteral("Object geometry must be finite"), QStringLiteral("objects.points"));
    if (o.shape == CanvasShape::Path) {
        if (!o.points.isEmpty() || o.pathNodes.size() < (o.pathClosed ? 3 : 2))
            return problem(ErrorCode::Validation, QStringLiteral("Path needs ordered nodes"), QStringLiteral("objects.pathNodes"));
        for (const auto &node : o.pathNodes)
            for (const QPointF &p : {node.anchor, node.in, node.out})
                if (!std::isfinite(p.x()) || !std::isfinite(p.y()))
                    return problem(ErrorCode::Validation, QStringLiteral("Path coordinates must be finite"), QStringLiteral("objects.pathNodes"));
        for (const auto &contour : o.extraContours) {
            if (contour.nodes.size() < (contour.closed ? 3 : 2))
                return problem(ErrorCode::Validation, QStringLiteral("Contour needs ordered nodes"), QStringLiteral("objects.extraContours"));
            for (const auto &node : contour.nodes)
                for (const QPointF &p : {node.anchor, node.in, node.out})
                    if (!std::isfinite(p.x()) || !std::isfinite(p.y()))
                        return problem(ErrorCode::Validation, QStringLiteral("Contour coordinates must be finite"), QStringLiteral("objects.extraContours"));
        }
    } else if (!o.pathNodes.isEmpty() || o.pathClosed || !o.extraContours.isEmpty())
        return problem(ErrorCode::Validation, QStringLiteral("Only paths own path nodes"), QStringLiteral("objects.pathNodes"));
    if (o.fillRule != Qt::OddEvenFill && o.fillRule != Qt::WindingFill)
        return problem(ErrorCode::Validation, QStringLiteral("Invalid fill rule"), QStringLiteral("objects.fillRule"));
    if (o.strokeCap != Qt::FlatCap && o.strokeCap != Qt::RoundCap && o.strokeCap != Qt::SquareCap)
        return problem(ErrorCode::Validation, QStringLiteral("Invalid stroke cap"), QStringLiteral("objects.strokeCap"));
    if (o.strokeJoin != Qt::MiterJoin && o.strokeJoin != Qt::RoundJoin && o.strokeJoin != Qt::BevelJoin)
        return problem(ErrorCode::Validation, QStringLiteral("Invalid stroke join"), QStringLiteral("objects.strokeJoin"));
    if (o.dashPattern.size() > 4096 || o.dashPattern.size() % 2)
        return problem(ErrorCode::Validation, QStringLiteral("Invalid dash pattern"), QStringLiteral("objects.dashPattern"));
    for (qreal dash : o.dashPattern)
        if (!std::isfinite(dash) || dash <= 0 || dash > 1e6)
            return problem(ErrorCode::Validation, QStringLiteral("Invalid dash length"), QStringLiteral("objects.dashPattern"));
    if (o.name.size() > limits.maximumNameLength)
        return problem(ErrorCode::Unsupported, QStringLiteral("Object name is too long"), QStringLiteral("objects.name"));
    if (o.z >= PaintOrderCeiling || o.z == std::numeric_limits<qint64>::min())
        return problem(ErrorCode::Validation, QStringLiteral("Object paint order is out of range"), QStringLiteral("objects.z"));
    if ((o.shape == CanvasShape::Image) != (o.image != 0))
        return problem(ErrorCode::Validation, QStringLiteral("Only image objects reference an image"), QStringLiteral("objects.image"));
    if (!validColor(o.fill) || !validColor(o.stroke))
        return problem(ErrorCode::Validation, QStringLiteral("Invalid object colour"), QStringLiteral("objects.colour"));
    if (!o.managedFill.isEmpty()) {
        if (o.shape == CanvasShape::Image || o.shape == CanvasShape::Polyline
            || (o.shape == CanvasShape::Path && !o.hasClosedContour()))
            return problem(ErrorCode::Unsupported, QStringLiteral("Managed fills require a closed vector object"), QStringLiteral("objects.managedFill"));
        if (const auto error = CanvasManagedPaint::validate(o.managedFill)) return error;
    }
    return {};
}

} // namespace QindaTK::CanvasDocumentFormat
