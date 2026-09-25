// SPDX-License-Identifier: LGPL-3.0-or-later
// ScoreModel entry and notation edits (WP-067, docs/notation.md#editing):
// note and rest entry with the overwrite rule, durations, dots, ties,
// articulations, dynamics, pitch, spelling, chords and deletion.
#include "score_model.h"

#include <algorithm>

namespace QindaTK {

namespace {

struct Target {
    int part = -1, staff = -1, voice = -1, measure = -1;
    qint64 tick = 0, measureStart = 0, measureEnd = 0;
};

// Resolves an entry request's IDs and exact tick against the content.
QString resolve(const ScoreContent &content, const QString &partId, const QString &staffId, const QString &voiceId,
                const QString &measureId, const QString &positionTicks, Target &target)
{
    if (!Score::findStaff(content, QUuid::fromString(staffId), &target.part, &target.staff))
        return QObject::tr("There is no staff %1").arg(staffId);
    if (!partId.isEmpty() && content.parts[target.part].id != QUuid::fromString(partId))
        return QObject::tr("Staff %1 is not in part %2").arg(staffId, partId);
    const auto &voices = content.parts[target.part].staves[target.staff].voices;
    for (int v = 0; v < voices.size(); ++v)
        if (voices[v].id == QUuid::fromString(voiceId)) target.voice = v;
    if (target.voice < 0) return QObject::tr("There is no voice %1 on that staff").arg(voiceId);
    target.measure = Score::measureIndex(content, QUuid::fromString(measureId));
    if (target.measure < 0) return QObject::tr("There is no measure %1").arg(measureId);
    bool parsed = false;
    target.tick = positionTicks.toLongLong(&parsed);
    const QVector<qint64> starts = Score::measureStarts(content);
    target.measureStart = starts[target.measure];
    target.measureEnd = starts[target.measure + 1];
    if (!parsed || target.tick < target.measureStart || target.tick >= target.measureEnd)
        return QObject::tr("Tick %1 is not in measure %2").arg(positionTicks).arg(target.measure + 1);
    return {};
}

ScorePitch stepped(const ScorePitch &pitch, int steps, int fifths)
{
    const int d = Score::diatonic(pitch) + steps;
    ScorePitch next;
    next.octave = d >= 0 ? d / 7 : -((-d + 6) / 7);
    next.step = d - next.octave * 7;
    next.alter = Score::keyAlter(fifths, next.step);
    return next;
}

QString respell(ScoreNote &note)
{
    const QVector<ScorePitch> options = Score::enharmonics(note.pitch);
    const auto it = std::find(options.begin(), options.end(), note.pitch);
    if (options.size() < 2 || it == options.end()) return QObject::tr("%1 has no other spelling").arg(Score::pitchName(note.pitch));
    note.pitch = (it + 1 == options.end()) ? options.front() : *(it + 1);
    return {};
}

bool validMidi(const ScorePitch &pitch)
{
    const int midi = Score::midiPitch(pitch);
    return midi >= 0 && midi <= 127;
}

} // namespace

QVariantMap ScoreModel::insertNote(const QString &partId, const QString &staffId, const QString &voiceId,
                                   const QString &measureId, const QString &positionTicks, const QString &pitch,
                                   const QString &durationId)
{
    Target target;
    if (const QString problem = resolve(m_content, partId, staffId, voiceId, measureId, positionTicks, target); !problem.isEmpty())
        return failure(problem);
    const auto spelled = Score::parsePitch(pitch);
    if (!spelled) return failure(tr("Unknown pitch %1").arg(pitch));
    const auto duration = Score::parseDuration(durationId);
    if (!duration) return failure(tr("Unknown duration %1").arg(durationId));
    ScoreEvent event;
    event.id = QUuid::createUuid();
    event.tick = target.tick;
    event.duration = *duration;
    event.notes.push_back({QUuid::createUuid(), *spelled, false});
    return editContent(tr("Enter note"), [&](ScoreContent &c) {
        ScoreVoice &voice = c.parts[target.part].staves[target.staff].voices[target.voice];
        const QString problem = Score::writeEvent(voice, event, target.measureStart, target.measureEnd, voice.number == 1);
        Score::dropBrokenTies(c);
        return problem;
    }, {{QStringLiteral("eventId"), Score::idText(event.id)}, {QStringLiteral("noteId"), Score::idText(event.notes.front().id)}});
}

QVariantMap ScoreModel::insertRest(const QString &partId, const QString &staffId, const QString &voiceId,
                                   const QString &measureId, const QString &positionTicks, const QString &durationId)
{
    Target target;
    if (const QString problem = resolve(m_content, partId, staffId, voiceId, measureId, positionTicks, target); !problem.isEmpty())
        return failure(problem);
    const auto duration = Score::parseDuration(durationId);
    if (!duration) return failure(tr("Unknown duration %1").arg(durationId));
    ScoreEvent event;
    event.id = QUuid::createUuid();
    event.tick = target.tick;
    event.duration = *duration;
    event.rest = true;
    return editContent(tr("Enter rest"), [&](ScoreContent &c) {
        ScoreVoice &voice = c.parts[target.part].staves[target.staff].voices[target.voice];
        const QString problem = Score::writeEvent(voice, event, target.measureStart, target.measureEnd, voice.number == 1);
        Score::dropBrokenTies(c);
        return problem;
    }, {{QStringLiteral("eventId"), Score::idText(event.id)}});
}

QVariantMap ScoreModel::editEvent(const QString &eventId, const QString &edit, const QVariant &value)
{
    const auto ref = Score::findEvent(m_content, QUuid::fromString(eventId));
    if (!ref.valid()) return failure(tr("There is no event %1").arg(eventId));
    const QVector<qint64> starts = Score::measureStarts(m_content);
    const ScoreEvent &original = m_content.parts[ref.part].staves[ref.staff].voices[ref.voice].events[ref.event];
    const int measure = Score::measureIndexAt(starts, original.tick);
    const int fifths = m_content.measures[measure].fifths;
    const QString text = value.toString();
    const auto noteIndex = [](const ScoreEvent &event, const QString &noteId) {
        for (int n = 0; n < event.notes.size(); ++n)
            if (event.notes[n].id == QUuid::fromString(noteId)) return n;
        return -1;
    };
    const auto rewrite = [&](ScoreContent &c, ScoreEvent event) {
        ScoreVoice &voice = c.parts[ref.part].staves[ref.staff].voices[ref.voice];
        return Score::writeEvent(voice, event, starts[measure], starts[measure + 1], voice.number == 1);
    };
    const auto apply = [&](const QString &label, const std::function<QString(ScoreContent &, ScoreEvent &)> &change) {
        return editContent(label, [&](ScoreContent &c) {
            ScoreEvent &event = c.parts[ref.part].staves[ref.staff].voices[ref.voice].events[ref.event];
            QString problem = change(c, event);
            if (problem.isEmpty()) Score::dropBrokenTies(c);
            return problem;
        }, {{QStringLiteral("eventId"), eventId}});
    };

    if (edit == QLatin1String("duration") || edit == QLatin1String("dot")) {
        std::optional<ScoreDuration> duration = edit == QLatin1String("dot") ? std::optional(original.duration)
                                                                             : Score::parseDuration(text);
        if (!duration) return failure(tr("Unknown duration %1").arg(text));
        if (edit == QLatin1String("dot")) duration->dots = duration->dots ? 0 : 1;
        return apply(tr("Change duration"), [&](ScoreContent &c, ScoreEvent &event) {
            ScoreEvent changed = event;
            changed.duration = *duration;
            return rewrite(c, changed);
        });
    }
    if (edit == QLatin1String("tie")) {
        if (original.rest) return failure(tr("A rest cannot be tied"));
        return apply(tr("Tie"), [&](ScoreContent &, ScoreEvent &event) {
            const int only = text.isEmpty() ? -1 : noteIndex(event, text);
            if (!text.isEmpty() && only < 0) return tr("There is no note %1 in the event").arg(text);
            bool anyUntied = false;
            for (int n = 0; n < event.notes.size(); ++n) anyUntied = anyUntied || ((only < 0 || n == only) && !event.notes[n].tie);
            for (int n = 0; n < event.notes.size(); ++n)
                if (only < 0 || n == only) event.notes[n].tie = anyUntied;
            return QString();
        });
    }
    if (edit == QLatin1String("articulation")) {
        if (!Score::articulationNames().contains(text)) return failure(tr("Unknown articulation %1").arg(text));
        if (original.rest && text != QLatin1String("fermata")) return failure(tr("Only a fermata goes on a rest"));
        return apply(tr("Articulation"), [&](ScoreContent &, ScoreEvent &event) {
            QStringList names = event.articulations;
            if (names.contains(text)) names.removeAll(text); else names.push_back(text);
            event.articulations = Score::canonicalArticulations(names);
            return QString();
        });
    }
    if (edit == QLatin1String("dynamic")) {
        if (!text.isEmpty() && !Score::dynamicNames().contains(text)) return failure(tr("Unknown dynamic %1").arg(text));
        return apply(tr("Dynamic"), [&](ScoreContent &, ScoreEvent &event) { event.dynamic = text; return QString(); });
    }
    if (edit == QLatin1String("pitch") || edit == QLatin1String("addPitch") || edit == QLatin1String("note")) {
        const QVariantMap map = value.toMap();
        const QString name = map.isEmpty() ? text : map.value(QStringLiteral("pitch")).toString();
        const auto pitch = Score::parsePitch(name);
        if (!pitch) return failure(tr("Unknown pitch %1").arg(name));
        return apply(tr("Change pitch"), [&](ScoreContent &, ScoreEvent &event) {
            if (event.rest || edit != QLatin1String("pitch")) {
                for (const ScoreNote &note : event.notes)
                    if (note.pitch == *pitch) return tr("The chord already holds %1").arg(name);
                if (event.notes.size() >= Score::kMaxChord) return tr("A chord holds at most %1 notes").arg(Score::kMaxChord);
                if (event.rest) event.articulations = Score::canonicalArticulations(event.articulations);
                event.rest = false;
                event.notes.push_back({QUuid::createUuid(), *pitch, false});
                return QString();
            }
            const int n = map.isEmpty() ? 0 : noteIndex(event, map.value(QStringLiteral("noteId")).toString());
            if (n < 0) return tr("There is no such note in the event");
            event.notes[n].pitch = *pitch;
            return QString();
        });
    }
    if (edit == QLatin1String("step") || edit == QLatin1String("transpose")) {
        const int amount = value.toInt();
        if (original.rest || amount == 0) return failure(tr("Only notes can move by a step or semitones"));
        return apply(edit == QLatin1String("step") ? tr("Move by step") : tr("Transpose"),
                     [&](ScoreContent &, ScoreEvent &event) {
            for (ScoreNote &note : event.notes) {
                note.pitch = edit == QLatin1String("step") ? stepped(note.pitch, amount, fifths)
                                                           : Score::spell(Score::midiPitch(note.pitch) + amount, fifths);
                if (!validMidi(note.pitch)) return tr("The note would leave the MIDI range");
            }
            return QString();
        });
    }
    if (edit == QLatin1String("respell")) {
        if (original.rest) return failure(tr("A rest has no spelling"));
        return apply(tr("Respell"), [&](ScoreContent &, ScoreEvent &event) {
            for (int n = 0; n < event.notes.size(); ++n)
                if (text.isEmpty() || event.notes[n].id == QUuid::fromString(text))
                    if (const QString problem = respell(event.notes[n]); !problem.isEmpty()) return problem;
            return QString();
        });
    }
    if (edit == QLatin1String("removePitch")) {
        return apply(tr("Remove note"), [&](ScoreContent &, ScoreEvent &event) {
            const int n = noteIndex(event, text);
            if (n < 0) return tr("There is no note %1 in the event").arg(text);
            event.notes.removeAt(n);
            if (event.notes.isEmpty()) {
                event.rest = true;
                event.articulations.removeIf([](const QString &name) { return name != QLatin1String("fermata"); });
            }
            return QString();
        });
    }
    if (edit == QLatin1String("rest")) {
        return apply(tr("Make rest"), [&](ScoreContent &, ScoreEvent &event) {
            event.rest = true;
            event.notes.clear();
            event.articulations.removeIf([](const QString &name) { return name != QLatin1String("fermata"); });
            return QString();
        });
    }
    return failure(tr("Unknown notation edit %1").arg(edit));
}

QVariantMap ScoreModel::deleteEvents(const QStringList &eventIds)
{
    for (const QString &id : eventIds)
        if (!Score::findEvent(m_content, QUuid::fromString(id)).valid()) return failure(tr("There is no event %1").arg(id));
    return editContent(tr("Delete"), [&](ScoreContent &c) {
        for (const QString &id : eventIds) {
            const auto ref = Score::findEvent(c, QUuid::fromString(id));
            if (!ref.valid()) continue; // removed with its tuplet group
            ScoreVoice &voice = c.parts[ref.part].staves[ref.staff].voices[ref.voice];
            ScoreEvent &event = voice.events[ref.event];
            if (!event.rest) {
                event.rest = true;
                event.notes.clear();
                event.articulations.removeIf([](const QString &name) { return name != QLatin1String("fermata"); });
                continue;
            }
            if (voice.number == 1) continue; // voice 1 keeps its rests
            QString problem;
            const QVector<ScoreTupletGroup> groups = Score::tupletGroups(voice.events, &problem);
            int first = ref.event, last = ref.event;
            for (const ScoreTupletGroup &group : groups)
                if (ref.event >= group.first && ref.event <= group.last) {
                    first = group.first;
                    last = group.last;
                }
            bool allRests = true;
            for (int i = first; i <= last; ++i) allRests = allRests && voice.events[i].rest;
            if (!allRests) continue; // a tuplet keeps its rest while it holds notes
            voice.events.remove(first, last - first + 1);
        }
        Score::dropBrokenTies(c);
        return QString();
    });
}

} // namespace QindaTK
