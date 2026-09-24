// SPDX-License-Identifier: LGPL-3.0-or-later
#include "automation_model.h"

// The reference automation engine: read/write/touch/latch transitions and the
// record-gesture path. Every timestamp is an exact tick supplied by the caller
// (a transport or engine clock); nothing here reads a GUI frame clock.
//
//   mode   | transport running, control touched      | released            | transport stops
//   off    | live value only, nothing written        | -                   | -
//   read   | curve plays; gestures are live only     | -                   | -
//   touch  | writes from the touch                   | pass ends, returns  | pass ends
//   latch  | writes from the touch                   | holds last value    | pass ends
//   write  | writes from transport start, touched or not                   | pass ends
//
// A pass keeps the curve before its start exactly (an entry point holds the
// original value), replaces the points inside it, and ends with a ramp back to
// the original curve over `returnTicks`. One pass is one undo step.

namespace QindaTK {
namespace {

QVariantMap result(const OperationError &error, bool writing = false)
{
    if (!error) return {{QStringLiteral("ok"), true}, {QStringLiteral("writing"), writing}};
    return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), error.message}, {QStringLiteral("code"), int(error.code)}};
}

bool writes(AutomationMode mode)
{
    return mode == AutomationMode::Touch || mode == AutomationMode::Latch || mode == AutomationMode::Write;
}

CurveInterpolation recordedInterpolation(const AutomationParameter &parameter)
{
    return parameter.step > 0 || parameter.interpolation == CurveInterpolation::Step ? CurveInterpolation::Step
                                                                                     : CurveInterpolation::Linear;
}

} // namespace

OperationError AutomationModel::setMode(const QString &parameterId, AutomationMode mode)
{
    if (!m_lanes.contains(parameterId))
        return fail({ErrorCode::InvalidArgument, tr("Unknown parameter"), {}, QStringLiteral("automation")});
    QString reason;
    if (!modeAvailable(mode, &reason))
        return fail({ErrorCode::Unsupported, reason, QStringLiteral("mode"), QStringLiteral("automation")});
    if (m_lanes[parameterId].mode == mode) return {};
    if (m_lanes[parameterId].writing) finishPass(parameterId, m_lanes[parameterId].lastTicks);
    Lane &lane = m_lanes[parameterId];
    lane.mode = mode;
    lane.touched = false;
    lane.latched = false;
    if (mode == AutomationMode::Write && m_running) startPass(lane, m_transportTicks);
    emit modeChanged(parameterId);
    emit writeStateChanged(parameterId);
    return {};
}

void AutomationModel::startPass(Lane &lane, qint64 ticks)
{
    invalidateSessionFor(lane.parameter.id, tr("Automation recording started"));
    const double original = AutomationCurve::valueAt(lane.parameter, lane.points, ticks);
    if (!lane.touched) lane.lastValue = original;
    lane.writing = true;
    lane.passStart = ticks;
    lane.lastTicks = ticks;
    lane.pass.clear();
    AutomationPoint entry;
    entry.id = QUuid::createUuid();
    entry.ticks = ticks;
    entry.value = original;
    entry.interpolation = recordedInterpolation(lane.parameter);
    lane.pass.push_back(entry);
    appendSample(lane, ticks, lane.lastValue);
}

void AutomationModel::appendSample(Lane &lane, qint64 ticks, double value)
{
    const double v = AutomationCurve::quantized(lane.parameter, value);
    lane.lastValue = v;
    // The entry point owns the pass start; a sample there lands one tick later.
    const qint64 at = std::max(ticks, lane.pass.first().ticks + 1);
    AutomationPoint &last = lane.pass.last();
    if (lane.pass.size() > 1 && last.ticks >= at) {
        last.value = v;
    } else if (lane.pass.size() > 2 && last.value == v && lane.pass.at(lane.pass.size() - 2).value == v) {
        last.ticks = at; // extend a constant run instead of adding a point
    } else {
        AutomationPoint point;
        point.id = QUuid::createUuid();
        point.ticks = at;
        point.value = v;
        point.interpolation = recordedInterpolation(lane.parameter);
        lane.pass.push_back(point);
    }
    lane.lastTicks = std::max(lane.lastTicks, lane.pass.last().ticks);
}

QVector<AutomationPoint> AutomationModel::passResult(const Lane &lane, qint64 end, bool withReturn) const
{
    if (lane.pass.isEmpty()) return lane.points;
    QVector<AutomationPoint> written = lane.pass;
    const qint64 hold = std::max(end, written.last().ticks);
    if (hold > written.last().ticks) {
        AutomationPoint point = written.last();
        point.id = QUuid::createUuid();
        point.ticks = hold;
        written.push_back(point);
    }
    qint64 cut = hold;
    if (withReturn && m_returnTicks > 0) {
        if (const auto back = TimelineTime::add(hold, m_returnTicks)) {
            const double original = AutomationCurve::valueAt(lane.parameter, lane.points, *back);
            if (original != written.last().value) {
                AutomationPoint point;
                point.id = QUuid::createUuid();
                point.ticks = *back;
                point.value = original;
                point.interpolation = CurveInterpolation::Linear;
                for (const auto &existing : lane.points)
                    if (existing.ticks <= *back && existing.interpolation == CurveInterpolation::Step)
                        point.interpolation = CurveInterpolation::Step;
                    else if (existing.ticks <= *back)
                        point.interpolation = CurveInterpolation::Linear;
                written.push_back(point);
                cut = *back;
            }
        }
    }
    QVector<AutomationPoint> points;
    for (const auto &point : lane.points)
        if (point.ticks < lane.passStart || point.ticks > cut) points.push_back(point);
    points.append(written);
    AutomationCurve::sort(points);
    return points;
}

void AutomationModel::finishPass(const QString &parameterId, qint64 end)
{
    auto it = m_lanes.find(parameterId);
    if (it == m_lanes.end() || !it->writing) return;
    const QVector<AutomationPoint> next = passResult(*it, end, true);
    AutomationChangeSet changes = diff(parameterId, it->points, next);
    changes.transactionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    changes.label = tr("Record %1 automation").arg(it->parameter.name);
    it->writing = false;
    it->latched = false;
    it->pass.clear();
    if (!changes.changes.isEmpty()) record(changes);
    emit writeStateChanged(parameterId);
    emit pointsChanged(parameterId);
}

void AutomationModel::transportStarted(qint64 ticks)
{
    m_running = true;
    m_transportTicks = ticks;
    for (auto it = m_lanes.begin(); it != m_lanes.end(); ++it) {
        if (it->writing) continue;
        if (it->mode == AutomationMode::Write || (writes(it->mode) && it->touched)) {
            startPass(*it, ticks);
            emit writeStateChanged(it.key());
            emit pointsChanged(it.key());
        }
    }
    emit transportChanged();
}

void AutomationModel::transportStopped(qint64 ticks)
{
    for (const auto &id : std::as_const(m_order))
        if (m_lanes[id].writing) finishPass(id, std::max(ticks, m_lanes[id].lastTicks));
    m_running = false;
    m_transportTicks = ticks;
    emit transportChanged();
}

void AutomationModel::transportMoved(qint64 ticks)
{
    const qint64 previous = m_transportTicks;
    m_transportTicks = ticks;
    if (m_running) {
        for (const auto &id : std::as_const(m_order)) {
            Lane &lane = m_lanes[id];
            if (!lane.writing) continue;
            if (ticks < lane.lastTicks) {
                // A loop wrap or locate back ends this pass and starts another.
                finishPass(id, std::max(previous, m_lanes[id].lastTicks));
                Lane &again = m_lanes[id];
                if (again.mode == AutomationMode::Write || again.touched || again.mode == AutomationMode::Latch)
                    startPass(again, ticks);
            } else {
                lane.lastTicks = ticks;
            }
            emit writeStateChanged(id);
            emit pointsChanged(id);
        }
    }
    emit transportChanged();
}

OperationError AutomationModel::beginGesture(const QString &parameterId, qint64 ticks, double value)
{
    if (!m_lanes.contains(parameterId))
        return fail({ErrorCode::InvalidArgument, tr("Unknown parameter"), {}, QStringLiteral("automation")});
    Lane &lane = m_lanes[parameterId];
    lane.touched = true;
    lane.lastValue = AutomationCurve::quantized(lane.parameter, value);
    if (m_running && writes(lane.mode)) {
        if (!lane.writing) startPass(lane, ticks);
        else appendSample(lane, ticks, value);
        emit pointsChanged(parameterId);
    }
    emit writeStateChanged(parameterId);
    return {};
}

OperationError AutomationModel::recordGesture(const QString &parameterId, qint64 ticks, double value)
{
    if (!m_lanes.contains(parameterId))
        return fail({ErrorCode::InvalidArgument, tr("Unknown parameter"), {}, QStringLiteral("automation")});
    Lane &lane = m_lanes[parameterId];
    lane.lastValue = AutomationCurve::quantized(lane.parameter, value);
    if (lane.writing) {
        if (ticks < lane.lastTicks) {
            finishPass(parameterId, lane.lastTicks);
            startPass(m_lanes[parameterId], ticks);
        }
        appendSample(m_lanes[parameterId], ticks, value);
        emit pointsChanged(parameterId);
    }
    emit writeStateChanged(parameterId);
    return {};
}

OperationError AutomationModel::endGesture(const QString &parameterId, qint64 ticks)
{
    if (!m_lanes.contains(parameterId))
        return fail({ErrorCode::InvalidArgument, tr("Unknown parameter"), {}, QStringLiteral("automation")});
    Lane &lane = m_lanes[parameterId];
    lane.touched = false;
    if (lane.writing && lane.mode == AutomationMode::Touch) {
        finishPass(parameterId, std::max(ticks, lane.lastTicks));
    } else if (lane.writing && lane.mode == AutomationMode::Latch) {
        lane.latched = true;
        lane.lastTicks = std::max(lane.lastTicks, ticks);
    }
    emit writeStateChanged(parameterId);
    return {};
}

// ---- QML wrappers ----

QVariantMap AutomationModel::setModeName(const QString &parameterId, const QString &mode)
{
    const auto value = AutomationCodec::modeFromName(mode);
    if (!value) return result(fail({ErrorCode::InvalidArgument, tr("Unknown automation mode"), {}, {}}));
    return result(setMode(parameterId, *value));
}

QVariantMap AutomationModel::writeState(const QString &parameterId) const
{
    const Lane *found = lane(parameterId);
    if (!found) return {};
    QString reason;
    const bool available = modeAvailable(found->mode, &reason);
    return {{QStringLiteral("mode"), AutomationCodec::modeName(found->mode)},
            {QStringLiteral("touched"), found->touched},
            {QStringLiteral("latched"), found->latched},
            {QStringLiteral("writing"), found->writing},
            {QStringLiteral("liveValue"), found->lastValue},
            {QStringLiteral("passStart"), QString::number(found->passStart)},
            {QStringLiteral("available"), available},
            {QStringLiteral("reason"), reason}};
}

void AutomationModel::setTransportState(bool running, const QString &ticks)
{
    const auto value = TimelineTime::parseDecimal(ticks);
    if (!value) return;
    if (running && !m_running) transportStarted(*value);
    else if (!running && m_running) transportStopped(*value);
    else transportMoved(*value);
}

void AutomationModel::setTransportTicks(const QString &ticks)
{
    if (const auto value = TimelineTime::parseDecimal(ticks)) transportMoved(*value);
}

QVariantMap AutomationModel::beginGestureAt(const QString &parameterId, const QString &ticks, double value)
{
    const auto at = TimelineTime::parseDecimal(ticks);
    if (!at) return result({ErrorCode::InvalidArgument, tr("ticks must be an exact decimal string"), {}, {}});
    const OperationError error = beginGesture(parameterId, *at, value);
    return result(error, !error && m_lanes[parameterId].writing);
}

QVariantMap AutomationModel::recordGestureAt(const QString &parameterId, const QString &ticks, double value)
{
    const auto at = TimelineTime::parseDecimal(ticks);
    if (!at) return result({ErrorCode::InvalidArgument, tr("ticks must be an exact decimal string"), {}, {}});
    const OperationError error = recordGesture(parameterId, *at, value);
    return result(error, !error && m_lanes[parameterId].writing);
}

QVariantMap AutomationModel::endGestureAt(const QString &parameterId, const QString &ticks)
{
    const auto at = TimelineTime::parseDecimal(ticks);
    if (!at) return result({ErrorCode::InvalidArgument, tr("ticks must be an exact decimal string"), {}, {}});
    const OperationError error = endGesture(parameterId, *at);
    return result(error, !error && m_lanes[parameterId].writing);
}

} // namespace QindaTK
