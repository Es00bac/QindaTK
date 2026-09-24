// SPDX-License-Identifier: LGPL-3.0-or-later
#include "motion_composition.h"

#include "automation_model.h"
#include "timeline_time.h"

#include <QHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QSet>
#include <QTransform>
#include <algorithm>
#include <cmath>
#include <functional>
#include <optional>
#include <utility>

namespace QindaTK {
namespace {

std::optional<qint64> decimalTicks(const QVariant &value)
{
    return value.metaType().id() == QMetaType::QString ? TimelineTime::parseDecimal(value.toString())
                                                       : std::optional<qint64>();
}

// AGENT-NOTE: layer time = timeRemap(composition time - timeOffsetTicks).
// The remap is exact piecewise-linear over decimal tick pairs, floor-rounded
// once through TimelineTime::mulDiv, and holds outside its first/last pair,
// so freezes, reversals and speed ramps stay integer-exact at any tick.
std::optional<qint64> layerTicks(const QVariantMap &layer, qint64 ticks, QString *error)
{
    const QString id = layer.value(QStringLiteral("layerId")).toString();
    qint64 offset = 0;
    if (layer.contains(QStringLiteral("timeOffsetTicks"))) {
        const auto parsed = decimalTicks(layer.value(QStringLiteral("timeOffsetTicks")));
        if (!parsed) {
            *error = QStringLiteral("Layer %1 timeOffsetTicks must be an exact decimal tick string").arg(id);
            return std::nullopt;
        }
        offset = *parsed;
    }
    const auto shifted = TimelineTime::subtract(ticks, offset);
    if (!shifted) {
        *error = QStringLiteral("Layer %1 time offset overflows the tick range").arg(id);
        return std::nullopt;
    }
    if (!layer.contains(QStringLiteral("timeRemap"))) return shifted;
    const QVariant remap = layer.value(QStringLiteral("timeRemap"));
    const QVariantList rows = remap.toList();
    if (remap.metaType().id() != QMetaType::QVariantList || rows.isEmpty()
        || rows.size() > MotionComposition::kMaxRemapPairs) {
        *error = QStringLiteral("Layer %1 timeRemap needs 1 to %2 {compTicks, layerTicks} pairs")
                     .arg(id).arg(MotionComposition::kMaxRemapPairs);
        return std::nullopt;
    }
    QVector<std::pair<qint64, qint64>> pairs;
    pairs.reserve(rows.size());
    for (const QVariant &row : rows) {
        const QVariantMap pair = row.toMap();
        const auto from = decimalTicks(pair.value(QStringLiteral("compTicks")));
        const auto to = decimalTicks(pair.value(QStringLiteral("layerTicks")));
        if (!from || !to || (!pairs.isEmpty() && *from <= pairs.last().first)) {
            *error = QStringLiteral("Layer %1 timeRemap pairs need exact decimal ticks in strictly "
                                    "increasing compTicks order").arg(id);
            return std::nullopt;
        }
        pairs.push_back({*from, *to});
    }
    const qint64 t = *shifted;
    if (t <= pairs.first().first) return pairs.first().second;
    if (t >= pairs.last().first) return pairs.last().second;
    const auto right = std::upper_bound(pairs.cbegin(), pairs.cend(), t,
        [](qint64 value, const std::pair<qint64, qint64> &pair) { return value < pair.first; });
    const auto &a = *(right - 1);
    const auto &b = *right;
    const auto elapsed = TimelineTime::subtract(t, a.first);
    const auto span = TimelineTime::subtract(b.first, a.first);
    const auto delta = TimelineTime::subtract(b.second, a.second);
    const auto scaled = elapsed && span && delta
        ? TimelineTime::mulDiv(*delta, quint64(*elapsed), quint64(*span), TimeMap::Floor)
        : std::nullopt;
    const auto result = scaled ? TimelineTime::add(a.second, *scaled) : std::nullopt;
    if (!result) *error = QStringLiteral("Layer %1 timeRemap overflows the tick range").arg(id);
    return result;
}

struct LayerState {
    QVariantMap source;
    QString id;
    QString parentId;
    qint64 z = 0;
    qint64 localTicks = 0;
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
    if (!m_history.isEmpty()) {
        m_history.clear();
        m_cursor = 0;
        emit historyChanged();
    }
    replaceLayers(layers);
}

void MotionComposition::replaceLayers(const QVariantList &layers)
{
    if (m_layers == layers) return;
    m_layers = layers;
    ++m_revision;
    emit layersChanged();
    emit evaluationChanged();
}

void MotionComposition::setError(const QString &message)
{
    if (m_lastError == message) return;
    m_lastError = message;
    emit evaluationChanged();
}

QVariantMap MotionComposition::reject(const QString &message)
{
    setError(message);
    return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
}

QVariantMap MotionComposition::contentMap() const
{
    AutomationModel emptyModel;
    const QVariantMap animation = m_animationModel ? m_animationModel->contentMap() : emptyModel.contentMap();
    return {{QStringLiteral("schema"), schemaId()},
            {QStringLiteral("version"), kSchemaVersion},
            {QStringLiteral("animation"), animation},
            {QStringLiteral("layers"), m_layers}};
}

QString MotionComposition::layerRecordError(const QVariantList &layerRecords)
{
    QSet<QString> layerIds;
    qsizetype effectCount = 0;
    for (const QVariant &entry : layerRecords) {
        const QVariantMap layer = entry.toMap();
        if (!layer.contains(QStringLiteral("layerId"))
            || layer.value(QStringLiteral("layerId")).metaType().id() != QMetaType::QString)
            return tr("Layer IDs must be stable strings");
        const QString id = layer.value(QStringLiteral("layerId")).toString();
        if (id.isEmpty() || layerIds.contains(id))
            return tr("Layers need unique non-empty stable IDs");
        layerIds.insert(id);
        if (layer.contains(QStringLiteral("parentLayerId"))
            && layer.value(QStringLiteral("parentLayerId")).metaType().id() != QMetaType::QString)
            return tr("Layer parent IDs must be stable strings");
        if (layer.contains(QStringLiteral("sourceId"))
            && layer.value(QStringLiteral("sourceId")).metaType().id() != QMetaType::QString)
            return tr("Layer sourceId values must be stable strings");
        if (layer.contains(QStringLiteral("maskId"))
            && layer.value(QStringLiteral("maskId")).metaType().id() != QMetaType::QString)
            return tr("Layer maskId values must be stable strings");
        if (layer.contains(QStringLiteral("animatedProperties"))) {
            const QVariantMap animated = layer.value(QStringLiteral("animatedProperties")).toMap();
            static const QSet<QString> fields{QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("rotation"),
                QStringLiteral("scaleX"), QStringLiteral("scaleY"), QStringLiteral("opacity")};
            for (auto it = animated.cbegin(); it != animated.cend(); ++it) {
                if (!fields.contains(it.key()) || it.value().metaType().id() != QMetaType::QString || it.value().toString().isEmpty())
                    return tr("Animated property mappings need supported transform names and stable curve IDs");
            }
        }
        if (layer.contains(QStringLiteral("effects"))) {
            if (layer.value(QStringLiteral("effects")).metaType().id() != QMetaType::QVariantList)
                return tr("Layer effects must be an array of effect records");
            QSet<QString> effectIds;
            for (const QVariant &value : layer.value(QStringLiteral("effects")).toList()) {
                const QVariantMap effect = value.toMap();
                if (!effect.contains(QStringLiteral("effectId"))
                    || effect.value(QStringLiteral("effectId")).metaType().id() != QMetaType::QString)
                    return tr("Effect IDs must be stable strings");
                const QString effectId = effect.value(QStringLiteral("effectId")).toString();
                if (effectId.isEmpty() || effectIds.contains(effectId))
                    return tr("Effect records need unique non-empty effectId values per layer");
                if (effect.contains(QStringLiteral("parameters"))
                    && effect.value(QStringLiteral("parameters")).metaType().id() != QMetaType::QVariantMap)
                    return tr("Effect parameters must be an object");
                effectIds.insert(effectId);
                if (++effectCount > 100000)
                    return tr("A motion composition may contain at most 100000 effect records");
            }
        }
    }
    return {};
}

QVariantMap MotionComposition::loadContent(const QVariantMap &content)
{
    if (content.value(QStringLiteral("schema")).toString() != schemaId())
        return reject(tr("Unsupported motion composition schema"));
    if (!content.contains(QStringLiteral("version"))
        || content.value(QStringLiteral("version")).toInt() != kSchemaVersion)
        return reject(tr("Unsupported motion composition version"));
    const QVariantList layerRecords = content.value(QStringLiteral("layers")).toList();
    if (layerRecords.size() > 10000)
        return reject(tr("A motion composition may contain at most 10000 layers"));
    if (!content.value(QStringLiteral("layers")).canConvert<QVariantList>())
        return reject(tr("Motion composition layers must be an array"));

    const QVariantMap animationContent = content.value(QStringLiteral("animation")).toMap();
    if (animationContent.value(QStringLiteral("schema")).toString() != AutomationModel::schemaId())
        return reject(tr("Motion composition automation schema is missing or unsupported"));
    AutomationModel candidate;
    const QVariantMap animationResult = candidate.loadContent(animationContent);
    if (!animationResult.value(QStringLiteral("ok")).toBool())
        return reject(animationResult.value(QStringLiteral("error")).toString());

    if (const QString error = layerRecordError(layerRecords); !error.isEmpty()) return reject(error);

    // Validate hierarchy, transform maps and every animated property against a
    // detached candidate before replacing either host-owned part of the scene.
    MotionComposition validator;
    validator.setAnimationModel(&candidate);
    validator.setLayers(layerRecords);
    const QVariantMap evaluated = validator.evaluate(QStringLiteral("0"));
    if (!evaluated.value(QStringLiteral("ok")).toBool())
        return reject(evaluated.value(QStringLiteral("error")).toString());
    if (!m_animationModel && !animationContent.value(QStringLiteral("parameters")).toList().isEmpty())
        return reject(tr("Set a host AutomationModel before loading animated composition data"));
    if (m_animationModel) {
        const QVariantMap loaded = m_animationModel->loadContent(animationContent);
        if (!loaded.value(QStringLiteral("ok")).toBool())
            return reject(loaded.value(QStringLiteral("error")).toString());
    }
    setLayers(layerRecords);
    setError({});
    return {{QStringLiteral("ok"), true},
            {QStringLiteral("layerCount"), layerRecords.size()},
            {QStringLiteral("animationParameterCount"), animationContent.value(QStringLiteral("parameters")).toList().size()}};
}

QString MotionComposition::toJson() const
{
    return QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(contentMap())).toJson(QJsonDocument::Indented));
}

QVariantMap MotionComposition::loadJson(const QString &json)
{
    const QByteArray bytes = json.toUtf8();
    if (bytes.size() > 64 * 1024 * 1024)
        return reject(tr("Motion composition JSON exceeds the 64 MiB limit"));
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return reject(tr("Motion composition JSON is invalid: %1").arg(parseError.errorString()));
    return loadContent(document.object().toVariantMap());
}

bool MotionComposition::saveToFile(const QUrl &url)
{
    if (!url.isLocalFile()) {
        setError(tr("Motion composition save requires a local file URL"));
        return false;
    }
    const QByteArray bytes = QJsonDocument(QJsonObject::fromVariantMap(contentMap())).toJson(QJsonDocument::Indented);
    if (bytes.size() > 64 * 1024 * 1024) {
        setError(tr("Motion composition JSON exceeds the 64 MiB limit"));
        return false;
    }
    QSaveFile file(url.toLocalFile());
    if (!file.open(QIODevice::WriteOnly)) { setError(file.errorString()); return false; }
    if (file.write(bytes) != bytes.size()) {
        const QString message = file.errorString();
        file.cancelWriting();
        setError(message.isEmpty() ? tr("Could not write the complete motion composition") : message);
        return false;
    }
    if (!file.commit()) { setError(file.errorString()); return false; }
    setError({});
    return true;
}

QVariantMap MotionComposition::loadFromFile(const QUrl &url)
{
    if (!url.isLocalFile()) return reject(tr("Motion composition load requires a local file URL"));
    QFile file(url.toLocalFile());
    if (!file.open(QIODevice::ReadOnly)) return reject(file.errorString());
    if (file.size() > 64 * 1024 * 1024) return reject(tr("Motion composition file exceeds the 64 MiB limit"));
    const QByteArray bytes = file.readAll();
    if (file.error() != QFileDevice::NoError) return reject(file.errorString());
    return loadJson(QString::fromUtf8(bytes));
}

QVariantMap MotionComposition::evaluate(const QString &timeTicks)
{
    const auto exactTicks = TimelineTime::parseDecimal(timeTicks);
    if (!exactTicks) {
        return reject(QStringLiteral("Composition time must be a signed decimal tick string"));
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
            return reject(QStringLiteral("Layers need unique stable IDs and integer z indexes"));
        }
        indexes.insert(state.id, states.size());
        states.push_back(state);
    }
    for (const auto &state : std::as_const(states)) {
        if (!state.parentId.isEmpty() && !indexes.contains(state.parentId)) {
            return reject(QStringLiteral("Layer %1 has unknown parent %2").arg(state.id, state.parentId));
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

        const auto layerTime = layerTicks(state.source, *exactTicks, &failure);
        if (!layerTime) return false;
        state.localTicks = *layerTime;
        const QString localText = TimelineTime::toDecimal(*layerTime);
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
            const double sample = m_animationModel->valueAt(propertyId, localText);
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
            return reject(failure);
        }
        const auto &state = states.at(index);
        const auto &m = state.matrix;
        evaluated.push_back(QVariantMap{
            {QStringLiteral("layerId"), state.id},
            {QStringLiteral("parentLayerId"), state.parentId},
            {QStringLiteral("sourceId"), state.source.value(QStringLiteral("sourceId"))},
            {QStringLiteral("maskId"), state.source.value(QStringLiteral("maskId"))},
            {QStringLiteral("zIndex"), state.z},
            {QStringLiteral("localTicks"), TimelineTime::toDecimal(state.localTicks)},
            {QStringLiteral("visible"), state.visible},
            {QStringLiteral("opacity"), state.opacity},
            {QStringLiteral("effects"), state.source.value(QStringLiteral("effects"), QVariantList{})},
            {QStringLiteral("matrix"), QVariantList{m.m11(), m.m12(), m.m21(), m.m22(), m.dx(), m.dy()}}});
    }
    setError({});
    return {{QStringLiteral("ok"), true}, {QStringLiteral("timeTicks"), timeTicks},
            {QStringLiteral("layers"), evaluated}};
}

} // namespace QindaTK
