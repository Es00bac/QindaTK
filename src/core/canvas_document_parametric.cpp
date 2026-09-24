// SPDX-License-Identifier: LGPL-3.0-or-later
#include "canvas_document.h"
#include "canvas_parametric.h"
#include <algorithm>
#include <cmath>

namespace QindaTK {
namespace {
// Applies a parameter patch to a copy of the definition. Each kind accepts
// only its own keys (polygon `sides`, star `points`/`innerRadius`, rounded
// rectangle `radius`) plus x/y/width/height; `kind` may repeat the current
// kind so parametricParameters() output can be passed back unchanged.
bool applyParameters(CanvasObject &o, const QVariantMap &values)
{
    const CanvasParametricKind kind = o.parametricKind;
    for (auto it = values.cbegin(); it != values.cend(); ++it) {
        const QString &key = it.key();
        if (key == QLatin1String("kind")) {
            if (it.value().toString() != CanvasParametric::id(kind)) return false;
            continue;
        }
        bool ok = false;
        const double value = it.value().toDouble(&ok);
        if (!ok || !std::isfinite(value)) return false;
        const bool countKey = (kind == CanvasParametricKind::Polygon && key == QLatin1String("sides"))
            || (kind == CanvasParametricKind::Star && key == QLatin1String("points"));
        if (key == QLatin1String("x")) o.rect.moveLeft(value);
        else if (key == QLatin1String("y")) o.rect.moveTop(value);
        else if (key == QLatin1String("width")) o.rect.setWidth(value);
        else if (key == QLatin1String("height")) o.rect.setHeight(value);
        else if (countKey) {
            if (value != std::floor(value) || value < CanvasParametric::MinimumPoints
                || value > CanvasParametric::MaximumPoints) return false;
            o.parametricPoints = quint32(value);
        } else if (key == QLatin1String("innerRadius") && kind == CanvasParametricKind::Star)
            o.parametricInnerRatio = value;
        else if (key == QLatin1String("radius") && kind == CanvasParametricKind::RoundedRect)
            o.parametricCornerRadius = value;
        else return false;
    }
    return CanvasParametric::valid(o);
}
}

qulonglong CanvasDocument::addParametricShape(const QString &kind, qreal x, qreal y,
                                               qreal width, qreal height,
                                               const QVariantMap &parameters,
                                               const QColor &fill, const QColor &stroke,
                                               qreal strokeWidth)
{
    CanvasObject object;
    object.shape = CanvasShape::Path;
    object.pathClosed = true;
    object.parametricKind = CanvasParametric::kind(kind);
    if (object.parametricKind == CanvasParametricKind::None) return 0;
    object.rect = QRectF(x, y, width, height);
    object.parametricPoints = object.parametricKind == CanvasParametricKind::Polygon ? 6
        : object.parametricKind == CanvasParametricKind::Star ? 5 : 0;
    object.parametricInnerRatio = object.parametricKind == CanvasParametricKind::Star ? 0.45 : 0;
    object.parametricCornerRadius = object.parametricKind == CanvasParametricKind::RoundedRect
        ? std::max(0.0, std::min({18.0, double(width) / 2, double(height) / 2})) : 0;
    object.fill = fill;
    object.stroke = stroke;
    object.strokeWidth = strokeWidth;
    if (!applyParameters(object, parameters)) return 0;
    object.pathNodes = CanvasParametric::nodes(object);
    return insert(object);
}

QVariantMap CanvasDocument::parametricParameters(qulonglong id) const
{
    const auto *o = object(id);
    if (!o || o->parametricKind == CanvasParametricKind::None) return {};
    QVariantMap values{{QStringLiteral("kind"), CanvasParametric::id(o->parametricKind)},
        {QStringLiteral("x"), o->rect.x()}, {QStringLiteral("y"), o->rect.y()},
        {QStringLiteral("width"), o->rect.width()}, {QStringLiteral("height"), o->rect.height()}};
    if (o->parametricKind == CanvasParametricKind::Polygon)
        values.insert(QStringLiteral("sides"), o->parametricPoints);
    else if (o->parametricKind == CanvasParametricKind::Star) {
        values.insert(QStringLiteral("points"), o->parametricPoints);
        values.insert(QStringLiteral("innerRadius"), o->parametricInnerRatio);
    } else values.insert(QStringLiteral("radius"), o->parametricCornerRadius);
    return values;
}

bool CanvasDocument::setParametricParameters(qulonglong id, const QVariantMap &parameters)
{
    const auto *before = object(id);
    if (!before || before->parametricKind == CanvasParametricKind::None
        || effectiveLocked(*before)) return false;
    CanvasObject after = *before;
    if (!applyParameters(after, parameters)) return false;
    after.pathNodes = CanvasParametric::nodes(after);
    if (after == *before) return true;
    CanvasEdit edit;
    edit.label = tr("Edit parametric shape");
    edit.changes.append({id, *before, after});
    return !commitEdit(edit);
}
} // namespace QindaTK
