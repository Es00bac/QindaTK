// SPDX-License-Identifier: LGPL-3.0-or-later
#include "automation_model.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <algorithm>
#include <cmath>

namespace QindaTK {
namespace {

QVariantMap failure(const OperationError &error)
{
    return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), error.message},
            {QStringLiteral("code"), int(error.code)}, {QStringLiteral("field"), error.fieldPath}};
}

OperationError argument(const QString &message)
{
    return {ErrorCode::InvalidArgument, message, {}, QStringLiteral("automation")};
}

} // namespace

AutomationModel::AutomationModel(QObject *parent)
    : QObject(parent)
{
}

void AutomationModel::setReturnTicksText(const QString &ticks)
{
    const auto value = TimelineTime::parseDecimal(ticks);
    if (!value || *value < 0 || *value == m_returnTicks) return;
    m_returnTicks = *value;
    emit settingsChanged();
}

OperationError AutomationModel::setTimebase(TimelineRate timebase)
{
    if (!timebase.isValid()) return fail(argument(tr("The timebase must be a positive rational rate")));
    for (const auto &lane : std::as_const(m_lanes))
        if (!lane.points.isEmpty()) return fail(argument(tr("Change the timebase before adding points")));
    m_timebase = timebase;
    emit contentReset();
    return {};
}

const AutomationModel::Lane *AutomationModel::lane(const QString &parameterId) const
{
    const auto it = m_lanes.constFind(parameterId);
    return it == m_lanes.cend() ? nullptr : &*it;
}

QVector<AutomationPoint> AutomationModel::effectivePoints(const QString &parameterId) const
{
    const Lane *found = lane(parameterId);
    if (!found) return {};
    if (found->writing) return passResult(*found, std::max(found->lastTicks, found->passStart), false);
    if (m_session && m_session->request.parameterId == parameterId && m_session->hasPreview && !m_session->error)
        return m_session->preview;
    return found->points;
}

bool AutomationModel::addParameter(const AutomationParameter &parameter, OperationError *error)
{
    OperationError failed = AutomationCurve::validateParameter(parameter);
    if (!failed && m_lanes.contains(parameter.id))
        failed = argument(tr("Parameter “%1” already exists").arg(parameter.id));
    if (error) *error = failed;
    if (failed) return !fail(failed);
    Lane lane;
    lane.parameter = parameter;
    lane.lastValue = parameter.defaultValue;
    m_lanes.insert(parameter.id, lane);
    m_order.push_back(parameter.id);
    emit parametersChanged();
    return true;
}

QVariantMap AutomationModel::addParameterMap(const QVariantMap &map)
{
    QString error;
    const auto parameter = AutomationCodec::parameterFromMap(map, &error);
    if (!parameter) return failure(fail(argument(error)));
    OperationError failed;
    if (!addParameter(*parameter, &failed)) return failure(failed);
    if (map.contains(QStringLiteral("mode"))) setModeName(parameter->id, map.value(QStringLiteral("mode")).toString());
    return {{QStringLiteral("ok"), true}};
}

bool AutomationModel::removeParameter(const QString &parameterId)
{
    if (!m_lanes.contains(parameterId)) return false;
    invalidateSessionFor(parameterId, tr("The parameter was removed"));
    m_lanes.remove(parameterId);
    m_order.removeAll(parameterId);
    // Inverse data naming a removed lane can never replay.
    m_history.clear();
    m_cursor = 0;
    ++m_revision;
    emit parametersChanged();
    emit historyChanged();
    emit revisionChanged();
    return true;
}

QVariantMap AutomationModel::parameterData(const QString &parameterId) const
{
    const Lane *found = lane(parameterId);
    if (!found) return {};
    QVariantMap map = AutomationCodec::toMap(found->parameter);
    map.insert(QStringLiteral("mode"), AutomationCodec::modeName(found->mode));
    map.insert(QStringLiteral("pointCount"), int(found->points.size()));
    return map;
}

QVariantMap AutomationModel::pointRow(const AutomationParameter &parameter, const AutomationPoint &point) const
{
    QVariantMap map = AutomationCodec::toMap(point);
    map.insert(QStringLiteral("normalizedValue"), AutomationCurve::normalized(parameter, point.value));
    map.insert(QStringLiteral("valueText"), formatValue(parameter.id, point.value));
    return map;
}

QVariantList AutomationModel::points(const QString &parameterId) const
{
    const Lane *found = lane(parameterId);
    QVariantList rows;
    if (!found) return rows;
    const auto points = effectivePoints(parameterId);
    rows.reserve(points.size());
    for (const auto &point : points) rows.push_back(pointRow(found->parameter, point));
    return rows;
}

QVariantMap AutomationModel::pointData(const QString &parameterId, const QString &pointId) const
{
    const Lane *found = lane(parameterId);
    if (!found) return {};
    for (const auto &point : effectivePoints(parameterId))
        if (point.id == QUuid(pointId)) return pointRow(found->parameter, point);
    return {};
}

QString AutomationModel::adjacentPoint(const QString &parameterId, const QString &pointId, int direction) const
{
    const auto points = effectivePoints(parameterId);
    if (points.isEmpty()) return {};
    qsizetype index = -1;
    for (qsizetype i = 0; i < points.size(); ++i)
        if (points.at(i).id == QUuid(pointId)) index = i;
    const qsizetype next = index < 0 ? (direction < 0 ? points.size() - 1 : 0)
                                     : std::clamp<qsizetype>(index + (direction < 0 ? -1 : 1), 0, points.size() - 1);
    return points.at(next).id.toString(QUuid::WithoutBraces);
}

QString AutomationModel::segmentAt(const QString &parameterId, const QString &ticksText) const
{
    const auto ticks = TimelineTime::parseDecimal(ticksText);
    const auto points = effectivePoints(parameterId);
    if (!ticks) return {};
    for (qsizetype i = 0; i + 1 < points.size(); ++i)
        if (points.at(i).ticks <= *ticks && *ticks < points.at(i + 1).ticks)
            return points.at(i).id.toString(QUuid::WithoutBraces);
    return {};
}

double AutomationModel::valueAt(const QString &parameterId, const QString &ticksText) const
{
    const Lane *found = lane(parameterId);
    const auto ticks = TimelineTime::parseDecimal(ticksText);
    if (!found || !ticks) return qQNaN();
    return AutomationCurve::valueAt(found->parameter, effectivePoints(parameterId), *ticks);
}

double AutomationModel::normalize(const QString &parameterId, double value) const
{
    const Lane *found = lane(parameterId);
    return found ? AutomationCurve::normalized(found->parameter, value) : qQNaN();
}

double AutomationModel::denormalize(const QString &parameterId, double y) const
{
    const Lane *found = lane(parameterId);
    if (!found) return qQNaN();
    const auto &range = found->parameter.range;
    return AutomationCurve::quantized(found->parameter, std::lerp(range.minimum, range.maximum, y));
}

QString AutomationModel::formatValue(const QString &parameterId, double value) const
{
    const Lane *found = lane(parameterId);
    if (!found || !std::isfinite(value)) return {};
    const auto &parameter = found->parameter;
    const double span = parameter.range.maximum - parameter.range.minimum;
    const int decimals = parameter.step > 0 ? std::clamp(int(std::ceil(-std::log10(parameter.step))), 0, 6)
                                            : std::clamp(3 - int(std::floor(std::log10(span))), 0, 6);
    const QString unit = parameter.range.unitId == QLatin1String("normalized") ? QString() : parameter.range.unitId;
    const QString number = QString::number(value, 'f', decimals);
    return unit.isEmpty() ? number : number + QLatin1Char(' ') + unit;
}

QVariantMap AutomationModel::parseValue(const QString &parameterId, const QString &text) const
{
    const Lane *found = lane(parameterId);
    if (!found) return failure(argument(tr("Unknown parameter")));
    QString number = text.trimmed();
    const QString unit = found->parameter.range.unitId;
    if (number.endsWith(unit, Qt::CaseInsensitive)) number.chop(unit.size());
    bool ok = false;
    const double value = number.trimmed().toDouble(&ok);
    const auto &range = found->parameter.range;
    if (!ok || !std::isfinite(value)) return failure(argument(tr("Enter a number")));
    if (value < range.minimum || value > range.maximum)
        return failure(argument(tr("Enter %1 to %2").arg(formatValue(parameterId, range.minimum),
                                                        formatValue(parameterId, range.maximum))));
    return {{QStringLiteral("ok"), true}, {QStringLiteral("value"), AutomationCurve::quantized(found->parameter, value)}};
}

QVariantList AutomationModel::sampleLane(const QString &parameterId, const QString &startText,
                                         const QString &endText, int columns) const
{
    const Lane *found = lane(parameterId);
    const auto start = TimelineTime::parseDecimal(startText);
    const auto end = TimelineTime::parseDecimal(endText);
    QVariantList samples;
    if (!found || !start || !end || *end <= *start) return samples;
    const auto points = effectivePoints(parameterId);
    const auto &parameter = found->parameter;
    const double span = double(*end - *start);
    const auto push = [&](qint64 ticks, double value) {
        samples.push_back(QVariantMap{{QStringLiteral("x"), double(ticks - *start) / span},
                                      {QStringLiteral("y"), AutomationCurve::normalized(parameter, value)}});
    };
    // Exact point positions are always included so steps and corners stay
    // crisp; column samples fill curved segments between them.
    const int count = std::clamp(columns, 2, 4096);
    qsizetype next = std::upper_bound(points.cbegin(), points.cend(), *start,
                                      [](qint64 t, const AutomationPoint &p) { return t < p.ticks; }) - points.cbegin();
    for (int i = 0; i <= count; ++i) {
        const qint64 ticks = i == count ? *end : *start + qint64(span * i / count);
        while (next < points.size() && points.at(next).ticks <= ticks) {
            const auto &point = points.at(next);
            if (point.ticks > *start && point.ticks < *end) {
                if (next > 0 && points.at(next - 1).interpolation == CurveInterpolation::Step)
                    push(point.ticks, points.at(next - 1).value);
                push(point.ticks, point.value);
            }
            ++next;
        }
        push(ticks, AutomationCurve::valueAt(parameter, points, ticks));
    }
    return samples;
}

// ---- modes and capabilities (state; the record path is in automation_record.cpp) ----

bool AutomationModel::modeAvailable(AutomationMode mode, QString *reason) const
{
    const auto it = m_unavailable.constFind(int(mode));
    if (it == m_unavailable.cend()) return true;
    if (reason) *reason = *it;
    return false;
}

void AutomationModel::setModeAvailable(AutomationMode mode, bool available, const QString &reason)
{
    if (mode == AutomationMode::Off || mode == AutomationMode::Read) return; // always supported
    if (available) m_unavailable.remove(int(mode));
    else m_unavailable.insert(int(mode), reason.isEmpty() ? tr("The automation engine does not support this mode")
                                                          : reason);
    emit capabilitiesChanged();
    for (auto it = m_lanes.begin(); it != m_lanes.end(); ++it)
        if (!available && it->mode == mode) setMode(it.key(), AutomationMode::Read);
}

void AutomationModel::setModeAvailability(const QString &mode, bool available, const QString &reason)
{
    if (const auto value = AutomationCodec::modeFromName(mode)) setModeAvailable(*value, available, reason);
}

QVariantMap AutomationModel::capabilities() const
{
    QVariantMap result;
    for (auto mode : {AutomationMode::Off, AutomationMode::Read, AutomationMode::Touch, AutomationMode::Latch,
                      AutomationMode::Write}) {
        QString reason;
        const bool available = modeAvailable(mode, &reason);
        result.insert(AutomationCodec::modeName(mode),
                      QVariantMap{{QStringLiteral("available"), available}, {QStringLiteral("reason"), reason}});
    }
    return result;
}

OperationError AutomationModel::fail(OperationError error)
{
    m_lastError = error.message;
    emit lastErrorChanged();
    emit rejected(error.message);
    return error;
}

// ---- persistence ----

QVariantMap AutomationModel::contentMap() const
{
    QVariantList parameters;
    for (const auto &id : m_order) {
        const Lane &lane = *m_lanes.constFind(id);
        QVariantMap map = AutomationCodec::toMap(lane.parameter);
        map.insert(QStringLiteral("mode"), AutomationCodec::modeName(lane.mode));
        QVariantList points;
        for (const auto &point : lane.points) points.push_back(AutomationCodec::toMap(point));
        map.insert(QStringLiteral("points"), points);
        parameters.push_back(map);
    }
    return {{QStringLiteral("schema"), schemaId()},
            {QStringLiteral("timebase"), QVariantMap{{QStringLiteral("numerator"), timebaseNumeratorText()},
                                                     {QStringLiteral("denominator"), timebaseDenominatorText()}}},
            {QStringLiteral("returnTicks"), returnTicksText()},
            {QStringLiteral("parameters"), parameters}};
}

QVariantMap AutomationModel::loadContent(const QVariantMap &content)
{
    const auto reject = [this](const QString &message) { return failure(fail(argument(message))); };
    if (content.contains(QStringLiteral("schema")) && content.value(QStringLiteral("schema")).toString() != schemaId())
        return reject(tr("Unsupported automation schema"));
    TimelineRate timebase = m_timebase;
    if (content.contains(QStringLiteral("timebase"))) {
        const QVariantMap rate = content.value(QStringLiteral("timebase")).toMap();
        const auto numerator = TimelineTime::parseDecimal(rate.value(QStringLiteral("numerator")).toString());
        const auto denominator = TimelineTime::parseDecimal(rate.value(QStringLiteral("denominator")).toString());
        const auto normalized = numerator && denominator ? TimelineRate::normalized(*numerator, *denominator)
                                                         : std::nullopt;
        if (!normalized) return reject(tr("timebase needs decimal-string numerator and denominator"));
        timebase = *normalized;
    }
    QHash<QString, Lane> lanes;
    QStringList order;
    for (const auto &value : content.value(QStringLiteral("parameters")).toList()) {
        const QVariantMap map = value.toMap();
        QString error;
        const auto parameter = AutomationCodec::parameterFromMap(map, &error);
        if (!parameter) return reject(error);
        if (lanes.contains(parameter->id)) return reject(tr("Parameter IDs must be unique"));
        Lane lane;
        lane.parameter = *parameter;
        lane.lastValue = parameter->defaultValue;
        const auto mode = AutomationCodec::modeFromName(map.value(QStringLiteral("mode"), QStringLiteral("read")).toString());
        if (!mode) return reject(tr("Unknown automation mode"));
        lane.mode = modeAvailable(*mode) ? *mode : AutomationMode::Read;
        for (const auto &pointValue : map.value(QStringLiteral("points")).toList()) {
            const auto point = AutomationCodec::pointFromMap(pointValue.toMap(), &error);
            if (!point) return reject(error);
            lane.points.push_back(*point);
        }
        AutomationCurve::sort(lane.points);
        if (auto failed = AutomationCurve::validate(lane.parameter, lane.points))
            return reject(tr("%1: %2").arg(parameter->id, failed.message));
        lanes.insert(parameter->id, lane);
        order.push_back(parameter->id);
    }
    if (m_session) cancel(m_session->operationId);
    if (content.contains(QStringLiteral("returnTicks"))) setReturnTicksText(content.value(QStringLiteral("returnTicks")).toString());
    m_timebase = timebase;
    m_lanes = lanes;
    m_order = order;
    m_history.clear();
    m_cursor = 0;
    ++m_revision;
    emit contentReset();
    emit parametersChanged();
    for (const auto &id : std::as_const(m_order)) emit pointsChanged(id);
    emit historyChanged();
    emit revisionChanged();
    return {{QStringLiteral("ok"), true}, {QStringLiteral("revision"), revisionText()}};
}

QString AutomationModel::toJson() const
{
    return QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(contentMap())).toJson(QJsonDocument::Indented));
}

QVariantMap AutomationModel::loadJson(const QString &json)
{
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(json.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return failure(fail(argument(tr("The automation document is not a JSON object"))));
    return loadContent(document.object().toVariantMap());
}

} // namespace QindaTK
