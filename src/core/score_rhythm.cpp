// SPDX-License-Identifier: LGPL-3.0-or-later
// Rhythm rules of the score model (WP-067, docs/notation.md#rhythm): tuplet
// groups, rest filling and the overwrite rule every entry uses.
#include "score_types.h"

#include <QObject>
#include <algorithm>

namespace QindaTK::Score {

namespace {

qint64 spanOf(const ScoreEvent &event)
{
    return ticks(event.duration).value_or(0);
}

// Gaps of `events` (sorted) inside [from, to).
QVector<std::pair<qint64, qint64>> gapsIn(const QVector<ScoreEvent> &events, qint64 from, qint64 to)
{
    QVector<std::pair<qint64, qint64>> gaps;
    qint64 at = from;
    for (const ScoreEvent &event : events) {
        const qint64 end = event.tick + spanOf(event);
        if (end <= from || event.tick >= to) continue;
        if (event.tick > at) gaps.push_back({at, event.tick});
        at = std::max(at, end);
    }
    if (at < to) gaps.push_back({at, to});
    return gaps;
}

ScoreEvent restEvent(qint64 tick, const ScoreDuration &duration)
{
    ScoreEvent rest;
    rest.id = QUuid::createUuid();
    rest.tick = tick;
    rest.rest = true;
    rest.duration = duration;
    return rest;
}

// Tuplet rests (the group's ratio and unit) filling [from, to) greedily.
std::optional<QVector<ScoreDuration>> tupletFill(qint64 length, const ScoreDuration &like)
{
    QVector<ScoreDuration> out;
    while (length > 0) {
        bool placed = false;
        for (int base = like.unit; base <= 6 && !placed; ++base) {
            const ScoreDuration value{base, 0, like.actual, like.normal, like.unit};
            const auto t = ticks(value);
            if (t && *t <= length) {
                out.push_back(value);
                length -= *t;
                placed = true;
            }
        }
        if (!placed) return std::nullopt;
    }
    return out;
}

} // namespace

QVector<ScoreTupletGroup> tupletGroups(const QVector<ScoreEvent> &events, QString *error)
{
    QVector<ScoreTupletGroup> groups;
    std::optional<ScoreTupletGroup> open;
    int openUnit = 0;
    qint64 filled = 0;
    const auto fail = [&](const QString &message) {
        if (error) *error = message;
        return groups;
    };
    for (int i = 0; i < events.size(); ++i) {
        const ScoreDuration &d = events[i].duration;
        const qint64 t = spanOf(events[i]);
        if (open) {
            const bool contiguous = events[i - 1].tick + spanOf(events[i - 1]) == events[i].tick;
            if (d.actual != open->actual || d.normal != open->normal || d.unit != openUnit || !contiguous)
                return fail(QObject::tr("The tuplet starting at event %1 is incomplete")
                                .arg(idText(events[open->first].id)));
            filled += t;
            open->last = i;
        } else if (d.actual > 1) {
            open = ScoreTupletGroup{i, i, d.actual, d.normal, d.normal * baseTicks(d.unit)};
            openUnit = d.unit;
            filled = t;
        } else {
            continue;
        }
        if (filled > open->span)
            return fail(QObject::tr("The tuplet starting at event %1 is overfull").arg(idText(events[open->first].id)));
        if (filled == open->span) {
            groups.push_back(*open);
            open.reset();
        }
    }
    if (open)
        return fail(QObject::tr("The tuplet starting at event %1 is incomplete").arg(idText(events[open->first].id)));
    if (error) error->clear();
    return groups;
}

std::optional<QVector<ScoreDuration>> restFill(qint64 offset, qint64 length)
{
    QVector<ScoreDuration> out;
    while (length > 0) {
        int chosen = -1;
        for (int base = 0; base <= 6 && chosen < 0; ++base)
            if (baseTicks(base) <= length && offset % baseTicks(base) == 0) chosen = base;
        for (int base = 0; base <= 6 && chosen < 0; ++base)
            if (baseTicks(base) <= length) chosen = base;
        if (chosen < 0) return std::nullopt;
        out.push_back(ScoreDuration{chosen, 0, 1, 1, chosen});
        offset += baseTicks(chosen);
        length -= baseTicks(chosen);
    }
    return out;
}

QString writeEvent(ScoreVoice &voice, ScoreEvent event, qint64 measureStart, qint64 measureEnd,
                   bool fillWholeMeasure, qint64 phase)
{
    event.duration = normalized(event.duration);
    const auto length = ticks(event.duration);
    if (!length) return QObject::tr("%1 is not a whole number of ticks").arg(durationId(event.duration));
    const qint64 a = event.tick, b = a + *length;
    if (a < measureStart || b > measureEnd)
        return QObject::tr("A %1 at tick %2 does not fit in its measure").arg(durationId(event.duration)).arg(a);
    QString problem;
    const QVector<ScoreTupletGroup> groups = tupletGroups(voice.events, &problem);
    if (!problem.isEmpty()) return problem;

    // The tuplet region the new event belongs to, aligned to the measure.
    std::optional<std::pair<qint64, qint64>> region;
    if (event.duration.actual > 1) {
        const qint64 span = event.duration.normal * baseTicks(event.duration.unit);
        const qint64 from = a - (a - measureStart + phase) % span;
        if (from < measureStart || b > from + span || from + span > measureEnd)
            return QObject::tr("The %1 does not fit its tuplet group").arg(durationId(event.duration));
        region = std::pair{from, from + span};
    }
    const auto groupOf = [&groups](int index) -> const ScoreTupletGroup * {
        for (const ScoreTupletGroup &group : groups)
            if (index >= group.first && index <= group.last) return &group;
        return nullptr;
    };
    // A compatible group (same ratio and unit exactly on the region) keeps
    // its other members; any other overlapped group is replaced whole.
    const auto compatible = [&](const ScoreTupletGroup *group) {
        if (!group || !region) return false;
        const ScoreEvent &first = voice.events[group->first];
        return first.tick == region->first && group->span == region->second - region->first
               && first.duration.actual == event.duration.actual && first.duration.normal == event.duration.normal
               && first.duration.unit == event.duration.unit;
    };
    QVector<bool> removed(voice.events.size(), false);
    qint64 clearFrom = a, clearTo = b;
    if (region) {
        clearFrom = std::min(clearFrom, region->first);
        clearTo = std::max(clearTo, region->second);
    }
    for (bool grew = true; grew;) {
        grew = false;
        for (int i = 0; i < voice.events.size(); ++i) {
            const ScoreEvent &old = voice.events[i];
            const qint64 oldEnd = old.tick + spanOf(old);
            if (removed[i] || oldEnd <= clearFrom || old.tick >= clearTo) continue;
            const ScoreTupletGroup *group = groupOf(i);
            if (compatible(group) && (oldEnd <= a || old.tick >= b)) continue; // keeps its place in the group
            const int first = group && !compatible(group) ? group->first : i;
            const int last = group && !compatible(group) ? group->last : i;
            for (int j = first; j <= last; ++j) {
                removed[j] = true;
                const qint64 from = voice.events[j].tick, to = from + spanOf(voice.events[j]);
                if (from < clearFrom || to > clearTo) grew = true;
                clearFrom = std::min(clearFrom, from);
                clearTo = std::max(clearTo, to);
            }
        }
    }
    if (clearFrom < measureStart || clearTo > measureEnd)
        return QObject::tr("The edit would cross a bar line");

    ScoreVoice next = voice;
    next.events.clear();
    for (int i = 0; i < voice.events.size(); ++i)
        if (!removed[i]) next.events.push_back(voice.events[i]);
    next.events.push_back(event);
    const auto byTick = [](const ScoreEvent &x, const ScoreEvent &y) { return x.tick < y.tick; };
    std::sort(next.events.begin(), next.events.end(), byTick);

    const qint64 fillFrom = fillWholeMeasure ? measureStart : clearFrom;
    const qint64 fillTo = fillWholeMeasure ? measureEnd : clearTo;
    for (const auto &[gapFrom, gapTo] : gapsIn(next.events, fillFrom, fillTo)) {
        // Split the gap at the tuplet region: tuplet rests inside, plain rests outside.
        QVector<std::pair<qint64, qint64>> pieces;
        if (region && gapFrom < region->second && gapTo > region->first) {
            if (gapFrom < region->first) pieces.push_back({gapFrom, region->first});
            pieces.push_back({std::max(gapFrom, region->first), std::min(gapTo, region->second)});
            if (gapTo > region->second) pieces.push_back({region->second, gapTo});
        } else {
            pieces.push_back({gapFrom, gapTo});
        }
        for (const auto &[from, to] : pieces) {
            const bool inTuplet = region && from >= region->first && to <= region->second;
            const auto values = inTuplet ? tupletFill(to - from, event.duration)
                                         : restFill(from - measureStart + phase, to - from);
            if (!values) return QObject::tr("No rest fills the %1 ticks left at tick %2").arg(to - from).arg(from);
            qint64 at = from;
            for (const ScoreDuration &value : *values) {
                next.events.push_back(restEvent(at, value));
                at += *ticks(value);
            }
        }
    }
    std::sort(next.events.begin(), next.events.end(), byTick);
    tupletGroups(next.events, &problem);
    if (!problem.isEmpty()) return problem;
    voice = next;
    return {};
}

QString completeVoices(ScoreContent &content)
{
    const QVector<qint64> starts = measureStarts(content);
    for (ScorePart &part : content.parts)
        for (ScoreStaff &staff : part.staves)
            for (ScoreVoice &voice : staff.voices) {
                if (voice.number != 1) continue;
                QVector<ScoreEvent> added;
                for (int m = 0; m + 1 < starts.size(); ++m)
                    for (const auto &[from, to] : gapsIn(voice.events, starts[m], starts[m + 1])) {
                        const auto values = restFill(from - starts[m] + measurePhase(content.measures[m]), to - from);
                        if (!values) return QObject::tr("No rest fills measure %1").arg(m + 1);
                        qint64 at = from;
                        for (const ScoreDuration &value : *values) {
                            added.push_back(restEvent(at, value));
                            at += *ticks(value);
                        }
                    }
                voice.events += added;
                std::sort(voice.events.begin(), voice.events.end(),
                          [](const ScoreEvent &x, const ScoreEvent &y) { return x.tick < y.tick; });
            }
    return {};
}

void dropBrokenTies(ScoreContent &content)
{
    for (ScorePart &part : content.parts)
        for (ScoreStaff &staff : part.staves)
            for (ScoreVoice &voice : staff.voices)
                for (int e = 0; e < voice.events.size(); ++e)
                    for (ScoreNote &note : voice.events[e].notes) {
                        if (!note.tie) continue;
                        const bool adjacent = e + 1 < voice.events.size()
                                              && voice.events[e + 1].tick
                                                     == voice.events[e].tick + spanOf(voice.events[e]);
                        if (!adjacent || tieTarget(note, voice.events[e + 1]) < 0) note.tie = false;
                    }
}

QVariantList rhythmReport(const ScoreContent &content)
{
    const QVector<qint64> starts = measureStarts(content);
    QVariantList rows;
    for (int m = 0; m < content.measures.size(); ++m)
        for (const ScorePart &part : content.parts)
            for (const ScoreStaff &staff : part.staves)
                for (const ScoreVoice &voice : staff.voices) {
                    qint64 filled = 0;
                    for (const ScoreEvent &event : voice.events)
                        if (event.tick >= starts[m] && event.tick < starts[m + 1]) filled += spanOf(event);
                    const qint64 length = starts[m + 1] - starts[m];
                    rows.push_back(QVariantMap{
                        {QStringLiteral("measureId"), idText(content.measures[m].id)},
                        {QStringLiteral("measure"), m + 1},
                        {QStringLiteral("staffId"), idText(staff.id)},
                        {QStringLiteral("voiceId"), idText(voice.id)},
                        {QStringLiteral("voice"), voice.number},
                        {QStringLiteral("filled"), QString::number(filled)},
                        {QStringLiteral("length"), QString::number(length)},
                        {QStringLiteral("status"), filled == length ? QStringLiteral("complete")
                                                   : filled == 0 ? QStringLiteral("empty") : QStringLiteral("partial")}});
                }
    return rows;
}

} // namespace QindaTK::Score
