// SPDX-License-Identifier: LGPL-3.0-or-later
#include "timeline_snap.h"

#include "timeline_model.h"

#include <algorithm>

namespace QindaTK {
namespace {
struct Candidate {
    qint64 target = 0;
    quint64 distance = 0;
    int priority = 0;
    int candidateIndex = 0;
    QString kind;
    QUuid id;
};

quint64 distanceBetween(qint64 a, qint64 b)
{
    return a >= b ? quint64(a) - quint64(b) : quint64(b) - quint64(a);
}

bool better(const Candidate &a, const Candidate &b)
{
    if (a.distance != b.distance) return a.distance < b.distance;
    if (a.priority != b.priority) return a.priority < b.priority;
    if (a.target != b.target) return a.target < b.target;
    return a.candidateIndex < b.candidateIndex;
}

void consider(std::optional<Candidate> &best, qint64 edge, qint64 target, quint64 tolerance,
              int priority, int index, const QString &kind, const QUuid &id = {})
{
    const quint64 distance = distanceBetween(edge, target);
    if (distance > tolerance) return;
    const Candidate next{target, distance, priority, index, kind, id};
    if (!best || better(next, *best)) best = next;
}

qint64 saturatingAdd(qint64 value, qint64 delta)
{
    if (const auto sum = TimelineTime::add(value, delta)) return *sum;
    return delta > 0 ? std::numeric_limits<qint64>::max() : std::numeric_limits<qint64>::min();
}

void considerEdges(std::optional<Candidate> &best, const std::vector<TimelineModel::EdgeEntry> &edges,
                   qint64 edge, qint64 tolerance, int priority, int index,
                   const QString &startKind, const QString &endKind,
                   const QSet<QUuid> *excluded, const QUuid &excludedId)
{
    const qint64 low = saturatingAdd(edge, -tolerance);
    const qint64 high = saturatingAdd(edge, tolerance);
    auto it = std::lower_bound(edges.cbegin(), edges.cend(), low,
                               [](const TimelineModel::EdgeEntry &entry, qint64 value) {
                                   return entry.tick < value;
                               });
    for (; it != edges.cend() && it->tick <= high; ++it) {
        if (excluded && excluded->contains(it->id)) continue;
        if (!excludedId.isNull() && it->id == excludedId) continue;
        consider(best, edge, it->tick, quint64(tolerance), priority, index,
                 it->end ? endKind : startKind, it->id);
    }
}
} // namespace

TimelineSnapResult TimelineSnapper::snap(const TimelineModel &model,
                                         const QVector<qint64> &candidates,
                                         const TimelineSnapContext &context)
{
    TimelineSnapResult result;
    if (context.tolerance <= 0 || candidates.isEmpty()) return result;
    quint64 gridNumerator = 0, gridDenominator = 0;
    qint64 gridOrigin = 0;
    const bool hasGrid = model.gridStep(context.grid, gridNumerator, gridDenominator, gridOrigin);
    std::optional<Candidate> best;
    std::optional<Candidate> bestGrid;
    const quint64 tolerance = quint64(context.tolerance);
    for (int i = 0; i < candidates.size(); ++i) {
        const qint64 edge = candidates.at(i);
        if (context.clipEdges)
            considerEdges(best, model.clipEdges(), edge, context.tolerance, 0, i,
                          QStringLiteral("clipStart"), QStringLiteral("clipEnd"),
                          context.excludedClips, {});
        if (context.markers)
            considerEdges(best, model.markerEdges(), edge, context.tolerance, 1, i,
                          QStringLiteral("marker"), QStringLiteral("markerEnd"),
                          nullptr, context.excludedMarker);
        if (context.playhead)
            consider(best, edge, *context.playhead, tolerance, 2, i, QStringLiteral("playhead"));
        if (context.editRange && model.hasEditRange()) {
            consider(best, edge, model.editRangeStartValue(), tolerance, 3, i,
                     QStringLiteral("rangeStart"));
            consider(best, edge, model.editRangeEndValue(), tolerance, 3, i,
                     QStringLiteral("rangeEnd"));
        }
        if (context.origin) consider(best, edge, 0, tolerance, 4, i, QStringLiteral("origin"));
        if (hasGrid) {
            const auto bracket = TimelineTime::gridBracket(edge, gridOrigin, gridNumerator,
                                                           gridDenominator);
            if (bracket) {
                consider(bestGrid, edge, bracket->first, tolerance, 5, i, QStringLiteral("grid"));
                consider(bestGrid, edge, bracket->second, tolerance, 5, i, QStringLiteral("grid"));
            }
        }
    }
    // Dense grid lines would otherwise win against a nearby clip edge.
    if (!best) best = bestGrid;
    if (!best) return result;
    const auto adjustment = TimelineTime::subtract(best->target, candidates.at(best->candidateIndex));
    if (!adjustment) return result;
    result.snapped = true;
    result.target = best->target;
    result.adjustment = *adjustment;
    result.kind = best->kind;
    result.targetId = best->id;
    return result;
}

} // namespace QindaTK
