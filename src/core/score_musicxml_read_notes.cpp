// SPDX-License-Identifier: LGPL-3.0-or-later
// MusicXML import of notes, chords, rests, ties, articulations and
// dynamics into voices (WP-067, D-181, docs/notation.md#interchange).
#include "score_musicxml_read_p.h"

#include <QObject>
#include <algorithm>

namespace QindaTK::ScoreXml {

XmlNote readNote(QXmlStreamReader &xml, Import &import)
{
    XmlNote note;
    note.id = xml.attributes().value(QStringLiteral("id")).toString();
    xmlChildren(xml, [&](const QString &name) {
        if (name == QLatin1String("chord")) { note.chord = true; xml.skipCurrentElement(); }
        else if (name == QLatin1String("rest")) { note.rest = true; xml.skipCurrentElement(); }
        else if (name == QLatin1String("grace")) { note.grace = true; xml.skipCurrentElement(); }
        else if (name == QLatin1String("cue")) { note.cue = true; xml.skipCurrentElement(); }
        else if (name == QLatin1String("pitch")) {
            xmlChildren(xml, [&](const QString &field) {
                const QString text = xml.readElementText();
                if (field == QLatin1String("step")) note.pitch.step = int(QStringLiteral("CDEFGAB").indexOf(text));
                else if (field == QLatin1String("alter")) note.pitch.alter = text.toInt();
                else if (field == QLatin1String("octave")) note.pitch.octave = text.toInt();
            });
        } else if (name == QLatin1String("duration")) {
            note.divisions = xml.readElementText().toLongLong();
        } else if (name == QLatin1String("tie")) {
            note.tieStart = note.tieStart || xml.attributes().value(QStringLiteral("type")) == QLatin1String("start");
            xml.skipCurrentElement();
        } else if (name == QLatin1String("voice")) {
            note.voice = xml.readElementText().trimmed();
        } else if (name == QLatin1String("type")) {
            const auto base = Score::parseBaseName(xml.readElementText().trimmed());
            if (base) { note.duration.base = *base; note.duration.unit = *base; note.hasType = true; }
        } else if (name == QLatin1String("dot")) {
            ++note.duration.dots;
            xml.skipCurrentElement();
        } else if (name == QLatin1String("time-modification")) {
            xmlChildren(xml, [&](const QString &field) {
                const QString text = xml.readElementText().trimmed();
                if (field == QLatin1String("actual-notes")) note.duration.actual = text.toInt();
                else if (field == QLatin1String("normal-notes")) note.duration.normal = text.toInt();
                else if (field == QLatin1String("normal-type")) note.duration.unit = Score::parseBaseName(text).value_or(-1);
            });
        } else if (name == QLatin1String("staff")) {
            note.staff = std::max(1, xml.readElementText().toInt());
        } else if (name == QLatin1String("notations")) {
            xmlChildren(xml, [&](const QString &kind) {
                if (kind == QLatin1String("tied")) {
                    note.tieStart = note.tieStart || xml.attributes().value(QStringLiteral("type")) == QLatin1String("start");
                    xml.skipCurrentElement();
                } else if (kind == QLatin1String("tuplet")) {
                    recomputed(import, xml, kind); // groups follow from the ratios (D-181)
                } else if (kind == QLatin1String("fermata")) {
                    note.articulations.push_back(QStringLiteral("fermata"));
                    xml.skipCurrentElement();
                } else if (kind == QLatin1String("articulations")) {
                    xmlChildren(xml, [&](const QString &mark) {
                        const QString ours = mark == QLatin1String("strong-accent") ? QStringLiteral("marcato") : mark;
                        if (Score::articulationNames().contains(ours)) note.articulations.push_back(ours);
                        else import.unsupported.insert(mark);
                        xml.skipCurrentElement();
                    });
                } else {
                    unsupported(import, xml, kind);
                }
            });
        } else if (name == QLatin1String("stem") || name == QLatin1String("beam") || name == QLatin1String("notehead")
                   || name == QLatin1String("accidental") || name == QLatin1String("instrument")) {
            recomputed(import, xml, name); // engraving hints: the layout recomputes them
        } else {
            unsupported(import, xml, name);
        }
    });
    note.duration = Score::normalized(note.duration);
    return note;
}

void readDirection(QXmlStreamReader &xml, Import &import, PartState &state)
{
    QString dynamic, voice;
    int staff = 1;
    xmlChildren(xml, [&](const QString &name) {
        if (name == QLatin1String("direction-type")) {
            xmlChildren(xml, [&](const QString &kind) {
                if (kind == QLatin1String("dynamics"))
                    xmlChildren(xml, [&](const QString &value) {
                        if (Score::dynamicNames().contains(value)) dynamic = value;
                        else import.unsupported.insert(QStringLiteral("dynamics/") + value);
                        xml.skipCurrentElement();
                    });
                else unsupported(import, xml, kind);
            });
        } else if (name == QLatin1String("voice")) {
            voice = xml.readElementText().trimmed();
        } else if (name == QLatin1String("staff")) {
            staff = std::max(1, xml.readElementText().toInt());
        } else if (name == QLatin1String("sound")) {
            // Tempo belongs to the session tempo map (WP-049), not the score.
            unsupported(import, xml, xml.attributes().hasAttribute(QStringLiteral("tempo")) ? QStringLiteral("tempo")
                                                                                             : QStringLiteral("sound"));
        } else if (name == QLatin1String("offset")) {
            recomputed(import, xml, name);
        } else {
            unsupported(import, xml, name);
        }
    });
    if (!dynamic.isEmpty()) state.dynamics.insert({staff - 1, voice}, dynamic);
}

static int voiceNumber(PartState &state, int staff, const QString &xmlVoice)
{
    const QPair<int, QString> key{staff, xmlVoice};
    if (const auto it = state.voiceNumbers.constFind(key); it != state.voiceNumbers.cend()) return it.value();
    // QindaTK and common writers number voices staff * 4 + n.
    int number = xmlVoice.toInt() - staff * Score::kMaxVoices;
    QSet<int> used;
    for (auto it = state.voiceNumbers.cbegin(); it != state.voiceNumbers.cend(); ++it)
        if (it.key().first == staff) used.insert(it.value());
    if (number < 1 || number > Score::kMaxVoices || used.contains(number)) {
        number = 1;
        while (used.contains(number)) ++number;
    }
    state.voiceNumbers.insert(key, number);
    return number;
}

void addNote(Import &import, PartState &state, const XmlNote &note, qint64 &cursor)
{
    if (note.grace || note.cue) {
        import.unsupported.insert(note.grace ? QStringLiteral("grace") : QStringLiteral("cue"));
        return;
    }
    const int staff = std::min(note.staff, Score::kMaxStaves) - 1;
    const int number = voiceNumber(state, staff, note.voice);
    if (number > Score::kMaxVoices) {
        import.unsupported.insert(QStringLiteral("more than %1 voices on a staff").arg(Score::kMaxVoices));
        return;
    }
    QVector<ScoreEvent> &events = state.events[{staff, number}];
    const qint64 written = note.divisions * Score::kPpq / state.divisions;
    if ((note.divisions * Score::kPpq) % state.divisions) ++import.rounded;
    const auto value = note.hasType ? Score::ticks(note.duration) : std::optional<qint64>();
    if (value && *value != written) ++import.mismatches;
    if (note.chord && !events.isEmpty() && !events.back().rest && !note.rest) {
        events.back().notes.push_back({fromXmlId(note.id), note.pitch, note.tieStart});
        return;
    }
    if (!value) { // a whole-measure rest without a type: fill its duration
        const auto rests = Score::restFill(0, written);
        if (!note.rest || !rests) {
            import.unsupported.insert(QStringLiteral("note without a type"));
            cursor += written;
            return;
        }
        for (const ScoreDuration &duration : *rests) {
            ScoreEvent event;
            event.id = QUuid::createUuid();
            event.tick = cursor;
            event.duration = duration;
            event.rest = true;
            events.push_back(event);
            cursor += *Score::ticks(duration);
        }
        state.pendingEvent = {};
        return;
    }
    ScoreEvent event;
    event.id = !state.pendingEvent.isNull() ? state.pendingEvent : note.rest ? fromXmlId(note.id) : QUuid::createUuid();
    state.pendingEvent = {};
    event.tick = cursor;
    event.duration = note.duration;
    event.rest = note.rest;
    if (!note.rest) event.notes.push_back({fromXmlId(note.id), note.pitch, note.tieStart});
    event.articulations = Score::canonicalArticulations(note.articulations);
    for (const QString &voiceKey : {note.voice, QString()})
        if (const QString dynamic = state.dynamics.take({staff, voiceKey}); !dynamic.isEmpty() && event.dynamic.isEmpty())
            event.dynamic = dynamic;
    events.push_back(event);
    cursor += *value;
}

} // namespace QindaTK::ScoreXml
