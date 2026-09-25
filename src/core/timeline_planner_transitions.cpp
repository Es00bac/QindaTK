// SPDX-License-Identifier: LGPL-3.0-or-later
#include "timeline_planner.h"

#include "timeline_model.h"

#include <QColor>
#include <QCoreApplication>
#include <algorithm>

// Transitions (WP-071, docs/sequence-edits.md "Transitions", D-120): a
// transition lives on its incoming clip as beforeCut/afterCut ticks around
// the cut. Bounds come from the clip records only (D-118).
namespace QindaTK {
namespace {
QString tr(const char *text)
{
    return QCoreApplication::translate("TimelineModel", text);
}

void setError(TimelinePlanner::Context &context, OperationError error)
{
    context.plan.error = std::move(error);
    context.plan.changes.changes.clear();
}

OperationError checkParameters(const TimelineTransition &transition)
{
    for (auto it = transition.parameters.cbegin(); it != transition.parameters.cend(); ++it) {
        const QString value = it.value().toString();
        bool ok = false;
        if (it.key() == QLatin1String("direction"))
            ok = QStringList{QStringLiteral("left"), QStringLiteral("right"), QStringLiteral("up"),
                             QStringLiteral("down")}.contains(value);
        else if (it.key() == QLatin1String("color"))
            ok = QColor::isValidColorName(value);
        else if (it.key() == QLatin1String("curve"))
            ok = value == QLatin1String("linear") || value == QLatin1String("equalPower");
        if (!ok)
            return TimelinePlanner::error(ErrorCode::Validation,
                tr("Transition parameter “%1” cannot be “%2”").arg(it.key(), value), it.key());
    }
    return {};
}

// AGENT-GUARD (D-120): a clip's two transition windows must not overlap.
// Its lead-in (its own transition's afterCut) and the lead-out into the next
// clip (that clip's transition's beforeCut) together fit in its length.
// MediaProgram::render draws a span that is inside a fadeOut only as the
// partner of the incoming span, so overlapping windows would drop the clip
// before this one in the middle of its dissolve (tst_sequence_edits
// transitionsOnAShortClipDoNotOverlap).
bool windowsOverlap(const TimelineClip &middle, const TimelineClip &next)
{
    const qint64 in = middle.transition.isNull() ? 0 : middle.transition.afterCut;
    const qint64 out = next.transition.isNull() ? 0 : next.transition.beforeCut;
    const auto used = TimelineTime::add(in, out);
    return !used || *used > middle.duration;
}
} // namespace

// SetTransition into clipIds[0] (and, when linked, into each partner that
// also starts at a cut). The requested length is placed by alignment and,
// with frameExact, rounded to whole program frames around the cut.
void TimelinePlanner::planTransition(Context &context)
{
    const auto &request = context.request;
    if (request.clipIds.size() != 1)
        return setError(context, error(ErrorCode::InvalidArgument, tr("A transition belongs to one incoming clip")));
    const bool removing = !request.transition || request.transition->isNull();
    TimelineTransition wanted = removing ? TimelineTransition{} : *request.transition;
    if (!removing) {
        if (auto failure = checkParameters(wanted)) return setError(context, failure);
        const qint64 length = wanted.duration();
        if (length < 1)
            return setError(context, error(ErrorCode::Validation, tr("A transition lasts at least one tick"),
                                           QStringLiteral("duration")));
        const QString &alignment = wanted.alignment;
        if (alignment == QLatin1String("start")) {
            wanted.beforeCut = 0;
            wanted.afterCut = length;
        } else if (alignment == QLatin1String("end")) {
            wanted.beforeCut = length;
            wanted.afterCut = 0;
        } else if (alignment == QLatin1String("center")) {
            wanted.beforeCut = length - length / 2; // the incoming head takes the odd tick
            wanted.afterCut = length / 2;
        } else if (!alignment.isEmpty()) {
            return setError(context, error(ErrorCode::InvalidArgument,
                                           tr("Alignment must be start, center or end"), QStringLiteral("alignment")));
        }
    }
    const QVector<QUuid> ids = request.linked ? withLinked(context.model, request.clipIds) : request.clipIds;
    QHash<QUuid, TimelineClip> after;
    for (const QUuid &id : ids) {
        const TimelineClip *incoming = context.model.clipRecord(id);
        if (!incoming || !editableClip(context, *incoming)) {
            if (!incoming) setError(context, error(ErrorCode::StaleRevision, tr("A clip in this edit no longer exists")));
            return;
        }
        const bool primary = id == request.clipIds.first();
        const TimelineClip *outgoing = endingAt(context.model, incoming->trackId, incoming->start, {id});
        TimelineClip next = *incoming;
        if (removing) {
            next.transition = {};
            after.insert(id, next);
            continue;
        }
        if (!outgoing) {
            if (!primary) continue;
            return setError(context, error(ErrorCode::Validation,
                tr("No clip ends where “%1” starts, so there is no cut to transition").arg(clipName(*incoming))));
        }
        TimelineTransition placed = wanted;
        if (request.frameExact) {
            // Whole frames around the cut: the frame count nearest the length,
            // split by alignment with the odd frame before the cut.
            const auto cutFrame = context.model.ticksToFrames(incoming->start, TimeMap::NearestAway);
            const TimelineRate rate{context.model.timeMap()->frameRateNumerator(),
                                    context.model.timeMap()->frameRateDenominator()};
            const auto frames = TimelineTime::rescale(wanted.duration(), context.model.timebase(), rate,
                                                      TimeMap::NearestAway);
            if (!cutFrame || !frames)
                return setError(context, error(ErrorCode::InvalidArgument, tr("The transition is out of range")));
            const qint64 count = std::max<qint64>(1, *frames);
            const qint64 beforeFrames = wanted.afterCut == 0 ? count
                : wanted.beforeCut == 0 ? 0 : count - count / 2;
            const auto first = context.model.frameStartTicks(*cutFrame - beforeFrames);
            const auto last = context.model.frameStartTicks(*cutFrame - beforeFrames + count);
            const auto beforeCut = first ? TimelineTime::subtract(incoming->start, *first) : std::nullopt;
            const auto afterCut = last ? TimelineTime::subtract(*last, incoming->start) : std::nullopt;
            if (!beforeCut || !afterCut)
                return setError(context, error(ErrorCode::InvalidArgument, tr("The transition is out of range")));
            // A cut between frame starts keeps the region on its own side.
            placed.beforeCut = std::max<qint64>(0, *beforeCut);
            placed.afterCut = std::max<qint64>(0, *afterCut);
        }
        if (placed.beforeCut > headRoom(context, *incoming) || placed.afterCut > tailRoom(context, *outgoing))
            return setError(context, error(ErrorCode::Validation,
                tr("The source media has insufficient handles for this transition"), QStringLiteral("handles")));
        if (placed.beforeCut > outgoing->duration || placed.afterCut > incoming->duration)
            return setError(context, error(ErrorCode::Validation,
                tr("The transition is longer than the clips beside the cut"), QStringLiteral("duration")));
        next.transition = placed;
        after.insert(id, next);
    }
    // Every clip that now carries a window, against the windows at its other end.
    for (auto it = after.cbegin(); it != after.cend(); ++it) {
        if (it->transition.isNull()) continue;
        const TimelineClip *outgoing = endingAt(context.model, it->trackId, it->start, {it.key()});
        const TimelineClip *next = startingAt(context.model, it->trackId, it->end(), {it.key()});
        const TimelineClip left = outgoing ? after.value(outgoing->id, *outgoing) : TimelineClip{};
        const TimelineClip right = next ? after.value(next->id, *next) : TimelineClip{};
        const TimelineClip *overlapped = outgoing && windowsOverlap(left, it.value()) ? outgoing
            : next && windowsOverlap(it.value(), right) ? &*it : nullptr;
        if (overlapped)
            return setError(context, error(ErrorCode::Validation,
                tr("The transition would overlap the other transition on “%1”").arg(clipName(*overlapped)),
                QStringLiteral("duration")));
    }
    emitClips(context, after);
}

// AGENT-GUARD (D-120): a transition lives on its incoming clip but depends
// on the outgoing clip ending exactly at that clip's start. Any edit that
// moves either side runs through here: a cut that no longer exists drops the
// transition inside the same change set (undo restores it), and a cut whose
// handles became too short, or whose window now overlaps the window at the
// other end of the outgoing clip, is a soft error, so the preview shows it
// and commit refuses it.
void TimelinePlanner::reconcileTransitions(Context &context)
{
    auto &changes = context.plan.changes.changes;
    QHash<QUuid, TimelineClip> after;
    QHash<QUuid, int> index;
    QSet<QUuid> removed, check;
    QVector<QPair<TimelineClip, TimelineClip>> cuts; // incoming, outgoing in the edited state
    for (int i = 0; i < changes.size(); ++i) {
        const TimelineChange &change = changes.at(i);
        if (change.kind != TimelineChange::Kind::Clip) continue;
        index.insert(change.id, i);
        if (change.clipAfter) {
            after.insert(change.id, *change.clipAfter);
            if (!change.clipAfter->transition.isNull()) check.insert(change.id);
        } else {
            removed.insert(change.id);
        }
        // The clips that start where this one ended or now ends: their
        // windows reach into it, so its new length may be too short for both.
        for (const auto *side : {&change.clipBefore, &change.clipAfter}) {
            if (!*side) continue;
            const TimelineClip &was = **side;
            if (const TimelineClip *next = startingAt(context.model, was.trackId, was.end(), {was.id}))
                if (!next->transition.isNull()) check.insert(next->id);
        }
    }
    QList<QUuid> ordered(check.cbegin(), check.cend());
    std::sort(ordered.begin(), ordered.end());
    for (const QUuid &id : ordered) {
        if (removed.contains(id)) continue;
        TimelineClip incoming = after.contains(id) ? after.value(id) : *context.model.clipRecord(id);
        if (incoming.transition.isNull()) continue;
        // The outgoing clip in the edited state: a changed clip, or a
        // committed one the edit leaves where it was.
        std::optional<TimelineClip> outgoing;
        for (auto it = after.cbegin(); it != after.cend() && !outgoing; ++it)
            if (it.key() != id && it->trackId == incoming.trackId && it->end() == incoming.start) outgoing = *it;
        if (!outgoing) {
            QSet<QUuid> skip = removed;
            for (auto it = after.cbegin(); it != after.cend(); ++it) skip.insert(it.key());
            skip.insert(id);
            if (const TimelineClip *clip = endingAt(context.model, incoming.trackId, incoming.start, skip))
                outgoing = *clip;
        }
        if (!outgoing) {
            incoming.transition = {};
            if (index.contains(id)) {
                changes[index.value(id)].clipAfter = incoming;
            } else {
                TimelineChange change;
                change.kind = TimelineChange::Kind::Clip;
                change.id = id;
                change.clipBefore = *context.model.clipRecord(id);
                change.clipAfter = incoming;
                changes.push_back(change);
                index.insert(id, int(changes.size()) - 1);
            }
            after.insert(id, incoming);
            continue;
        }
        const TimelineTransition &transition = incoming.transition;
        if (transition.beforeCut > headRoom(context, incoming) || transition.afterCut > tailRoom(context, *outgoing)
            || transition.beforeCut > outgoing->duration || transition.afterCut > incoming.duration) {
            if (!context.plan.error)
                context.plan.error = error(ErrorCode::Validation,
                    tr("The transition into “%1” needs more source handle than this edit leaves")
                        .arg(clipName(incoming)), QStringLiteral("handles"));
        }
        cuts.push_back({incoming, *outgoing});
    }
    // Second pass, once every dropped transition is gone: the outgoing
    // clip's own lead-in (its transition's afterCut) and this window's
    // beforeCut share that clip's length.
    for (const auto &[incoming, outgoing] : cuts) {
        const TimelineClip left = after.value(outgoing.id, outgoing);
        if (!windowsOverlap(left, incoming)) continue;
        if (!context.plan.error)
            context.plan.error = error(ErrorCode::Validation,
                tr("The transitions at both ends of “%1” would overlap after this edit").arg(clipName(left)),
                QStringLiteral("duration"));
    }
}

} // namespace QindaTK
