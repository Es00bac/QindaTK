// SPDX-License-Identifier: LGPL-3.0-or-later
#include "timeline_codec.h"
#include "timeline_model.h"
#include "timeline_planner.h"

namespace QindaTK {
namespace {
QVariantMap failure(const OperationError &error)
{
    return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), error.message},
            {QStringLiteral("code"), int(error.code)}, {QStringLiteral("field"), error.fieldPath}};
}

QVariantMap failure(const QString &message, ErrorCode code = ErrorCode::InvalidArgument)
{
    return failure(OperationError{code, message, {}, {}});
}

std::optional<TimelineEditKind> kindFromText(const QString &text)
{
    static const QHash<QString, TimelineEditKind> kinds{
        {QStringLiteral("move"), TimelineEditKind::Move},
        {QStringLiteral("trimStart"), TimelineEditKind::TrimStart},
        {QStringLiteral("trimEnd"), TimelineEditKind::TrimEnd},
        {QStringLiteral("split"), TimelineEditKind::Split},
        {QStringLiteral("delete"), TimelineEditKind::Delete},
        {QStringLiteral("setClipTiming"), TimelineEditKind::SetClipTiming},
        {QStringLiteral("addMarker"), TimelineEditKind::AddMarker},
        {QStringLiteral("moveMarker"), TimelineEditKind::MoveMarker},
        {QStringLiteral("editMarker"), TimelineEditKind::EditMarker},
        {QStringLiteral("removeMarker"), TimelineEditKind::RemoveMarker},
        {QStringLiteral("trackProperty"), TimelineEditKind::TrackProperty},
        {QStringLiteral("reorderTrack"), TimelineEditKind::ReorderTrack},
        {QStringLiteral("resizeTrack"), TimelineEditKind::ResizeTrack},
        {QStringLiteral("addTrack"), TimelineEditKind::AddTrack},
        {QStringLiteral("removeTrack"), TimelineEditKind::RemoveTrack}};
    const auto it = kinds.constFind(text);
    return it == kinds.cend() ? std::nullopt : std::optional<TimelineEditKind>(*it);
}

std::optional<TimelineSnapGrid> gridFromText(const QString &text)
{
    if (text.isEmpty() || text == QLatin1String("none")) return TimelineSnapGrid::None;
    if (text == QLatin1String("frames")) return TimelineSnapGrid::Frames;
    if (text == QLatin1String("seconds")) return TimelineSnapGrid::Seconds;
    if (text == QLatin1String("samples")) return TimelineSnapGrid::Samples;
    return std::nullopt;
}

bool optionalTicks(const QVariantMap &map, const QString &key, std::optional<qint64> &target,
                   QString &error)
{
    if (!map.contains(key)) return true;
    target = TimelineCodec::ticks(map.value(key));
    if (!target) error = QStringLiteral("%1 must be an exact decimal string").arg(key);
    return target.has_value();
}
} // namespace

QString TimelineModel::begin(const TimelineEditRequest &request, OperationError *error)
{
    const auto reject = [&](OperationError failure) {
        if (error) *error = failure;
        fail(failure.message);
        return QString();
    };
    if (m_session && m_session->stale) endSession();
    if (m_session)
        return reject({ErrorCode::Conflict, tr("Another timeline edit is in progress"), {}, {}});
    if (request.expected) {
        const SnapshotHeader current = snapshotHeader();
        if (request.expected->documentId != current.documentId
            || request.expected->resetGeneration != current.resetGeneration)
            return reject({ErrorCode::StaleGeneration, tr("The timeline was reloaded"), {}, {}});
        if (request.expected->revision != current.revision)
            return reject({ErrorCode::StaleRevision, tr("The timeline changed before the edit began"), {}, {}});
    }
    if (auto failure = TimelinePlanner::validateRequest(*this, request)) return reject(failure);
    Session session;
    session.operationId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    session.request = request;
    session.base = snapshotHeader();
    m_session = session;
    if (error) *error = {};
    emit editStateChanged();
    return m_session->operationId;
}

TimelinePlan TimelineModel::update(const QString &operationId, const TimelineEditInput &input)
{
    TimelinePlan plan;
    if (!m_session || m_session->operationId != operationId || operationId.isEmpty()) {
        plan.error = m_retiredOperations.contains(operationId)
            ? OperationError{ErrorCode::StaleRevision, tr("The timeline changed during the edit"), {}, {}}
            : OperationError{ErrorCode::InvalidArgument, tr("No such timeline edit"), {}, {}};
        return plan;
    }
    if (m_session->stale || m_session->base.revision != snapshotHeader().revision
        || m_session->base.resetGeneration != m_generation) {
        plan.error = {ErrorCode::StaleRevision, tr("The timeline changed during the edit"), {}, {}};
        return plan;
    }
    const bool hadPreview = hasPreview();
    m_session->plan = TimelinePlanner::plan(*this, m_session->request, input, operationId);
    m_session->hasPlan = true;
    m_session->clipChanges.clear();
    m_session->markerChanges.clear();
    m_session->trackChanges.clear();
    m_session->orderChange = -1;
    const auto &changes = m_session->plan.changes.changes;
    for (int i = 0; i < changes.size(); ++i) {
        const auto &change = changes.at(i);
        switch (change.kind) {
        case TimelineChange::Kind::Clip: m_session->clipChanges.insert(change.id, i); break;
        case TimelineChange::Kind::Marker: m_session->markerChanges.insert(change.id, i); break;
        case TimelineChange::Kind::Track: m_session->trackChanges.insert(change.id, i); break;
        case TimelineChange::Kind::TrackOrder: m_session->orderChange = i; break;
        }
    }
    publishPreview(hadPreview || hasPreview());
    return m_session->plan;
}

OperationError TimelineModel::commit(const QString &operationId, QString *transactionId)
{
    if (!m_session || m_session->operationId != operationId || operationId.isEmpty()) {
        if (m_retiredOperations.contains(operationId))
            return failWith({ErrorCode::StaleRevision, tr("The timeline changed during the edit"), {}, {}});
        return failWith({ErrorCode::InvalidArgument, tr("No such timeline edit"), {}, {}});
    }
    if (m_session->stale || m_session->base.revision != snapshotHeader().revision
        || m_session->base.resetGeneration != m_generation) {
        endSession();
        return failWith({ErrorCode::StaleRevision, tr("The timeline changed during the edit"), {}, {}});
    }
    if (!m_session->hasPlan) {
        endSession();
        return failWith({ErrorCode::InvalidArgument, tr("The edit has no preview to commit"), {}, {}});
    }
    TimelineChangeSet changes = m_session->plan.changes;
    if (m_session->plan.error) {
        const OperationError error = m_session->plan.error;
        endSession();
        return failWith(error);
    }
    if (changes.isEmpty()) {
        endSession();
        if (transactionId) transactionId->clear();
        return {};
    }
    // The policy may be stateful; it sees the final change set once more.
    if (auto failure = policy()->validate(*this, changes)) {
        endSession();
        return failWith(failure);
    }
    const quint64 bytes = cost(changes);
    if (!m_externalUndo && bytes > m_historyBudget) {
        endSession();
        return failWith({ErrorCode::Validation, tr("This edit is too large to undo"), {}, {}});
    }
    endSession();
    if (auto failure = applyChangeSet(changes, false)) return failWith(failure);
    recordHistory(changes, bytes);
    if (transactionId) *transactionId = changes.transactionId;
    emit committed(changes.transactionId, changes.label, changePayload(changes));
    return {};
}

void TimelineModel::cancel(const QString &operationId)
{
    if (!m_session || m_session->operationId != operationId) return;
    endSession();
}

OperationError TimelineModel::apply(const TimelineEditRequest &request,
                                    const TimelineEditInput &input, QString *transactionId)
{
    OperationError error;
    const QString operation = begin(request, &error);
    if (operation.isEmpty()) return error;
    const TimelinePlan plan = update(operation, input);
    if (plan.error) {
        cancel(operation);
        return failWith(plan.error);
    }
    return commit(operation, transactionId);
}

void TimelineModel::endSession()
{
    if (!m_session) return;
    const bool hadPreview = hasPreview();
    m_session.reset();
    emit editStateChanged();
    if (hadPreview) emit previewChanged();
}

void TimelineModel::invalidateSession(const QString &reason)
{
    if (!m_session || m_session->stale) return;
    const QString operation = m_session->operationId;
    const bool hadPreview = hasPreview();
    m_session->stale = true;
    m_retiredOperations.push_back(operation);
    while (m_retiredOperations.size() > 64) m_retiredOperations.removeFirst();
    emit editStateChanged();
    if (hadPreview) emit previewChanged();
    emit editInvalidated(operation, reason);
}

void TimelineModel::publishPreview(bool changed)
{
    if (changed) emit previewChanged();
}

QVariantMap TimelineModel::editPreview() const
{
    if (!m_session || m_session->stale || !m_session->hasPlan) return {};
    const TimelinePlan &plan = m_session->plan;
    QVariantMap result{{QStringLiteral("ok"), !plan.error},
                       {QStringLiteral("operationId"), m_session->operationId},
                       {QStringLiteral("label"), plan.changes.label},
                       {QStringLiteral("changeCount"), int(plan.changes.changes.size())},
                       {QStringLiteral("appliedDelta"), QString::number(plan.appliedDelta)},
                       {QStringLiteral("appliedTrackDelta"), plan.appliedTrackDelta},
                       {QStringLiteral("snapped"), plan.snap.snapped},
                       {QStringLiteral("snapTarget"), QString::number(plan.snap.target)},
                       {QStringLiteral("snapKind"), plan.snap.kind},
                       {QStringLiteral("snapTargetId"), plan.snap.targetId.isNull()
                            ? QString() : plan.snap.targetId.toString(QUuid::WithoutBraces)}};
    if (plan.error) {
        result.insert(QStringLiteral("error"), plan.error.message);
        result.insert(QStringLiteral("code"), int(plan.error.code));
    }
    return result;
}

QVariantMap TimelineModel::beginEdit(const QVariantMap &map)
{
    const auto kind = kindFromText(map.value(QStringLiteral("kind")).toString());
    if (!kind) return failure(tr("Unknown timeline edit kind"));
    TimelineEditRequest request;
    request.kind = *kind;
    for (const auto &text : map.value(QStringLiteral("clipIds")).toStringList()) {
        const QUuid id(text);
        if (id.isNull()) return failure(tr("clipIds must contain UUID strings"));
        request.clipIds.push_back(id);
    }
    request.anchorClipId = TimelineCodec::uuid(map.value(QStringLiteral("anchorClipId")));
    request.markerId = TimelineCodec::uuid(map.value(QStringLiteral("markerId")));
    request.trackId = TimelineCodec::uuid(map.value(QStringLiteral("trackId")));
    request.beforeTrackId = TimelineCodec::uuid(map.value(QStringLiteral("beforeTrackId")));
    request.ripple = map.value(QStringLiteral("ripple")).toBool();
    request.label = map.value(QStringLiteral("label")).toString();
    QString error;
    std::optional<qint64> grab;
    if (!optionalTicks(map, QStringLiteral("grabTicks"), grab, error)
        || !optionalTicks(map, QStringLiteral("start"), request.start, error)
        || !optionalTicks(map, QStringLiteral("duration"), request.duration, error)
        || !optionalTicks(map, QStringLiteral("sourceOffset"), request.sourceOffset, error))
        return failure(error);
    request.grabTicks = grab.value_or(0);
    if (map.contains(QStringLiteral("expectedRevision"))) {
        const auto revision = RevisionId::parseDecimal(map.value(QStringLiteral("expectedRevision")).toString());
        if (!revision) return failure(tr("expectedRevision must be a decimal string"));
        SnapshotHeader expected = snapshotHeader();
        expected.revision = *revision;
        if (map.contains(QStringLiteral("expectedGeneration"))) {
            const auto generation = TimelineTime::parseDecimal(map.value(QStringLiteral("expectedGeneration")).toString());
            if (!generation || *generation < 0) return failure(tr("expectedGeneration must be a decimal string"));
            expected.resetGeneration = quint64(*generation);
        }
        request.expected = expected;
    }
    if (map.contains(QStringLiteral("marker"))) {
        const auto *base = request.kind == TimelineEditKind::EditMarker ? markerRecord(request.markerId) : nullptr;
        const auto marker = TimelineCodec::markerFromMap(map.value(QStringLiteral("marker")).toMap(), &error, base);
        if (!marker) return failure(error);
        request.marker = marker;
    }
    if (map.contains(QStringLiteral("track"))) {
        QVariantMap trackMap = map.value(QStringLiteral("track")).toMap();
        if (!trackMap.contains(QStringLiteral("id")))
            trackMap.insert(QStringLiteral("id"), QUuid::createUuid().toString(QUuid::WithoutBraces));
        const auto track = TimelineCodec::trackFromMap(trackMap, &error);
        if (!track) return failure(error);
        request.track = track;
    }
    request.property = map.value(QStringLiteral("property")).toString();
    request.value = map.value(QStringLiteral("value"));
    OperationError failed;
    const QString operation = begin(request, &failed);
    if (operation.isEmpty()) return failure(failed);
    return {{QStringLiteral("ok"), true}, {QStringLiteral("operationId"), operation}};
}

QVariantMap TimelineModel::updateEdit(const QString &operationId, const QVariantMap &map)
{
    TimelineEditInput input;
    QString error;
    std::optional<qint64> pointer, tolerance, playhead;
    if (!optionalTicks(map, QStringLiteral("pointerTicks"), pointer, error)
        || !optionalTicks(map, QStringLiteral("snapToleranceTicks"), tolerance, error)
        || !optionalTicks(map, QStringLiteral("playheadTicks"), playhead, error))
        return failure(error);
    const auto grid = gridFromText(map.value(QStringLiteral("grid")).toString());
    if (!grid) return failure(tr("grid must be none, frames, seconds or samples"));
    input.pointerTicks = pointer.value_or(m_session && m_session->operationId == operationId
                                              ? m_session->request.grabTicks : 0);
    input.snapToleranceTicks = std::max<qint64>(0, tolerance.value_or(0));
    input.playheadTicks = playhead;
    input.grid = *grid;
    input.trackDelta = map.value(QStringLiteral("trackDelta")).toInt();
    input.trackHeight = map.value(QStringLiteral("trackHeight")).toInt();
    input.snap = map.value(QStringLiteral("snap"), true).toBool();
    const TimelinePlan plan = update(operationId, input);
    if (plan.error.code == ErrorCode::StaleRevision || plan.error.code == ErrorCode::InvalidArgument) {
        if (!m_session || m_session->operationId != operationId || m_session->stale)
            return failure(plan.error);
    }
    QVariantMap result = editPreview();
    if (result.isEmpty()) return failure(plan.error);
    return result;
}

QVariantMap TimelineModel::commitEdit(const QString &operationId)
{
    QString transaction;
    const quint64 before = m_revision;
    if (auto error = commit(operationId, &transaction)) return failure(error);
    return {{QStringLiteral("ok"), true}, {QStringLiteral("transactionId"), transaction},
            {QStringLiteral("changed"), m_revision != before},
            {QStringLiteral("revision"), revisionText()}};
}

bool TimelineModel::cancelEdit(const QString &operationId)
{
    if (!m_session || m_session->operationId != operationId) return false;
    cancel(operationId);
    return true;
}

QVariantMap TimelineModel::applyEdit(const QVariantMap &request, const QVariantMap &input)
{
    const QVariantMap begun = beginEdit(request);
    if (!begun.value(QStringLiteral("ok")).toBool()) return begun;
    const QString operation = begun.value(QStringLiteral("operationId")).toString();
    const QVariantMap preview = updateEdit(operation, input);
    if (!preview.value(QStringLiteral("ok")).toBool()) {
        cancel(operation);
        fail(preview.value(QStringLiteral("error")).toString());
        return preview;
    }
    return commitEdit(operation);
}

} // namespace QindaTK
