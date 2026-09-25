// SPDX-License-Identifier: LGPL-3.0-or-later
// Score engraving, part 2 (WP-067, D-179, docs/notation.md#layout):
// accidentals in key and measure context, stem directions, beams by beat,
// flags, rest positions in multi-voice measures, ties (also across systems),
// tuplet brackets and articulation placement.
#include "score_layout.h"

#include <algorithm>
#include <cmath>

namespace QindaTK::Score {

namespace {

QString accidentalName(int alter)
{
    switch (alter) {
    case -2: return QStringLiteral("double-flat");
    case -1: return QStringLiteral("flat");
    case 1: return QStringLiteral("sharp");
    case 2: return QStringLiteral("double-sharp");
    default: break;
    }
    return QStringLiteral("natural");
}

// Beaming unit: a dotted quarter in compound eighth meters, else the beat.
qint64 beamUnit(int numerator, int denominator)
{
    if (denominator == 8 && numerator % 3 == 0) return 3 * (kWholeTicks / 8);
    if (denominator >= 8) return kPpq;
    return kWholeTicks / denominator;
}

double minStep(const ScoreEventLayout &event)
{
    double value = event.step;
    for (const ScoreHeadLayout &head : event.heads) value = std::min(value, head.step);
    return value;
}

double maxStep(const ScoreEventLayout &event)
{
    double value = event.step;
    for (const ScoreHeadLayout &head : event.heads) value = std::max(value, head.step);
    return value;
}

double stemX(const ScoreEventLayout &event, double sp)
{
    return event.x + (event.stem > 0 ? 0.55 : -0.55) * sp;
}

void beamGroup(QVector<ScoreEventLayout *> &group, bool multi, int voiceNumber, ScoreStaffLayout &layout, double sp)
{
    int stem = multi ? (voiceNumber % 2 ? 1 : -1) : 0;
    if (!stem) {
        double centre = 0;
        for (const ScoreEventLayout *event : group) centre += (minStep(*event) + maxStep(*event)) / 2.0;
        stem = centre / group.size() >= 4.0 ? -1 : 1;
    }
    for (ScoreEventLayout *event : group) {
        event->stem = stem;
        event->beamed = true;
        event->flags = 0;
    }
    const double x0 = stemX(*group.front(), sp), x1 = stemX(*group.back(), sp);
    const auto tip = [stem](const ScoreEventLayout &event) { return stem > 0 ? maxStep(event) + 7 : minStep(event) - 7; };
    double s0 = tip(*group.front()), s1 = tip(*group.back());
    if (std::abs(s1 - s0) > 2.0) s1 = s0 + (s1 > s0 ? 2.0 : -2.0);
    const auto lineAt = [&](double x) { return x1 == x0 ? s0 : s0 + (s1 - s0) * (x - x0) / (x1 - x0); };
    double shift = 0;
    for (const ScoreEventLayout *event : group) {
        const double at = lineAt(stemX(*event, sp));
        shift = stem > 0 ? std::max(shift, maxStep(*event) + 5 - at) : std::min(shift, minStep(*event) - 5 - at);
    }
    s0 += shift;
    s1 += shift;
    for (ScoreEventLayout *event : group) event->stemEnd = lineAt(stemX(*event, sp));
    layout.beams.push_back({x0, s0, x1, s1, stem > 0});
    // Secondary beams: runs of events with at least `level` beams.
    const double inward = stem > 0 ? -1.5 : 1.5;
    for (int level = 2; level <= 5; ++level) {
        for (int i = 0; i < group.size();) {
            if (group[i]->base - 2 < level) { ++i; continue; }
            int j = i;
            while (j + 1 < group.size() && group[j + 1]->base - 2 >= level) ++j;
            const double offset = inward * (level - 1);
            double a = stemX(*group[i], sp), b = stemX(*group[j], sp);
            if (i == j) { // a stub toward the neighbour
                const bool left = i + 1 == group.size();
                b = a + (left ? -1.0 : 1.0) * sp;
            }
            layout.beams.push_back({std::min(a, b), lineAt(std::min(a, b)) + offset, std::max(a, b),
                                    lineAt(std::max(a, b)) + offset, stem > 0});
            i = j + 1;
        }
    }
}

} // namespace

QSet<QUuid> tieContinuations(const ScoreContent &content)
{
    QSet<QUuid> into;
    for (const ScorePart &part : content.parts)
        for (const ScoreStaff &staff : part.staves)
            for (const ScoreVoice &voice : staff.voices)
                for (int e = 0; e + 1 < voice.events.size(); ++e)
                    for (const ScoreNote &note : voice.events[e].notes) {
                        const int target = tieTarget(note, voice.events[e + 1]);
                        if (target >= 0) into.insert(voice.events[e + 1].notes[target].id);
                    }
    return into;
}

QHash<QUuid, QString> accidentals(const ScoreContent &content, const ScoreStaff &staff, const QSet<QUuid> &tiedInto)
{
    QHash<QUuid, QString> shown;
    const QVector<qint64> starts = measureStarts(content);
    for (int m = 0; m < content.measures.size(); ++m) {
        QVector<std::pair<int, const ScoreEvent *>> events; // (voice, event)
        for (const ScoreVoice &voice : staff.voices)
            for (const ScoreEvent &event : voice.events)
                if (event.tick >= starts[m] && event.tick < starts[m + 1] && !event.rest)
                    events.push_back({voice.number, &event});
        std::stable_sort(events.begin(), events.end(), [](const auto &a, const auto &b) {
            return a.second->tick != b.second->tick ? a.second->tick < b.second->tick : a.first < b.first;
        });
        QHash<int, int> state; // diatonic -> alteration in force
        for (const auto &[voice, event] : events)
            for (const ScoreNote &note : event->notes) {
                const int d = diatonic(note.pitch);
                const int current = state.contains(d) ? state.value(d) : keyAlter(content.measures[m].fifths, note.pitch.step);
                if (!tiedInto.contains(note.id) && note.pitch.alter != current)
                    shown.insert(note.id, accidentalName(note.pitch.alter));
                state.insert(d, note.pitch.alter);
            }
    }
    return shown;
}

void engraveStaff(const ScoreContent &content, const ScoreStaff &staff, ScoreStaffLayout &layout,
                  const QSet<QUuid> &tiedInto, double sp)
{
    QHash<QUuid, ScoreEventLayout *> byEvent;
    QHash<QUuid, std::pair<ScoreEventLayout *, int>> byNote;
    for (ScoreMeasureLayout &measure : layout.measures) {
        int occupied = 0;
        for (const ScoreVoiceLayout &voice : measure.voices) occupied += voice.events.isEmpty() ? 0 : 1;
        const bool multi = occupied > 1;
        const qint64 unit = beamUnit(measure.numerator, measure.denominator);
        for (ScoreVoiceLayout &voice : measure.voices) {
            for (ScoreEventLayout &event : voice.events) {
                byEvent.insert(event.id, &event);
                for (int h = 0; h < event.heads.size(); ++h) byNote.insert(event.heads[h].noteId, {&event, h});
                if (event.rest) {
                    event.step = multi ? (voice.number % 2 ? 6 : 2) : 4;
                    continue;
                }
                event.stem = event.base < 1 ? 0
                             : multi ? (voice.number % 2 ? 1 : -1)
                             : (minStep(event) + maxStep(event)) / 2.0 >= 4.0 ? -1 : 1;
                event.flags = event.base >= 3 ? event.base - 2 : 0;
                event.stemEnd = event.stem > 0 ? std::max(maxStep(event) + 7, 4.0)
                                : event.stem < 0 ? std::min(minStep(event) - 7, 4.0) : event.step;
                // Seconds in a chord: the upper (stem up) or lower (stem down) head moves aside.
                QVector<int> order(event.heads.size());
                for (int h = 0; h < order.size(); ++h) order[h] = h;
                std::sort(order.begin(), order.end(),
                          [&event](int a, int b) { return event.heads[a].step < event.heads[b].step; });
                for (int i = 1; i < order.size(); ++i) {
                    ScoreHeadLayout &lower = event.heads[order[i - 1]];
                    ScoreHeadLayout &upper = event.heads[order[i]];
                    if (upper.step - lower.step == 1 && lower.dx == 0 && upper.dx == 0) {
                        if (event.stem >= 0) upper.dx = 1.1 * sp;
                        else lower.dx = -1.1 * sp;
                    }
                }
                const bool above = event.stem < 0;
                event.articulationStep = above ? maxStep(event) + 2.5 : minStep(event) - 2.5;
            }
            // Beams: runs of eighths or shorter inside one beaming unit.
            QVector<ScoreEventLayout *> run;
            qint64 runUnit = -1;
            const auto flush = [&] {
                if (run.size() >= 2) beamGroup(run, multi, voice.number, layout, sp);
                run.clear();
            };
            for (ScoreEventLayout &event : voice.events) {
                const qint64 at = (event.tick - measure.start) / unit;
                if (event.rest || event.base < 3 || at != runUnit) flush();
                runUnit = at;
                if (!event.rest && event.base >= 3) run.push_back(&event);
            }
            flush();
            for (ScoreEventLayout &event : voice.events)
                if (!event.rest && event.stem != 0)
                    event.articulationStep = event.stem < 0 ? std::max(maxStep(event) + 2.5, event.articulationStep)
                                                            : std::min(minStep(event) - 2.5, event.articulationStep);
        }
    }

    // Ties, including halves into and out of the system.
    for (const ScoreVoice &voice : staff.voices)
        for (int e = 0; e + 1 < voice.events.size(); ++e)
            for (const ScoreNote &note : voice.events[e].notes) {
                const int target = tieTarget(note, voice.events[e + 1]);
                if (target < 0) continue;
                const auto from = byNote.value(note.id, {nullptr, 0});
                const auto to = byNote.value(voice.events[e + 1].notes[target].id, {nullptr, 0});
                if (!from.first && !to.first) continue;
                const ScoreEventLayout *anchor = from.first ? from.first : to.first;
                const double step = anchor->heads[from.first ? from.second : to.second].step;
                const double x0 = from.first ? from.first->x + 0.7 * sp
                                             : layout.measures.front().contentX - 0.5 * sp;
                const double x1 = to.first ? to.first->x - 0.7 * sp : layout.x1 - 0.3 * sp;
                layout.ties.push_back({x0, x1, step, anchor->stem < 0});
            }
    Q_UNUSED(tiedInto);

    // Tuplet brackets on the stem side (above for rests and stemless notes).
    for (const ScoreVoice &voice : staff.voices) {
        QString problem;
        for (const ScoreTupletGroup &group : tupletGroups(voice.events, &problem)) {
            ScoreEventLayout *first = byEvent.value(voice.events[group.first].id);
            ScoreEventLayout *last = byEvent.value(voice.events[group.last].id);
            if (!first || !last) continue;
            bool below = false;
            for (int i = group.first; i <= group.last; ++i)
                if (const ScoreEventLayout *event = byEvent.value(voice.events[i].id); event && !event->rest) {
                    below = event->stem < 0;
                    break;
                }
            double step = below ? 0.0 : 8.0;
            for (int i = group.first; i <= group.last; ++i) {
                const ScoreEventLayout *event = byEvent.value(voice.events[i].id);
                if (!event) continue;
                step = below ? std::min({step, minStep(*event) - 2, event->stem ? event->stemEnd - 1.5 : step})
                             : std::max({step, maxStep(*event) + 2, event->stem ? event->stemEnd + 1.5 : step});
            }
            const bool plain = group.normal * 2 > group.actual && (group.normal & (group.normal - 1)) == 0;
            layout.tuplets.push_back({first->x - 0.6 * sp, last->x + 0.6 * sp, step,
                                      plain ? QString::number(group.actual)
                                            : QStringLiteral("%1:%2").arg(group.actual).arg(group.normal)});
        }
    }
    Q_UNUSED(content);
}

} // namespace QindaTK::Score
