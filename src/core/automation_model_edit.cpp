// SPDX-License-Identifier: LGPL-3.0-or-later
#include "automation_model.h"

#include <QSet>
#include <algorithm>
#include <limits>

namespace QindaTK {
namespace {

OperationError argument(const QString &message)
{
    return {ErrorCode::InvalidArgument, message, {}, QStringLiteral("automation")};
}

OperationError invalid(const QString &message)
{
    return {ErrorCode::Validation, message, {}, QStringLiteral("automation")};
}

AutomationEditFields merged(AutomationEditFields base, const AutomationEditFields &input)
{
    if (input.ticks) base.ticks = input.ticks;
    if (input.value) base.value = input.value;
    if (input.interpolation) base.interpolation = input.interpolation;
    if (input.handle) base.handle = input.handle;
    if (input.clearHandle) base.clearHandle = true;
    if (input.minimum) base.minimum = input.minimum;
    if (input.maximum) base.maximum = input.maximum;
    if (input.defaultValue) base.defaultValue = input.defaultValue;
    return base;
}

QString defaultLabel(AutomationEditKind kind)
{
    switch (kind) {
    case AutomationEditKind::MovePoints: return AutomationModel::tr("Move automation points");
    case AutomationEditKind::AddPoint: return AutomationModel::tr("Add automation point");
    case AutomationEditKind::RemovePoints: return AutomationModel::tr("Delete automation points");
    case AutomationEditKind::SetPoint: return AutomationModel::tr("Edit automation point");
    case AutomationEditKind::MoveSegment: return AutomationModel::tr("Move automation segment");
    case AutomationEditKind::SetHandle: return AutomationModel::tr("Edit curve handle");
    case AutomationEditKind::SetInterpolation: return AutomationModel::tr("Change interpolation");
    case AutomationEditKind::EditParameter: return AutomationModel::tr("Change parameter range");
    }
    return {};
}

qsizetype indexOf(const QVector<AutomationPoint> &points, const QUuid &id)
{
    for (qsizetype i = 0; i < points.size(); ++i)
        if (points.at(i).id == id) return i;
    return -1;
}

bool needsAnchor(AutomationEditKind kind)
{
    return kind == AutomationEditKind::SetPoint || kind == AutomationEditKind::MoveSegment
        || kind == AutomationEditKind::SetHandle;
}

// Moves a set rigidly: time is clamped so no point crosses an unmoved
// neighbour or zero; one point takes its exact target value, several shift
// by the anchor's value delta clamped to the range.
OperationError planMove(const AutomationParameter &parameter, QVector<AutomationPoint> &points,
                        QSet<QUuid> moving, const QUuid &anchorId, const AutomationEditFields &f)
{
    const qsizetype a = indexOf(points, anchorId);
    if (a < 0) return argument(AutomationModel::tr("Unknown automation point"));
    moving.insert(anchorId);
    const AutomationPoint anchor = points.at(a);
    qint64 low = std::numeric_limits<qint64>::min(), high = std::numeric_limits<qint64>::max();
    double lowest = parameter.range.maximum, highest = parameter.range.minimum;
    for (qsizetype i = 0; i < points.size(); ++i) {
        if (!moving.contains(points.at(i).id)) continue;
        lowest = std::min(lowest, points.at(i).value);
        highest = std::max(highest, points.at(i).value);
        low = std::max(low, -points.at(i).ticks);
        if (i > 0 && !moving.contains(points.at(i - 1).id))
            low = std::max(low, points.at(i - 1).ticks + 1 - points.at(i).ticks);
        if (i + 1 < points.size() && !moving.contains(points.at(i + 1).id))
            high = std::min(high, points.at(i + 1).ticks - 1 - points.at(i).ticks);
    }
    if (low > high) return invalid(AutomationModel::tr("The points have no room to move"));
    const auto wanted = f.ticks ? TimelineTime::subtract(*f.ticks, anchor.ticks) : std::optional<qint64>(0);
    if (!wanted) return argument(AutomationModel::tr("The time is out of range"));
    const qint64 dt = std::clamp(*wanted, low, high);
    const bool single = moving.size() == 1;
    const double dv = f.value ? std::clamp(*f.value - anchor.value, parameter.range.minimum - lowest,
                                           parameter.range.maximum - highest) : 0.0;
    for (auto &point : points) {
        if (!moving.contains(point.id)) continue;
        point.ticks += dt;
        if (single && f.value) point.value = AutomationCurve::quantized(parameter, *f.value);
        else if (dv != 0.0) point.value = AutomationCurve::quantized(parameter, point.value + dv);
    }
    return {};
}

OperationError planHandle(QVector<AutomationPoint> &points, qsizetype i, const QString &side,
                          const AutomationEditFields &f)
{
    const bool out = side == QLatin1String("out");
    if (!out && side != QLatin1String("in")) return argument(AutomationModel::tr("side must be \"in\" or \"out\""));
    if ((out && i + 1 >= points.size()) || (!out && i == 0))
        return invalid(AutomationModel::tr("No segment on that side of the point"));
    auto &target = out ? points[i].outHandle : points[i].inHandle;
    if (f.clearHandle) {
        target.reset();
        return {};
    }
    if (!f.handle) return argument(AutomationModel::tr("A handle edit needs x and y"));
    AutomationHandle handle{std::clamp(f.handle->x, 0.0, 1.0), std::clamp(f.handle->y, 0.0, 1.0)};
    // Keep the cubic's controls ordered in x, as CurveEvaluator requires.
    if (out) handle.x = std::min(handle.x, points.at(i + 1).inHandle ? points.at(i + 1).inHandle->x : 2.0 / 3.0);
    else handle.x = std::max(handle.x, points.at(i - 1).outHandle ? points.at(i - 1).outHandle->x : 1.0 / 3.0);
    target = handle;
    points[out ? i : i - 1].interpolation = CurveInterpolation::CubicBezier;
    return {};
}

} // namespace

QString AutomationModel::begin(const AutomationEditRequest &request, OperationError *error)
{
    const auto reject = [&](OperationError failed) {
        if (error) *error = failed;
        fail(failed);
        return QString();
    };
    if (m_session) return reject({ErrorCode::Conflict, tr("Another automation edit is in progress"), {}, {}});
    if (request.expectedRevision && *request.expectedRevision != m_revision)
        return reject({ErrorCode::StaleRevision, tr("The automation changed before the edit began"), {}, {}});
    const Lane *found = lane(request.parameterId);
    if (!found) return reject(argument(tr("Unknown parameter")));
    if (found->writing) return reject({ErrorCode::Conflict, tr("The parameter is being recorded"), {}, {}});
    if (needsAnchor(request.kind) && indexOf(found->points, request.anchorPointId) < 0)
        return reject(argument(tr("Unknown automation point")));
    for (const auto &id : request.pointIds)
        if (indexOf(found->points, id) < 0) return reject(argument(tr("Unknown automation point")));
    Session session;
    session.operationId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    session.request = request;
    if (session.request.label.isEmpty()) session.request.label = defaultLabel(request.kind);
    session.baseRevision = m_revision;
    if (request.kind == AutomationEditKind::AddPoint) session.createdId = QUuid::createUuid();
    m_session = session;
    if (error) *error = {};
    emit editStateChanged();
    return m_session->operationId;
}

OperationError AutomationModel::plan(Session &session, const AutomationEditFields &input) const
{
    const auto &request = session.request;
    const Lane *found = lane(request.parameterId);
    if (!found) return argument(tr("Unknown parameter"));
    const AutomationEditFields f = merged(request.fields, input);
    const AutomationParameter &parameter = found->parameter;
    QVector<AutomationPoint> points = found->points;
    const qsizetype anchor = indexOf(points, request.anchorPointId);
    session.parameter.reset();
    switch (request.kind) {
    case AutomationEditKind::MovePoints: {
        QSet<QUuid> moving(request.pointIds.cbegin(), request.pointIds.cend());
        const QUuid anchorId = request.anchorPointId.isNull() && !request.pointIds.isEmpty()
                                   ? request.pointIds.first() : request.anchorPointId;
        if (auto error = planMove(parameter, points, moving, anchorId, f)) return error;
        break;
    }
    case AutomationEditKind::AddPoint: {
        if (!f.ticks || !f.value) return argument(tr("A new point needs ticks and a value"));
        for (const auto &point : std::as_const(points))
            if (point.ticks == *f.ticks) return invalid(tr("A point already exists at this time"));
        AutomationPoint point;
        point.id = session.createdId;
        point.ticks = *f.ticks;
        point.value = AutomationCurve::quantized(parameter, *f.value);
        point.interpolation = f.interpolation.value_or(parameter.interpolation);
        points.push_back(point);
        AutomationCurve::sort(points);
        break;
    }
    case AutomationEditKind::RemovePoints: {
        const QSet<QUuid> removed(request.pointIds.cbegin(), request.pointIds.cend());
        points.removeIf([&](const AutomationPoint &point) { return removed.contains(point.id); });
        break;
    }
    case AutomationEditKind::SetPoint: {
        AutomationPoint &point = points[anchor];
        if (f.ticks) {
            if ((anchor > 0 && *f.ticks <= points.at(anchor - 1).ticks)
                || (anchor + 1 < points.size() && *f.ticks >= points.at(anchor + 1).ticks))
                return invalid(tr("The time must stay between the neighbouring points"));
            point.ticks = *f.ticks;
        }
        if (f.value) {
            if (*f.value < parameter.range.minimum || *f.value > parameter.range.maximum)
                return invalid(tr("The value lies outside the parameter range"));
            point.value = AutomationCurve::quantized(parameter, *f.value);
        }
        if (f.interpolation) point.interpolation = *f.interpolation;
        break;
    }
    case AutomationEditKind::MoveSegment: {
        if (anchor + 1 >= points.size()) return invalid(tr("No segment starts at this point"));
        AutomationPoint &left = points[anchor];
        AutomationPoint &right = points[anchor + 1];
        const double dv = f.value ? std::clamp(*f.value - left.value,
                                               parameter.range.minimum - std::min(left.value, right.value),
                                               parameter.range.maximum - std::max(left.value, right.value)) : 0.0;
        if (dv != 0.0) {
            left.value = AutomationCurve::quantized(parameter, left.value + dv);
            right.value = AutomationCurve::quantized(parameter, right.value + dv);
        }
        break;
    }
    case AutomationEditKind::SetHandle:
        if (auto error = planHandle(points, anchor, request.side, f)) return error;
        break;
    case AutomationEditKind::SetInterpolation: {
        if (!f.interpolation) return argument(tr("Choose an interpolation"));
        const QSet<QUuid> targets(request.pointIds.cbegin(), request.pointIds.cend());
        for (auto &point : points)
            if (targets.contains(point.id) || point.id == request.anchorPointId) point.interpolation = *f.interpolation;
        break;
    }
    case AutomationEditKind::EditParameter: {
        AutomationParameter next = parameter;
        if (f.minimum) next.range.minimum = *f.minimum;
        if (f.maximum) next.range.maximum = *f.maximum;
        if (f.defaultValue) next.defaultValue = *f.defaultValue;
        if (next.range.maximum > next.range.minimum)
            next.defaultValue = std::clamp(next.defaultValue, next.range.minimum, next.range.maximum);
        if (auto error = AutomationCurve::validateParameter(next)) return error;
        // Canonical values are kept; only values outside the new range move,
        // to its nearest bound, inside the same undo step.
        for (auto &point : points) point.value = std::clamp(point.value, next.range.minimum, next.range.maximum);
        session.parameter = next;
        break;
    }
    }
    session.preview = points;
    return AutomationCurve::validate(session.parameter.value_or(parameter), points);
}

OperationError AutomationModel::update(const QString &operationId, const AutomationEditFields &input)
{
    if (!m_session || m_session->operationId != operationId || operationId.isEmpty())
        return argument(tr("No such automation edit"));
    if (m_session->baseRevision != m_revision)
        return {ErrorCode::StaleRevision, tr("The automation changed during the edit"), {}, {}};
    m_session->error = plan(*m_session, input);
    m_session->hasPreview = true;
    emit pointsChanged(m_session->request.parameterId);
    return m_session->error;
}

AutomationChangeSet AutomationModel::diff(const QString &parameterId, const QVector<AutomationPoint> &before,
                                          const QVector<AutomationPoint> &after)
{
    AutomationChangeSet changes;
    QHash<QUuid, qsizetype> next;
    for (qsizetype i = 0; i < after.size(); ++i) next.insert(after.at(i).id, i);
    QSet<QUuid> seen;
    for (const auto &point : before) {
        seen.insert(point.id);
        const auto it = next.constFind(point.id);
        if (it == next.cend()) changes.changes.push_back({parameterId, point.id, point, std::nullopt, {}, {}});
        else if (!(after.at(*it) == point))
            changes.changes.push_back({parameterId, point.id, point, after.at(*it), {}, {}});
    }
    for (const auto &point : after)
        if (!seen.contains(point.id)) changes.changes.push_back({parameterId, point.id, std::nullopt, point, {}, {}});
    return changes;
}

OperationError AutomationModel::commit(const QString &operationId, QString *transactionId, QStringList *createdIds)
{
    if (createdIds) createdIds->clear();
    if (!m_session || m_session->operationId != operationId || operationId.isEmpty())
        return fail(argument(tr("No such automation edit")));
    if (m_session->baseRevision != m_revision || !m_session->hasPreview || m_session->error) {
        const OperationError error = m_session->baseRevision != m_revision
            ? OperationError{ErrorCode::StaleRevision, tr("The automation changed during the edit"), {}, {}}
            : m_session->hasPreview ? m_session->error : argument(tr("The automation edit has no change to commit"));
        cancel(operationId);
        return fail(error);
    }
    const Session session = *m_session;
    const Lane &current = m_lanes[session.request.parameterId];
    AutomationChangeSet changes = diff(session.request.parameterId, current.points, session.preview);
    if (session.parameter && !(*session.parameter == current.parameter))
        changes.changes.prepend({session.request.parameterId, {}, {}, {}, current.parameter, session.parameter});
    changes.transactionId = operationId;
    changes.label = session.request.label;
    m_session.reset();
    emit editStateChanged();
    if (changes.changes.isEmpty()) {
        emit pointsChanged(session.request.parameterId);
        return {};
    }
    if (auto error = record(changes)) return error;
    if (transactionId) *transactionId = operationId;
    if (createdIds && !session.createdId.isNull()) createdIds->push_back(session.createdId.toString(QUuid::WithoutBraces));
    return {};
}

void AutomationModel::cancel(const QString &operationId)
{
    if (!m_session || m_session->operationId != operationId) return;
    const QString parameterId = m_session->request.parameterId;
    m_session.reset();
    emit editStateChanged();
    emit pointsChanged(parameterId);
}

OperationError AutomationModel::applyBatch(const QVector<AutomationBatchEdit> &edits, const QString &label,
                                           QString *transactionId, QStringList *createdIds)
{
    if (transactionId) transactionId->clear();
    if (createdIds) createdIds->clear();
    if (m_session) return fail({ErrorCode::Conflict, tr("Another automation edit is in progress"), {}, {}});
    if (edits.isEmpty()) return fail(argument(tr("An automation batch needs at least one edit")));
    AutomationChangeSet changes;
    QStringList created;
    QSet<QString> touched;
    for (const auto &edit : edits) {
        const AutomationEditRequest &request = edit.request;
        if (touched.contains(request.parameterId))
            return fail(argument(tr("An automation batch edits each parameter at most once")));
        touched.insert(request.parameterId);
        if (request.expectedRevision && *request.expectedRevision != m_revision)
            return fail({ErrorCode::StaleRevision, tr("The automation changed before the edit began"), {}, {}});
        const Lane *found = lane(request.parameterId);
        if (!found) return fail(argument(tr("Unknown parameter")));
        if (found->writing) return fail({ErrorCode::Conflict, tr("The parameter is being recorded"), {}, {}});
        if (needsAnchor(request.kind) && indexOf(found->points, request.anchorPointId) < 0)
            return fail(argument(tr("Unknown automation point")));
        for (const auto &id : request.pointIds)
            if (indexOf(found->points, id) < 0) return fail(argument(tr("Unknown automation point")));
        Session session;
        session.request = request;
        session.baseRevision = m_revision;
        if (request.kind == AutomationEditKind::AddPoint) session.createdId = QUuid::createUuid();
        if (auto error = plan(session, edit.input)) return fail(error);
        if (session.parameter && !(*session.parameter == found->parameter))
            changes.changes.push_back({request.parameterId, {}, {}, {}, found->parameter, session.parameter});
        changes.changes += diff(request.parameterId, found->points, session.preview).changes;
        if (!session.createdId.isNull()) created.push_back(session.createdId.toString(QUuid::WithoutBraces));
    }
    if (changes.changes.isEmpty()) return {};
    changes.transactionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    changes.label = label.isEmpty() ? tr("Edit automation") : label;
    if (auto error = record(changes)) return error;
    if (transactionId) *transactionId = changes.transactionId;
    if (createdIds) *createdIds = created;
    return {};
}

void AutomationModel::invalidateSessionFor(const QString &parameterId, const QString &reason)
{
    if (!m_session || m_session->request.parameterId != parameterId) return;
    const QString operation = m_session->operationId;
    cancel(operation);
    emit editInvalidated(operation, reason);
}

OperationError AutomationModel::record(AutomationChangeSet changes)
{
    if (auto error = applyChanges(changes, false)) return fail(error);
    m_history.resize(m_cursor);
    m_history.push_back(changes);
    ++m_historySerial;
    if (m_history.size() > kHistoryLimit) m_history.removeFirst();
    m_cursor = m_history.size();
    ++m_revision;
    emit historyChanged();
    emit revisionChanged();
    emit committed(changes.transactionId, changes.label);
    return {};
}

OperationError AutomationModel::applyChanges(const AutomationChangeSet &changes, bool reverse)
{
    QHash<QString, QVector<AutomationPoint>> points;
    QHash<QString, AutomationParameter> parameters;
    const auto conflict = [] {
        return OperationError{ErrorCode::Conflict, tr("The automation no longer matches this step"), {}, {}};
    };
    for (const auto &change : changes.changes) {
        const auto it = m_lanes.constFind(change.parameterId);
        if (it == m_lanes.cend()) return conflict();
        if (!points.contains(change.parameterId)) points.insert(change.parameterId, it->points);
        if (change.pointId.isNull()) {
            const auto &expected = reverse ? change.parameterAfter : change.parameterBefore;
            if (!expected || !(*expected == parameters.value(change.parameterId, it->parameter))) return conflict();
            parameters.insert(change.parameterId, *(reverse ? change.parameterBefore : change.parameterAfter));
            continue;
        }
        auto &list = points[change.parameterId];
        const qsizetype index = indexOf(list, change.pointId);
        const auto &expected = reverse ? change.after : change.before;
        const auto &target = reverse ? change.before : change.after;
        if ((index >= 0) != expected.has_value() || (index >= 0 && !(list.at(index) == *expected))) return conflict();
        if (target && index >= 0) list[index] = *target;
        else if (target) list.push_back(*target);
        else list.removeAt(index);
    }
    for (auto it = points.begin(); it != points.end(); ++it) {
        AutomationCurve::sort(it.value());
        const Lane &current = m_lanes[it.key()];
        if (auto error = AutomationCurve::validate(parameters.value(it.key(), current.parameter), it.value()))
            return error;
    }
    for (auto it = points.cbegin(); it != points.cend(); ++it) {
        Lane &target = m_lanes[it.key()];
        target.points = it.value();
        if (parameters.contains(it.key())) target.parameter = parameters.value(it.key());
        emit pointsChanged(it.key());
    }
    if (!parameters.isEmpty()) emit parametersChanged();
    return {};
}

bool AutomationModel::undo()
{
    if (m_session) cancel(m_session->operationId);
    if (!canUndo()) return false;
    for (const auto &lane : std::as_const(m_lanes))
        if (lane.writing) return !fail(invalid(tr("Stop recording before undoing")));
    if (auto error = applyChanges(m_history.at(m_cursor - 1), true)) return !fail(error);
    --m_cursor;
    ++m_revision;
    emit historyChanged();
    emit revisionChanged();
    return true;
}

bool AutomationModel::redo()
{
    if (m_session) cancel(m_session->operationId);
    if (!canRedo()) return false;
    for (const auto &lane : std::as_const(m_lanes))
        if (lane.writing) return !fail(invalid(tr("Stop recording before redoing")));
    if (auto error = applyChanges(m_history.at(m_cursor), false)) return !fail(error);
    ++m_cursor;
    ++m_revision;
    emit historyChanged();
    emit revisionChanged();
    return true;
}

void AutomationModel::clearHistory()
{
    m_history.clear();
    m_cursor = 0;
    emit historyChanged();
}

} // namespace QindaTK
