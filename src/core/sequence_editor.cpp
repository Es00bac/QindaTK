// SPDX-License-Identifier: LGPL-3.0-or-later
#include "sequence_editor.h"

#include "media_source_info.h"
#include "timeline_codec.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <limits>

namespace QindaTK {
namespace {
constexpr qint64 kUnbounded = std::numeric_limits<qint64>::max();

QString text(const QUuid &id)
{
    return id.isNull() ? QString() : id.toString(QUuid::WithoutBraces);
}

QString tr(const char *source)
{
    return QCoreApplication::translate("SequenceEditor", source);
}

QVariantMap failure(const QString &message)
{
    return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
}

// The committed clip on the same track that ends exactly where `clip` starts.
const TimelineClip *outgoingOf(const TimelineModel &model, const TimelineClip &clip)
{
    if (clip.start == std::numeric_limits<qint64>::min()) return nullptr;
    for (const QUuid &id : model.clipsInRange(clip.trackId, clip.start - 1, clip.start)) {
        const TimelineClip *other = model.clipRecord(id);
        if (other && other->id != clip.id && other->end() == clip.start) return other;
    }
    return nullptr;
}

qint64 tailRoom(const TimelineModel &model, const TimelineClip &clip)
{
    if (model.policy()->allowBeyondSource() || clip.sourceDuration < 0) return kUnbounded;
    const auto used = TimelineTime::add(clip.sourceOffset, clip.duration);
    const auto room = used ? TimelineTime::subtract(clip.sourceDuration, *used) : std::nullopt;
    return room.value_or(0);
}

QVariantMap descriptor(const QString &id, const QString &label, const QVariantMap &parameters,
                       const QString &fallback)
{
    return {{QStringLiteral("id"), id}, {QStringLiteral("label"), label},
            {QStringLiteral("valueText"), parameters.value(id, fallback).toString()},
            {QStringLiteral("editable"), true}};
}
} // namespace

SequenceEditor::SequenceEditor(QObject *parent) : QObject(parent) {}

SequenceEditor::~SequenceEditor()
{
    if (m_timeline && !m_trimOperation.isEmpty()) m_timeline->cancel(m_trimOperation);
}

void SequenceEditor::setTimeline(TimelineModel *timeline)
{
    if (m_timeline == timeline) return;
    if (m_timeline && !m_trimOperation.isEmpty()) m_timeline->cancel(m_trimOperation);
    for (const auto &link : std::as_const(m_links)) disconnect(link);
    m_links.clear();
    m_timeline = timeline;
    m_outgoing.clear();
    m_incoming.clear();
    m_trimOperation.clear();
    m_trimKey.clear();
    m_trimStatus = QStringLiteral("idle");
    m_trimError.clear();
    m_trimApplied = QStringLiteral("0");
    if (timeline) {
        m_links << connect(timeline, &TimelineModel::revisionChanged, this, [this] {
            refreshTrim();
            refreshTransition();
        });
        m_links << connect(timeline, &TimelineModel::previewChanged, this, [this] { refreshTrim(); });
        m_links << connect(timeline, &TimelineModel::editInvalidated, this,
                           [this](const QString &operationId, const QString &reason) {
                               if (operationId == m_trimOperation) resetTrimSession(QStringLiteral("rejected"), reason);
                           });
    }
    emit bindingChanged();
    refreshTrim();
    refreshTransition();
}

void SequenceEditor::setResolver(ResourceResolver *resolver)
{
    if (m_resolver == resolver) return;
    m_resolver = resolver;
    emit bindingChanged();
    refreshTrim();
    refreshTransition();
}

void SequenceEditor::setPlayer(MediaPlayer *player)
{
    if (m_player == player) return;
    m_player = player;
    emit bindingChanged();
}

void SequenceEditor::setLinked(bool linked)
{
    if (m_linked == linked) return;
    m_linked = linked;
    emit policyChanged();
}

void SequenceEditor::setFrameExact(bool exact)
{
    if (m_frameExact == exact) return;
    m_frameExact = exact;
    emit policyChanged();
}

void SequenceEditor::setTransitionClipId(const QString &clipId)
{
    if (m_transitionClip == clipId) return;
    m_transitionClip = clipId;
    m_transitionError.clear();
    refreshTransition();
}

bool SequenceEditor::isMissing(const QString &resourceId) const
{
    if (resourceId.isEmpty()) return false;
    if (m_resolver) {
        const QVariantMap entry = m_resolver->lookup(resourceId);
        return entry.isEmpty() || entry.value(QStringLiteral("availability")).toString() == QLatin1String("missing");
    }
    const QString path = mediaLocalPath(resourceId);
    return path.isEmpty() || !QFileInfo::exists(path);
}

QString SequenceEditor::handleText(qint64 ticks) const
{
    return QString::number(std::max<qint64>(0, ticks));
}

void SequenceEditor::setError(const QString &error)
{
    if (m_lastError == error) return;
    m_lastError = error;
    emit lastErrorChanged();
}

bool SequenceEditor::selectEditPoint(const QString &outgoingClipId, const QString &incomingClipId)
{
    const TimelineClip *a = m_timeline ? m_timeline->clipRecord(QUuid(outgoingClipId)) : nullptr;
    const TimelineClip *b = m_timeline ? m_timeline->clipRecord(QUuid(incomingClipId)) : nullptr;
    if (!a || !b || a->trackId != b->trackId || a->end() != b->start) {
        setError(tr("Choose two adjacent clips on one track"));
        return false;
    }
    if (!m_trimOperation.isEmpty()) cancelTrim();
    m_outgoing = text(a->id);
    m_incoming = text(b->id);
    m_trimStatus = QStringLiteral("idle");
    m_trimError.clear();
    setError({});
    refreshTrim();
    return true;
}

bool SequenceEditor::selectCut(const QString &trackId, const QString &ticks)
{
    const auto at = TimelineTime::parseDecimal(ticks);
    if (!m_timeline || !at) return false;
    const TimelineClip *best = nullptr;
    const TimelineClip *bestOutgoing = nullptr;
    quint64 distance = std::numeric_limits<quint64>::max();
    for (const auto &entry : m_timeline->trackIndex(QUuid(trackId))) {
        const TimelineClip *clip = m_timeline->clipRecord(entry.id);
        const TimelineClip *before = clip ? outgoingOf(*m_timeline, *clip) : nullptr;
        if (!before) continue;
        const quint64 gap = clip->start > *at ? quint64(clip->start) - quint64(*at) : quint64(*at) - quint64(clip->start);
        if (gap < distance) {
            distance = gap;
            best = clip;
            bestOutgoing = before;
        }
    }
    return best && selectEditPoint(text(bestOutgoing->id), text(best->id));
}

void SequenceEditor::refreshTrim()
{
    QVariantMap edit;
    const auto a = m_timeline && !m_outgoing.isEmpty() ? m_timeline->effectiveClip(QUuid(m_outgoing)) : std::nullopt;
    const auto b = m_timeline && !m_incoming.isEmpty() ? m_timeline->effectiveClip(QUuid(m_incoming)) : std::nullopt;
    if (a && b) {
        const qint64 tail = tailRoom(*m_timeline, *a);
        const bool beyond = m_timeline->policy()->allowBeyondSource();
        const TimeMap *map = m_timeline->timeMap();
        edit = {{QStringLiteral("editId"), m_outgoing + QLatin1Char('|') + m_incoming},
                {QStringLiteral("leftClipId"), m_outgoing},
                {QStringLiteral("rightClipId"), m_incoming},
                {QStringLiteral("trackId"), text(b->trackId)},
                {QStringLiteral("adjacent"), a->trackId == b->trackId && a->end() == b->start},
                {QStringLiteral("cutTicks"), QString::number(b->start)},
                {QStringLiteral("leftResourceId"), a->resourceId},
                {QStringLiteral("rightResourceId"), b->resourceId},
                // Source times of the first and last frame each side shows.
                {QStringLiteral("leftHeadTicks"), QString::number(a->sourceOffset)},
                {QStringLiteral("leftTailTicks"), QString::number(a->sourceOffset + a->duration - 1)},
                {QStringLiteral("rightHeadTicks"), QString::number(b->sourceOffset)},
                {QStringLiteral("rightTailTicks"), QString::number(b->sourceOffset + b->duration - 1)},
                {QStringLiteral("leftTailHandleTicks"), handleText(tail)},
                {QStringLiteral("rightHeadHandleTicks"), beyond ? handleText(kUnbounded) : handleText(b->sourceOffset)},
                {QStringLiteral("legalHead"), beyond || b->sourceOffset > 0},
                {QStringLiteral("legalTail"), tail > 0},
                {QStringLiteral("missingLeft"), isMissing(a->resourceId)},
                {QStringLiteral("missingRight"), isMissing(b->resourceId)},
                {QStringLiteral("leftLinked"), !a->linkId.isNull()},
                {QStringLiteral("rightLinked"), !b->linkId.isNull()},
                {QStringLiteral("previewing"), !m_trimOperation.isEmpty()},
                {QStringLiteral("rateNumerator"), QString::number(map->frameRateNumerator())},
                {QStringLiteral("rateDenominator"), QString::number(map->frameRateDenominator())}};
    }
    m_trimEdit = edit;
    emit trimChanged();
}

void SequenceEditor::resetTrimSession(const QString &status, const QString &error)
{
    m_trimOperation.clear();
    m_trimKey.clear();
    m_trimStatus = status;
    m_trimError = error;
    m_trimApplied = QStringLiteral("0");
    refreshTrim();
}

QVariantMap SequenceEditor::previewTrim(const QString &mode, const QString &side, const QString &deltaTicks)
{
    if (!m_timeline) return failure(tr("No timeline is bound"));
    const auto delta = TimelineTime::parseDecimal(deltaTicks);
    if (!delta) return failure(tr("The delta must be an exact decimal tick count"));
    if (m_outgoing.isEmpty() || m_incoming.isEmpty()) return failure(tr("Select an edit point first"));
    const bool right = side == QLatin1String("right");
    const QUuid target(right ? m_incoming : m_outgoing);
    TimelineEditRequest request;
    if (mode == QLatin1String("roll")) {
        request.kind = TimelineEditKind::Roll;
        request.clipIds = {QUuid(m_outgoing), QUuid(m_incoming)};
    } else if (mode == QLatin1String("ripple")) {
        // Ripple trims the outgoing clip's end or the incoming clip's start;
        // later clips close up or make room.
        request.kind = right ? TimelineEditKind::TrimStart : TimelineEditKind::TrimEnd;
        request.clipIds = {target};
        request.ripple = true;
    } else if (mode == QLatin1String("slip")) {
        request.kind = TimelineEditKind::Slip;
        request.clipIds = {target};
    } else if (mode == QLatin1String("slide")) {
        request.kind = TimelineEditKind::Slide;
        request.clipIds = {target};
    } else {
        return failure(tr("Unknown trim mode “%1”").arg(mode));
    }
    request.linked = m_linked;
    request.frameExact = m_frameExact;
    // A session's request is fixed at begin, so any change of mode, side or
    // policy starts a new one.
    const QString key = QStringList{mode, mode == QLatin1String("roll") ? QString() : side,
                                    m_linked ? QStringLiteral("linked") : QString(),
                                    m_frameExact ? QStringLiteral("frames") : QString()}.join(QLatin1Char(':'));
    if (!m_trimOperation.isEmpty() && (key != m_trimKey || m_timeline->editOperationId() != m_trimOperation)) {
        m_timeline->cancel(m_trimOperation);
        m_trimOperation.clear();
    }
    if (m_trimOperation.isEmpty()) {
        OperationError error;
        const QString operation = m_timeline->begin(request, &error);
        if (operation.isEmpty()) {
            resetTrimSession(QStringLiteral("rejected"), error.message);
            setError(error.message);
            return failure(error.message);
        }
        m_trimOperation = operation;
        m_trimKey = key;
    }
    TimelineEditInput input;
    input.pointerTicks = *delta;
    input.snap = false;
    const TimelinePlan plan = m_timeline->update(m_trimOperation, input);
    m_trimApplied = QString::number(plan.appliedDelta);
    m_trimStatus = plan.error ? QStringLiteral("rejected") : QStringLiteral("ready");
    m_trimError = plan.error.message;
    setError(plan.error.message);
    refreshTrim();
    return {{QStringLiteral("ok"), !plan.error}, {QStringLiteral("operationId"), m_trimOperation},
            {QStringLiteral("status"), m_trimStatus}, {QStringLiteral("appliedDelta"), m_trimApplied},
            {QStringLiteral("changeCount"), int(plan.changes.changes.size())},
            {QStringLiteral("error"), plan.error.message}};
}

QVariantMap SequenceEditor::commitTrim()
{
    if (!m_timeline || m_trimOperation.isEmpty()) return failure(tr("There is no trim preview to commit"));
    const QString operation = m_trimOperation;
    QString transaction;
    const OperationError error = m_timeline->commit(operation, &transaction);
    resetTrimSession(error ? QStringLiteral("rejected") : QStringLiteral("committed"), error.message);
    setError(error.message);
    if (error) return failure(error.message);
    return {{QStringLiteral("ok"), true}, {QStringLiteral("transactionId"), transaction},
            {QStringLiteral("revision"), m_timeline->revisionText()}};
}

bool SequenceEditor::cancelTrim()
{
    if (!m_timeline || m_trimOperation.isEmpty()) return false;
    const QString operation = m_trimOperation;
    // Clear first: cancel() emits previewChanged, which refreshes the snapshot.
    m_trimOperation.clear();
    m_timeline->cancel(operation);
    resetTrimSession(QStringLiteral("idle"), {});
    return true;
}

QVariantMap SequenceEditor::apply(TimelineEditRequest request, qint64 pointer)
{
    if (!m_timeline) return failure(tr("No timeline is bound"));
    if (!m_trimOperation.isEmpty()) cancelTrim();
    request.linked = m_linked;
    request.frameExact = m_frameExact;
    TimelineEditInput input;
    input.pointerTicks = pointer;
    input.snap = false;
    QString transaction;
    const OperationError error = m_timeline->apply(request, input, &transaction);
    setError(error.message);
    if (error) return failure(error.message);
    return {{QStringLiteral("ok"), true}, {QStringLiteral("transactionId"), transaction},
            {QStringLiteral("revision"), m_timeline->revisionText()}};
}

QVariantMap SequenceEditor::setTransition(const QString &clipId, const QVariantMap &spec)
{
    TimelineTransition transition;
    transition.type = spec.value(QStringLiteral("type"), QStringLiteral("dissolve")).toString();
    transition.alignment = spec.value(QStringLiteral("alignment"), QStringLiteral("center")).toString();
    const auto length = TimelineTime::parseDecimal(spec.value(QStringLiteral("durationTicks")).toString());
    if (!length || *length < 1) {
        m_transitionError = tr("The transition length must be a positive exact tick count");
        emit transitionChanged();
        return failure(m_transitionError);
    }
    // The planner places the length by alignment around the cut.
    transition.afterCut = *length;
    const QVariantMap parameters = spec.value(QStringLiteral("parameters")).toMap();
    for (auto it = parameters.cbegin(); it != parameters.cend(); ++it)
        transition.parameters.insert(it.key(), it.value().toString());
    TimelineEditRequest request;
    request.kind = TimelineEditKind::SetTransition;
    request.clipIds = {QUuid(clipId)};
    request.transition = transition;
    const QVariantMap result = apply(request, 0);
    m_transitionError = result.value(QStringLiteral("error")).toString();
    refreshTransition();
    return result;
}

QVariantMap SequenceEditor::removeTransition(const QString &clipId)
{
    TimelineEditRequest request;
    request.kind = TimelineEditKind::SetTransition;
    request.clipIds = {QUuid(clipId)};
    const QVariantMap result = apply(request, 0);
    m_transitionError = result.value(QStringLiteral("error")).toString();
    refreshTransition();
    return result;
}

QVariantMap SequenceEditor::editTransition(const QString &parameter, const QString &value)
{
    const TimelineClip *clip = m_timeline && !m_transitionClip.isEmpty()
        ? m_timeline->clipRecord(QUuid(m_transitionClip)) : nullptr;
    if (!clip) return failure(tr("Select the incoming clip of a cut first"));
    const TimelineTransition &current = clip->transition;
    // A new transition defaults to a one-second centred dissolve.
    const qint64 second = m_timeline->timebase().numerator / std::max<qint64>(1, m_timeline->timebase().denominator);
    QVariantMap spec{{QStringLiteral("type"), current.isNull() ? QStringLiteral("dissolve") : current.type},
                     {QStringLiteral("durationTicks"), QString::number(current.isNull() ? second : current.duration())},
                     {QStringLiteral("alignment"), current.alignment.isEmpty() ? QStringLiteral("center") : current.alignment}};
    QVariantMap parameters = current.parameters;
    if (parameter == QLatin1String("type") || parameter == QLatin1String("durationTicks")
        || parameter == QLatin1String("alignment")) {
        spec.insert(parameter, value);
    } else if (parameter.startsWith(QLatin1String("parameter:"))) {
        parameters.insert(parameter.mid(10), value);
    } else {
        m_transitionError = tr("Unknown transition setting “%1”").arg(parameter);
        emit transitionChanged();
        return failure(m_transitionError);
    }
    spec.insert(QStringLiteral("parameters"), parameters);
    return setTransition(m_transitionClip, spec);
}

QVariantMap SequenceEditor::transitionWindow(const QString &clipId) const
{
    const TimelineClip *clip = m_timeline ? m_timeline->clipRecord(QUuid(clipId)) : nullptr;
    if (!clip || clip->transition.isNull()) return failure(tr("The clip has no transition"));
    const auto start = TimelineTime::subtract(clip->start, clip->transition.beforeCut);
    const auto end = TimelineTime::add(clip->start, clip->transition.afterCut);
    if (!start || !end) return failure(tr("The transition window is out of range"));
    return {{QStringLiteral("ok"), true}, {QStringLiteral("startTicks"), QString::number(*start)},
            {QStringLiteral("endTicks"), QString::number(*end)}};
}

void SequenceEditor::refreshTransition()
{
    QVariantMap map;
    const TimelineClip *b = m_timeline && !m_transitionClip.isEmpty()
        ? m_timeline->clipRecord(QUuid(m_transitionClip)) : nullptr;
    if (b) {
        const TimelineClip *a = outgoingOf(*m_timeline, *b);
        const TimelineTransition &t = b->transition;
        const bool beyond = m_timeline->policy()->allowBeyondSource();
        const QString type = t.isNull() ? QStringLiteral("dissolve") : t.type;
        QVariantList parameters;
        if (type == QLatin1String("wipe"))
            parameters.push_back(descriptor(QStringLiteral("direction"), tr("Direction"), t.parameters, QStringLiteral("left")));
        if (type == QLatin1String("dip"))
            parameters.push_back(descriptor(QStringLiteral("color"), tr("Colour"), t.parameters, QStringLiteral("#000000")));
        parameters.push_back(descriptor(QStringLiteral("curve"), tr("Audio curve"), t.parameters, QStringLiteral("linear")));
        const QVariantMap window = transitionWindow(m_transitionClip);
        map = {{QStringLiteral("transitionId"), m_transitionClip},
               {QStringLiteral("leftClipId"), a ? text(a->id) : QString()},
               {QStringLiteral("rightClipId"), m_transitionClip},
               {QStringLiteral("hasTransition"), !t.isNull()},
               {QStringLiteral("type"), type},
               {QStringLiteral("durationTicks"), QString::number(t.duration())},
               {QStringLiteral("beforeCutTicks"), QString::number(t.beforeCut)},
               {QStringLiteral("afterCutTicks"), QString::number(t.afterCut)},
               {QStringLiteral("alignment"), t.alignment.isEmpty() ? QStringLiteral("center") : t.alignment},
               {QStringLiteral("headHandleTicks"), beyond ? handleText(kUnbounded) : handleText(b->sourceOffset)},
               {QStringLiteral("tailHandleTicks"), a ? handleText(tailRoom(*m_timeline, *a)) : QStringLiteral("0")},
               {QStringLiteral("missingMedia"), isMissing(b->resourceId) || (a && isMissing(a->resourceId))},
               {QStringLiteral("windowStartTicks"), window.value(QStringLiteral("startTicks"))},
               {QStringLiteral("windowEndTicks"), window.value(QStringLiteral("endTicks"))},
               {QStringLiteral("parameters"), parameters}};
    }
    m_transition = map;
    emit transitionChanged();
}

QVariantMap SequenceEditor::previewTransition()
{
    if (!m_player || !m_timeline) return failure(tr("Bind a MediaPlayer to preview transitions"));
    const QVariantMap window = transitionWindow(m_transitionClip);
    if (!window.value(QStringLiteral("ok")).toBool()) return window;
    const TimelineRate samples{m_timeline->timeMap()->sampleRate(), 1};
    const auto start = TimelineTime::parseDecimal(window.value(QStringLiteral("startTicks")).toString());
    const auto end = TimelineTime::parseDecimal(window.value(QStringLiteral("endTicks")).toString());
    const auto first = start ? TimelineTime::rescale(*start, m_timeline->timebase(), samples, TimeMap::Ceil) : std::nullopt;
    const auto last = end ? TimelineTime::rescale(*end, m_timeline->timebase(), samples, TimeMap::Ceil) : std::nullopt;
    if (!first || !last) return failure(tr("The transition window is out of range"));
    // AGENT-NOTE: the player renders the committed program through
    // MediaProgram::render, the same evaluator export uses (D-120).
    const QString loop = m_player->transport()->requestLoop(true, QString::number(*first), QString::number(*last));
    const QString seek = m_player->seekTicks(window.value(QStringLiteral("startTicks")).toString());
    const QString play = m_player->play();
    const bool ok = !loop.isEmpty() && !seek.isEmpty() && !play.isEmpty();
    m_previewFirstSample = QString::number(*first);
    m_previewLastSample = QString::number(*last);
    m_transitionPreview = ok ? QStringLiteral("pending") : QStringLiteral("failed");
    emit transitionChanged();
    QVariantMap result = window;
    result.insert(QStringLiteral("ok"), ok);
    result.insert(QStringLiteral("startSample"), QString::number(*first));
    result.insert(QStringLiteral("endSample"), QString::number(*last));
    return result;
}

bool SequenceEditor::stopTransitionPreview()
{
    if (!m_player || m_transitionPreview != QLatin1String("pending")) return false;
    m_player->stop();
    m_player->transport()->requestLoop(false, m_previewFirstSample, m_previewLastSample);
    m_transitionPreview = QStringLiteral("idle");
    emit transitionChanged();
    return true;
}

QVariantMap SequenceEditor::linkClips(const QStringList &clipIds)
{
    TimelineEditRequest request;
    request.kind = TimelineEditKind::Link;
    for (const QString &id : clipIds) request.clipIds.push_back(QUuid(id));
    const bool linked = m_linked;
    m_linked = false; // link exactly the named clips
    const QVariantMap result = apply(request, 0);
    m_linked = linked;
    return result;
}

QVariantMap SequenceEditor::unlinkClips(const QStringList &clipIds)
{
    TimelineEditRequest request;
    request.kind = TimelineEditKind::Unlink;
    for (const QString &id : clipIds) request.clipIds.push_back(QUuid(id));
    return apply(request, 0);
}

} // namespace QindaTK
