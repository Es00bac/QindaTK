// SPDX-License-Identifier: LGPL-3.0-or-later
#include "motion_composition.h"

#include "automation_model.h"
#include "timeline_time.h"

#include <QHash>
#include <QSet>
#include <QTransform>
#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>

namespace QindaTK {
namespace {

struct LayerState {
    QVariantMap source;
    QString id;
    QString parentId;
    qint64 z = 0;
    QTransform matrix;
    double opacity = 1.0;
    bool visible = true;
    bool ready = false;
};

bool finiteMap(const QVariantMap &map, QString *error)
{
    for (const QString &key : {QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("rotation"),
                                QStringLiteral("scaleX"), QStringLiteral("scaleY"), QStringLiteral("opacity")}) {
        if (!map.contains(key)) continue;
        bool ok = false;
        const double value = map.value(key).toDouble(&ok);
        if (!ok || !std::isfinite(value)) {
            *error = QStringLiteral("Layer transform %1 must be finite").arg(key);
            return false;
        }
    }
    return true;
}

QTransform compose(const QTransform &parent, const QTransform &local)
{
    // P∘L using Qt's affine mapping convention; spell out the products so the
    // composition order remains explicit and independent of QTransform API.
    const double a = parent.m11() * local.m11() + parent.m21() * local.m12();
    const double b = parent.m12() * local.m11() + parent.m22() * local.m12();
    const double c = parent.m11() * local.m21() + parent.m21() * local.m22();
    const double d = parent.m12() * local.m21() + parent.m22() * local.m22();
    const double tx = parent.m11() * local.dx() + parent.m21() * local.dy() + parent.dx();
    const double ty = parent.m12() * local.dx() + parent.m22() * local.dy() + parent.dy();
    return QTransform(a, b, 0.0, c, d, 0.0, tx, ty, 1.0);
}

} // namespace

MotionComposition::MotionComposition(QObject *parent) : QObject(parent) {}

void MotionComposition::setAnimationModel(AutomationModel *model)
{
    if (m_animationModel == model) return;
    m_animationModel = model;
    emit animationModelChanged();
    emit evaluationChanged();
}

void MotionComposition::setLayers(const QVariantList &layers)
{
    if (m_layers == layers) return;
    m_layers = layers;
    emit layersChanged();
    emit evaluationChanged();
}

QVariantMap MotionComposition::evaluate(const QString &timeTicks)
{
    const auto exactTicks = TimelineTime::parseDecimal(timeTicks);
    if (!exactTicks) {
        m_lastError = QStringLiteral("Composition time must be a signed decimal tick string");
        emit evaluationChanged();
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), m_lastError}};
    }

    QVector<LayerState> states;
    states.reserve(m_layers.size());
    QHash<QString, qsizetype> indexes;
    for (const QVariant &entry : m_layers) {
        LayerState state;
        state.source = entry.toMap();
        state.id = state.source.value(QStringLiteral("layerId")).toString();
        state.parentId = state.source.value(QStringLiteral("parentLayerId")).toString();
        bool zOk = true;
        state.z = state.source.value(QStringLiteral("zIndex"), 0).toLongLong(&zOk);
        if (state.id.isEmpty() || indexes.contains(state.id) || !zOk) {
            m_lastError = QStringLiteral("Layers need unique stable IDs and integer z indexes");
            emit evaluationChanged();
            return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), m_lastError}};
        }
        indexes.insert(state.id, states.size());
        states.push_back(state);
    }
    for (const auto &state : std::as_const(states)) {
        if (!state.parentId.isEmpty() && !indexes.contains(state.parentId)) {
            m_lastError = QStringLiteral("Layer %1 has unknown parent %2").arg(state.id, state.parentId);
            emit evaluationChanged();
            return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), m_lastError}};
        }
    }

    QVector<qsizetype> order(states.size());
    for (qsizetype i = 0; i < order.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](qsizetype a, qsizetype b) {
        if (states[a].z != states[b].z) return states[a].z < states[b].z;
        return states[a].id < states[b].id;
    });

    QSet<QString> visiting;
    QString failure;
    std::function<bool(qsizetype)> evaluateLayer = [&](qsizetype index) {
        LayerState &state = states[index];
        if (state.ready) return true;
        if (visiting.contains(state.id)) {
            failure = QStringLiteral("Composition layer hierarchy contains a cycle at %1").arg(state.id);
            return false;
        }
        visiting.insert(state.id);

        const QVariantMap transform = state.source.value(QStringLiteral("transform")).toMap();
        if (!finiteMap(transform, &failure)) return false;
        double values[6] = {transform.value(QStringLiteral("x"), 0.0).toDouble(),
                            transform.value(QStringLiteral("y"), 0.0).toDouble(),
                            transform.value(QStringLiteral("rotation"), 0.0).toDouble(),
                            transform.value(QStringLiteral("scaleX"), 1.0).toDouble(),
                            transform.value(QStringLiteral("scaleY"), 1.0).toDouble(),
                            transform.value(QStringLiteral("opacity"), 1.0).toDouble()};
        const QStringList fields{QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("rotation"),
                                 QStringLiteral("scaleX"), QStringLiteral("scaleY"), QStringLiteral("opacity")};
        const QVariantMap animated = state.source.value(QStringLiteral("animatedProperties")).toMap();
        for (qsizetype i = 0; i < fields.size(); ++i) {
            const QString propertyId = animated.value(fields.at(i)).toString();
            if (propertyId.isEmpty()) continue;
            if (!m_animationModel) {
                failure = QStringLiteral("Layer %1 animates %2 without an AnimationModel").arg(state.id, fields.at(i));
                return false;
            }
            const double sample = m_animationModel->valueAt(propertyId, timeTicks);
            if (!std::isfinite(sample)) {
                failure = QStringLiteral("Animation property %1 is unavailable").arg(propertyId);
                return false;
            }
            values[i] = sample;
        }
        if (!std::isfinite(values[5]) || values[5] < 0.0 || values[5] > 1.0) {
            failure = QStringLiteral("Layer %1 opacity must be between zero and one").arg(state.id);
            return false;
        }

        const double radians = values[2] * std::acos(-1.0) / 180.0;
        const double co = std::cos(radians), si = std::sin(radians);
        const QTransform local(co * values[3], si * values[3], 0.0,
                               -si * values[4], co * values[4], 0.0,
                               values[0], values[1], 1.0);
        state.matrix = local;
        state.opacity = values[5];
        state.visible = state.source.value(QStringLiteral("visible"), true).toBool();
        if (!state.parentId.isEmpty()) {
            const qsizetype parentIndex = indexes.value(state.parentId, -1);
            if (parentIndex < 0 || !evaluateLayer(parentIndex)) return false;
            const LayerState &parent = states[parentIndex];
            state.matrix = compose(parent.matrix, local);
            state.opacity *= parent.opacity;
            state.visible = state.visible && parent.visible;
        }
        visiting.remove(state.id);
        state.ready = true;
        return true;
    };

    QVariantList evaluated;
    evaluated.reserve(states.size());
    for (qsizetype z = 0; z < order.size(); ++z) {
        const qsizetype index = order.at(z);
        if (!evaluateLayer(index)) {
            m_lastError = failure;
            emit evaluationChanged();
            return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), m_lastError}};
        }
        const auto &state = states.at(index);
        const auto &m = state.matrix;
        evaluated.push_back(QVariantMap{
            {QStringLiteral("layerId"), state.id},
            {QStringLiteral("parentLayerId"), state.parentId},
            {QStringLiteral("sourceId"), state.source.value(QStringLiteral("sourceId"))},
            {QStringLiteral("zIndex"), state.z},
            {QStringLiteral("visible"), state.visible},
            {QStringLiteral("opacity"), state.opacity},
            {QStringLiteral("matrix"), QVariantList{m.m11(), m.m12(), m.m21(), m.m22(), m.dx(), m.dy()}}});
    }
    m_lastError.clear();
    emit evaluationChanged();
    return {{QStringLiteral("ok"), true}, {QStringLiteral("timeTicks"), timeTicks},
            {QStringLiteral("layers"), evaluated}};
}

} // namespace QindaTK
