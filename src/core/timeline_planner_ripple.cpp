// SPDX-License-Identifier: LGPL-3.0-or-later
#include "timeline_planner.h"

#include "timeline_model.h"

#include <QCoreApplication>
#include <algorithm>

// Ripple: the tracks an edit ripples, its edit points and the downstream
// shifts they cause.
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
} // namespace

QVector<QUuid> TimelinePlanner::rippleTracks(const TimelineModel &model, const QSet<QUuid> &sources,
                                              bool all)
{
    QVector<QUuid> result;
    for (const auto &id : model.trackOrder()) {
        if (!all && !sources.contains(id)) continue;
        const auto *track = model.trackRecord(id);
        // AGENT-NOTE: like an NLE with sync lock off, a locked track is never
        // rippled; its clips keep their positions.
        if (track && !track->locked) result.push_back(id);
    }
    return result;
}

// Every clip on a rippled track whose committed start is at or after an edit
// point moves by that point's shift. `after` already holds the targets' edits.
bool TimelinePlanner::applyRipple(Context &context, const QVector<QUuid> &tracks, RipplePoints points,
                                  QHash<QUuid, TimelineClip> &after, const QSet<QUuid> &skip)
{
    for (const auto &trackId : tracks) {
        auto list = points.value(trackId);
        if (list.isEmpty()) continue;
        std::sort(list.begin(), list.end(), [](const auto &a, const auto &b) { return a.at < b.at; });
        const auto &entries = context.model.trackIndex(trackId);
        auto it = std::lower_bound(entries.cbegin(), entries.cend(), list.first().at,
                                   [](const TimelineModel::IndexEntry &entry, qint64 value) {
                                       return entry.start < value;
                                   });
        for (; it != entries.cend(); ++it) {
            if (skip.contains(it->id)) continue;
            qint64 shift = 0;
            for (const auto &point : list) {
                if (point.at > it->start) break;
                const auto sum = TimelineTime::add(shift, point.shift);
                if (!sum) {
                    setError(context, error(ErrorCode::InvalidArgument,
                                                             tr("Ripple shift overflows")));
                    return false;
                }
                shift = *sum;
            }
            if (shift == 0) continue;
            TimelineClip clip = after.contains(it->id) ? after.value(it->id)
                                                       : *context.model.clipRecord(it->id);
            if (clip.locked) {
                setError(context, error(ErrorCode::Validation,
                    tr("Ripple would move locked clip “%1”").arg(clipName(clip))));
                return false;
            }
            const auto start = TimelineTime::add(clip.start, shift);
            if (!start || !TimelineTime::add(*start, clip.duration)) {
                setError(context, error(ErrorCode::InvalidArgument,
                                                         tr("Ripple moves a clip out of range")));
                return false;
            }
            clip.start = *start;
            after.insert(clip.id, clip);
        }
    }
    return true;
}

// AGENT-NOTE: one gesture inserts or removes time at an edit point once,
// however many selected clips share that point (a parallel trim of clips that
// end together, or all-tracks scope copying every point to every track), so
// identical points collapse. Distinct points still accumulate downstream.
TimelinePlanner::RipplePoints TimelinePlanner::pointsFor(const Context &context, const QVector<QUuid> &tracks,
                                                        const QVector<QPair<QUuid, RipplePoint>> &raw)
{
    RipplePoints result;
    const bool all = context.policy.rippleScope() == TimelineEditPolicy::RippleAllTracks;
    const auto add = [&result](const QUuid &track, const RipplePoint &point) {
        auto &list = result[track];
        for (const auto &existing : list)
            if (existing.at == point.at && existing.shift == point.shift) return;
        list.push_back(point);
    };
    for (const auto &entry : raw) {
        if (all) {
            for (const auto &track : tracks) add(track, entry.second);
        } else {
            add(entry.first, entry.second);
        }
    }
    return result;
}

} // namespace QindaTK
