// SPDX-License-Identifier: LGPL-3.0-or-later
// MotionComposition layer-record transactions and spatial path edits.
#include "motion_composition.h"

#include "automation_model.h"
#include "timeline_time.h"

#include <QPointF>
#include <QTransform>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <optional>

namespace QindaTK {
namespace {

struct PathContext {
    qint64 localTicks = 0;
    QTransform parent;  // parent world matrix; identity for a root layer
    QTransform inverse; // world -> parent space
    QPointF local;      // evaluated parent-space position
    QString curves[2];  // animated x / y curve IDs, empty when static
};

QTransform matrixFrom(const QVariant &value)
{
    const QVariantList m = value.toList();
    if (m.size() != 6) return {};
    return QTransform(m.at(0).toDouble(), m.at(1).toDouble(), 0.0, m.at(2).toDouble(), m.at(3).toDouble(), 0.0,
                      m.at(4).toDouble(), m.at(5).toDouble(), 1.0);
}

QVariantMap pointMap(const QPointF &point)
{
    return {{QStringLiteral("x"), point.x()}, {QStringLiteral("y"), point.y()}};
}

qsizetype keyAt(const QVector<AutomationPoint> &points, qint64 ticks)
{
    for (qsizetype i = 0; i < points.size(); ++i)
        if (points.at(i).ticks == ticks) return i;
    return -1;
}

// Resolves the layer at exact composition time: its layer-local ticks, the
// parent's world matrix and the curves animating its translation.
QString resolvePath(MotionComposition &composition, const QString &layerId, const QString &timeTicks,
                    PathContext *context)
{
    if (!composition.animationModel()) return MotionComposition::tr("Path edits need an AutomationModel");
    const QVariantMap evaluated = composition.evaluate(timeTicks);
    if (!evaluated.value(QStringLiteral("ok")).toBool()) return evaluated.value(QStringLiteral("error")).toString();
    const QVariantList rows = evaluated.value(QStringLiteral("layers")).toList();
    QVariantMap layer;
    for (const QVariant &row : rows)
        if (row.toMap().value(QStringLiteral("layerId")).toString() == layerId) layer = row.toMap();
    if (layer.isEmpty()) return MotionComposition::tr("Unknown layer %1").arg(layerId);
    const QString parentId = layer.value(QStringLiteral("parentLayerId")).toString();
    for (const QVariant &row : rows)
        if (!parentId.isEmpty() && row.toMap().value(QStringLiteral("layerId")).toString() == parentId)
            context->parent = matrixFrom(row.toMap().value(QStringLiteral("matrix")));
    bool invertible = false;
    context->inverse = context->parent.inverted(&invertible);
    if (!invertible) return MotionComposition::tr("The parent transform of %1 is not invertible").arg(layerId);
    context->localTicks = TimelineTime::parseDecimal(layer.value(QStringLiteral("localTicks")).toString()).value_or(0);
    const QTransform world = matrixFrom(layer.value(QStringLiteral("matrix")));
    context->local = context->inverse.map(QPointF(world.dx(), world.dy()));
    for (const QVariant &row : composition.layers()) {
        const QVariantMap record = row.toMap();
        if (record.value(QStringLiteral("layerId")).toString() != layerId) continue;
        const QVariantMap animated = record.value(QStringLiteral("animatedProperties")).toMap();
        context->curves[0] = animated.value(QStringLiteral("x")).toString();
        context->curves[1] = animated.value(QStringLiteral("y")).toString();
    }
    if (context->curves[0].isEmpty() && context->curves[1].isEmpty())
        return MotionComposition::tr("Layer %1 has no animated position").arg(layerId);
    return {};
}

} // namespace

QVariantMap MotionComposition::commitLayers(const QVariantList &after, const QString &label)
{
    const LayerEdit edit{QUuid::createUuid().toString(QUuid::WithoutBraces), label, m_layers, after};
    m_history.resize(m_cursor);
    m_history.push_back(edit);
    if (m_history.size() > kHistoryLimit) m_history.removeFirst();
    m_cursor = m_history.size();
    replaceLayers(after);
    setError({});
    emit historyChanged();
    emit layerEditCommitted(edit.transactionId, label);
    return {{QStringLiteral("ok"), true}, {QStringLiteral("transactionId"), edit.transactionId},
            {QStringLiteral("revision"), revisionText()}};
}

QVariantMap MotionComposition::insertEffect(const QString &layerId, const QVariantMap &effect, int index,
                                            const QString &expectedRevision)
{
    if (!expectedRevision.isEmpty() && expectedRevision != revisionText())
        return reject(tr("The composition changed before the effect was inserted"));
    const QVariant effectId = effect.value(QStringLiteral("effectId"));
    if (effectId.metaType().id() != QMetaType::QString || effectId.toString().isEmpty())
        return reject(tr("Effect IDs must be stable strings"));
    if (effect.contains(QStringLiteral("enabled"))
        && effect.value(QStringLiteral("enabled")).metaType().id() != QMetaType::Bool)
        return reject(tr("Effect enabled flags must be booleans"));
    QVariantList after = m_layers;
    for (qsizetype i = 0; i < after.size(); ++i) {
        QVariantMap layer = after.at(i).toMap();
        if (layer.value(QStringLiteral("layerId")).toString() != layerId) continue;
        QVariantList effects = layer.value(QStringLiteral("effects")).toList();
        if (index < -1 || index > effects.size())
            return reject(tr("The effect index is outside the layer's effect list"));
        effects.insert(index < 0 ? effects.size() : index, effect);
        layer.insert(QStringLiteral("effects"), effects);
        after[i] = layer;
        if (const QString error = layerRecordError(after); !error.isEmpty()) return reject(error);
        return commitLayers(after, tr("Insert %1 effect").arg(effectId.toString()));
    }
    return reject(tr("Unknown layer %1").arg(layerId));
}

bool MotionComposition::undo()
{
    if (!canUndo()) return false;
    const QVariantList before = m_history.at(m_cursor - 1).before;
    --m_cursor;
    replaceLayers(before);
    emit historyChanged();
    return true;
}

bool MotionComposition::redo()
{
    if (!canRedo()) return false;
    const QVariantList after = m_history.at(m_cursor).after;
    ++m_cursor;
    replaceLayers(after);
    emit historyChanged();
    return true;
}

QVariantMap MotionComposition::pathHandles(const QString &layerId, const QString &timeTicks)
{
    PathContext context;
    if (const QString error = resolvePath(*this, layerId, timeTicks, &context); !error.isEmpty())
        return reject(error);
    QPointF in = context.local, out = context.local;
    bool hasKey = false, hasIn = false, hasOut = false;
    for (int axis = 0; axis < 2; ++axis) {
        const QString &curve = context.curves[axis];
        if (curve.isEmpty()) continue;
        const QVector<AutomationPoint> points = m_animationModel->effectivePoints(curve);
        const qsizetype i = keyAt(points, context.localTicks);
        if (i < 0) continue;
        hasKey = true;
        const double here = m_animationModel->normalize(curve, points.at(i).value);
        // Unset handles show CurveEvaluator's defaults (1/3 and 2/3 along the segment).
        if (i > 0) {
            hasIn = true;
            const double y = points.at(i).inHandle ? points.at(i).inHandle->y
                : std::lerp(m_animationModel->normalize(curve, points.at(i - 1).value), here, 2.0 / 3.0);
            (axis ? in.ry() : in.rx()) = m_animationModel->denormalize(curve, y);
        }
        if (i + 1 < points.size()) {
            hasOut = true;
            const double y = points.at(i).outHandle ? points.at(i).outHandle->y
                : std::lerp(here, m_animationModel->normalize(curve, points.at(i + 1).value), 1.0 / 3.0);
            (axis ? out.ry() : out.rx()) = m_animationModel->denormalize(curve, y);
        }
    }
    const QString localText = TimelineTime::toDecimal(context.localTicks);
    if (!hasKey) return reject(tr("Layer %1 has no position key at %2 layer ticks").arg(layerId, localText));
    QVariantMap result{{QStringLiteral("ok"), true}, {QStringLiteral("layerId"), layerId},
                       {QStringLiteral("timeTicks"), timeTicks}, {QStringLiteral("localTicks"), localText},
                       {QStringLiteral("position"), pointMap(context.parent.map(context.local))}};
    if (hasIn) result.insert(QStringLiteral("inHandle"), pointMap(context.parent.map(in)));
    if (hasOut) result.insert(QStringLiteral("outHandle"), pointMap(context.parent.map(out)));
    setError({});
    return result;
}

QVariantMap MotionComposition::movePathKey(const QString &layerId, const QString &timeTicks,
                                           double worldX, double worldY)
{
    if (!std::isfinite(worldX) || !std::isfinite(worldY)) return reject(tr("Path positions must be finite"));
    PathContext context;
    if (const QString error = resolvePath(*this, layerId, timeTicks, &context); !error.isEmpty())
        return reject(error);
    const QPointF target = context.inverse.map(QPointF(worldX, worldY));
    QVector<AutomationBatchEdit> edits;
    for (int axis = 0; axis < 2; ++axis) {
        const QString &curve = context.curves[axis];
        if (curve.isEmpty()) continue;
        const AutomationModel::Lane *lane = m_animationModel->lane(curve);
        if (!lane) return reject(tr("Animation property %1 is unavailable").arg(curve));
        const auto &range = lane->parameter.range;
        AutomationBatchEdit edit;
        edit.request.parameterId = curve;
        edit.input.value = std::clamp(axis ? target.y() : target.x(), range.minimum, range.maximum);
        const qsizetype i = keyAt(lane->points, context.localTicks);
        if (i >= 0) {
            edit.request.kind = AutomationEditKind::SetPoint;
            edit.request.anchorPointId = lane->points.at(i).id;
        } else {
            edit.request.kind = AutomationEditKind::AddPoint;
            edit.input.ticks = context.localTicks;
            edit.input.interpolation = lane->parameter.interpolation;
        }
        edits.push_back(edit);
    }
    QString transactionId;
    QStringList created;
    if (const OperationError error = m_animationModel->applyBatch(edits, tr("Move motion path key"),
                                                                  &transactionId, &created))
        return reject(error.message);
    setError({});
    return {{QStringLiteral("ok"), true}, {QStringLiteral("transactionId"), transactionId},
            {QStringLiteral("localTicks"), TimelineTime::toDecimal(context.localTicks)},
            {QStringLiteral("createdIds"), created}};
}

QVariantMap MotionComposition::setPathTangent(const QString &layerId, const QString &timeTicks,
                                              const QString &side, double worldX, double worldY)
{
    const bool out = side == QLatin1String("out");
    if (!out && side != QLatin1String("in")) return reject(tr("A tangent side must be \"in\" or \"out\""));
    if (!std::isfinite(worldX) || !std::isfinite(worldY)) return reject(tr("Path positions must be finite"));
    PathContext context;
    if (const QString error = resolvePath(*this, layerId, timeTicks, &context); !error.isEmpty())
        return reject(error);
    const QPointF target = context.inverse.map(QPointF(worldX, worldY));
    // AGENT-NOTE: one shared time-handle x keeps both axes on the same Bezier
    // parameter, so the spatial segment is exactly the 2D curve through the
    // edited controls (sampled by tst_motion_editing).
    std::optional<double> handleX;
    QVector<AutomationBatchEdit> edits;
    for (int axis = 0; axis < 2; ++axis) {
        const QString &curve = context.curves[axis];
        if (curve.isEmpty()) continue;
        const AutomationModel::Lane *lane = m_animationModel->lane(curve);
        if (!lane) return reject(tr("Animation property %1 is unavailable").arg(curve));
        const qsizetype i = keyAt(lane->points, context.localTicks);
        if (i < 0 || (out ? i + 1 >= lane->points.size() : i == 0)) continue;
        const auto &existing = out ? lane->points.at(i).outHandle : lane->points.at(i).inHandle;
        if (!handleX && existing) handleX = existing->x;
        AutomationBatchEdit edit;
        edit.request.kind = AutomationEditKind::SetHandle;
        edit.request.parameterId = curve;
        edit.request.anchorPointId = lane->points.at(i).id;
        edit.request.side = side;
        edit.input.handle = AutomationHandle{0.0, m_animationModel->normalize(curve, axis ? target.y() : target.x())};
        edits.push_back(edit);
    }
    if (edits.isEmpty())
        return reject(tr("Layer %1 has no %2 path segment at %3 layer ticks")
                          .arg(layerId, side, TimelineTime::toDecimal(context.localTicks)));
    for (auto &edit : edits) edit.input.handle->x = handleX.value_or(out ? 1.0 / 3.0 : 2.0 / 3.0);
    QString transactionId;
    if (const OperationError error = m_animationModel->applyBatch(edits, tr("Edit motion path tangent"),
                                                                  &transactionId))
        return reject(error.message);
    setError({});
    return {{QStringLiteral("ok"), true}, {QStringLiteral("transactionId"), transactionId},
            {QStringLiteral("localTicks"), TimelineTime::toDecimal(context.localTicks)}};
}

} // namespace QindaTK
