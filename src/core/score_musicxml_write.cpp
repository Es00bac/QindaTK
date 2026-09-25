// SPDX-License-Identifier: LGPL-3.0-or-later
// MusicXML 4.0 partwise export (WP-067, D-181, docs/notation.md#interchange).
// Native IDs travel as `id` attributes (N-/M-/P- prefixed) and qindatk-*
// processing instructions, which other readers ignore.
#include "score_model.h"

#include <QSaveFile>
#include <QXmlStreamWriter>
#include <algorithm>

namespace QindaTK {

namespace {

QString xmlId(QChar prefix, const QUuid &id)
{
    return QString(prefix) + QLatin1Char('-') + Score::idText(id);
}

void writeClef(QXmlStreamWriter &xml, int number, ScoreClef clef)
{
    xml.writeStartElement(QStringLiteral("clef"));
    xml.writeAttribute(QStringLiteral("number"), QString::number(number));
    xml.writeTextElement(QStringLiteral("sign"), clef == ScoreClef::Bass ? QStringLiteral("F")
                                                 : clef == ScoreClef::Alto ? QStringLiteral("C") : QStringLiteral("G"));
    xml.writeTextElement(QStringLiteral("line"), clef == ScoreClef::Bass ? QStringLiteral("4")
                                                 : clef == ScoreClef::Alto ? QStringLiteral("3") : QStringLiteral("2"));
    xml.writeEndElement();
}

void writeValue(QXmlStreamWriter &xml, const ScoreDuration &duration)
{
    xml.writeTextElement(QStringLiteral("type"), Score::baseName(duration.base));
    for (int d = 0; d < duration.dots; ++d) xml.writeEmptyElement(QStringLiteral("dot"));
    if (duration.actual > 1) {
        xml.writeStartElement(QStringLiteral("time-modification"));
        xml.writeTextElement(QStringLiteral("actual-notes"), QString::number(duration.actual));
        xml.writeTextElement(QStringLiteral("normal-notes"), QString::number(duration.normal));
        if (duration.unit != duration.base) xml.writeTextElement(QStringLiteral("normal-type"), Score::baseName(duration.unit));
        xml.writeEndElement();
    }
}

struct EventContext {
    int staff = 1;         // 1-based
    QString voice;         // MusicXML voice text
    bool tupletStart = false;
    bool tupletStop = false;
};

void writeEvent(QXmlStreamWriter &xml, const ScoreEvent &event, const EventContext &context, const QSet<QUuid> &tiedInto)
{
    const qint64 length = Score::ticks(event.duration).value_or(0);
    if (!event.dynamic.isEmpty()) {
        xml.writeStartElement(QStringLiteral("direction"));
        xml.writeAttribute(QStringLiteral("placement"), QStringLiteral("below"));
        xml.writeStartElement(QStringLiteral("direction-type"));
        xml.writeStartElement(QStringLiteral("dynamics"));
        xml.writeEmptyElement(event.dynamic);
        xml.writeEndElement();
        xml.writeEndElement();
        xml.writeTextElement(QStringLiteral("voice"), context.voice);
        xml.writeTextElement(QStringLiteral("staff"), QString::number(context.staff));
        xml.writeEndElement();
    }
    xml.writeProcessingInstruction(QStringLiteral("qindatk-event"), Score::idText(event.id));
    const int count = event.rest ? 1 : int(event.notes.size());
    for (int n = 0; n < count; ++n) {
        const bool first = n == 0;
        xml.writeStartElement(QStringLiteral("note"));
        xml.writeAttribute(QStringLiteral("id"), xmlId(QLatin1Char('N'), event.rest ? event.id : event.notes[n].id));
        if (!first) xml.writeEmptyElement(QStringLiteral("chord"));
        const ScoreNote *note = event.rest ? nullptr : &event.notes[n];
        if (note) {
            xml.writeStartElement(QStringLiteral("pitch"));
            xml.writeTextElement(QStringLiteral("step"), QString(QChar::fromLatin1("CDEFGAB"[note->pitch.step])));
            if (note->pitch.alter) xml.writeTextElement(QStringLiteral("alter"), QString::number(note->pitch.alter));
            xml.writeTextElement(QStringLiteral("octave"), QString::number(note->pitch.octave));
            xml.writeEndElement();
        } else {
            xml.writeEmptyElement(QStringLiteral("rest"));
        }
        xml.writeTextElement(QStringLiteral("duration"), QString::number(length));
        const bool tieStop = note && tiedInto.contains(note->id);
        const bool tieStart = note && note->tie;
        if (tieStop) {
            xml.writeEmptyElement(QStringLiteral("tie"));
            xml.writeAttribute(QStringLiteral("type"), QStringLiteral("stop"));
        }
        if (tieStart) {
            xml.writeEmptyElement(QStringLiteral("tie"));
            xml.writeAttribute(QStringLiteral("type"), QStringLiteral("start"));
        }
        xml.writeTextElement(QStringLiteral("voice"), context.voice);
        writeValue(xml, event.duration);
        xml.writeTextElement(QStringLiteral("staff"), QString::number(context.staff));
        const QStringList marks = first ? event.articulations : QStringList();
        const bool hasArticulations = std::any_of(marks.begin(), marks.end(),
                                                  [](const QString &m) { return m != QLatin1String("fermata"); });
        const bool tuplet = first && (context.tupletStart || context.tupletStop);
        if (tieStop || tieStart || tuplet || !marks.isEmpty()) {
            xml.writeStartElement(QStringLiteral("notations"));
            if (tieStop) {
                xml.writeEmptyElement(QStringLiteral("tied"));
                xml.writeAttribute(QStringLiteral("type"), QStringLiteral("stop"));
            }
            if (tieStart) {
                xml.writeEmptyElement(QStringLiteral("tied"));
                xml.writeAttribute(QStringLiteral("type"), QStringLiteral("start"));
            }
            if (first && context.tupletStart) {
                xml.writeEmptyElement(QStringLiteral("tuplet"));
                xml.writeAttribute(QStringLiteral("type"), QStringLiteral("start"));
            }
            if (first && context.tupletStop) {
                xml.writeEmptyElement(QStringLiteral("tuplet"));
                xml.writeAttribute(QStringLiteral("type"), QStringLiteral("stop"));
            }
            if (hasArticulations) {
                xml.writeStartElement(QStringLiteral("articulations"));
                for (const QString &mark : marks)
                    if (mark != QLatin1String("fermata"))
                        xml.writeEmptyElement(mark == QLatin1String("marcato") ? QStringLiteral("strong-accent") : mark);
                xml.writeEndElement();
            }
            if (marks.contains(QStringLiteral("fermata"))) xml.writeEmptyElement(QStringLiteral("fermata"));
            xml.writeEndElement();
        }
        xml.writeEndElement(); // note
    }
}

} // namespace

QVariantMap ScoreModel::exportMusicXml(const QString &file, const QVariantMap &options) const
{
    QSet<QUuid> only;
    for (const QVariant &id : options.value(QStringLiteral("parts")).toList()) only.insert(QUuid::fromString(id.toString()));
    QSaveFile out(file);
    if (!out.open(QIODevice::WriteOnly))
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), tr("Cannot write %1").arg(file)}};
    const ScoreContent &c = m_content;
    const QVector<qint64> starts = Score::measureStarts(c);
    const QSet<QUuid> tiedInto = Score::tieContinuations(c);
    QXmlStreamWriter xml(&out);
    xml.setAutoFormatting(true);
    xml.writeStartDocument();
    xml.writeDTD(QStringLiteral("<!DOCTYPE score-partwise PUBLIC \"-//Recordare//DTD MusicXML 4.0 Partwise//EN\" "
                                "\"http://www.musicxml.org/dtds/partwise.dtd\">"));
    xml.writeStartElement(QStringLiteral("score-partwise"));
    xml.writeAttribute(QStringLiteral("version"), QStringLiteral("4.0"));
    xml.writeProcessingInstruction(QStringLiteral("qindatk-score"), Score::idText(c.id));
    xml.writeStartElement(QStringLiteral("work"));
    xml.writeTextElement(QStringLiteral("work-title"), c.title);
    xml.writeEndElement();
    xml.writeStartElement(QStringLiteral("identification"));
    if (!c.composer.isEmpty()) {
        xml.writeStartElement(QStringLiteral("creator"));
        xml.writeAttribute(QStringLiteral("type"), QStringLiteral("composer"));
        xml.writeCharacters(c.composer);
        xml.writeEndElement();
    }
    xml.writeStartElement(QStringLiteral("encoding"));
    xml.writeTextElement(QStringLiteral("software"), QStringLiteral("QindaTK"));
    xml.writeEndElement();
    xml.writeEndElement();
    xml.writeStartElement(QStringLiteral("part-list"));
    int written = 0;
    for (const ScorePart &part : c.parts) {
        if (!only.isEmpty() && !only.contains(part.id)) continue;
        ++written;
        xml.writeStartElement(QStringLiteral("score-part"));
        xml.writeAttribute(QStringLiteral("id"), xmlId(QLatin1Char('P'), part.id));
        xml.writeTextElement(QStringLiteral("part-name"), part.name);
        xml.writeTextElement(QStringLiteral("part-abbreviation"), part.abbreviation);
        xml.writeStartElement(QStringLiteral("midi-instrument"));
        xml.writeAttribute(QStringLiteral("id"), xmlId(QLatin1Char('I'), part.id));
        xml.writeTextElement(QStringLiteral("midi-channel"), QString::number(part.channel + 1));
        xml.writeEndElement();
        xml.writeEndElement();
    }
    xml.writeEndElement(); // part-list
    if (!written) return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), tr("No part to export")}};

    int events = 0;
    for (const ScorePart &part : c.parts) {
        if (!only.isEmpty() && !only.contains(part.id)) continue;
        xml.writeStartElement(QStringLiteral("part"));
        xml.writeAttribute(QStringLiteral("id"), xmlId(QLatin1Char('P'), part.id));
        for (int s = 0; s < part.staves.size(); ++s) {
            xml.writeProcessingInstruction(QStringLiteral("qindatk-staff"),
                                           QStringLiteral("%1 %2").arg(s + 1).arg(Score::idText(part.staves[s].id)));
            for (const ScoreVoice &voice : part.staves[s].voices)
                xml.writeProcessingInstruction(QStringLiteral("qindatk-voice"),
                                               QStringLiteral("%1 %2 %3").arg(s + 1).arg(voice.number).arg(Score::idText(voice.id)));
        }
        QVector<QVector<ScoreTupletGroup>> groups;
        for (const ScoreStaff &staff : part.staves)
            for (const ScoreVoice &voice : staff.voices) {
                QString problem;
                groups.push_back(Score::tupletGroups(voice.events, &problem));
            }
        for (int m = 0; m < c.measures.size(); ++m) {
            const ScoreMeasure &measure = c.measures[m];
            xml.writeStartElement(QStringLiteral("measure"));
            xml.writeAttribute(QStringLiteral("number"), QString::number(m + 1));
            xml.writeAttribute(QStringLiteral("id"), xmlId(QLatin1Char('M'), measure.id));
            if (measure.pickup > 0) xml.writeAttribute(QStringLiteral("implicit"), QStringLiteral("yes"));
            if (m > 0 && c.measures[m - 1].breakAfter != ScoreBreak::None) {
                xml.writeEmptyElement(QStringLiteral("print"));
                xml.writeAttribute(c.measures[m - 1].breakAfter == ScoreBreak::Page ? QStringLiteral("new-page")
                                                                                    : QStringLiteral("new-system"),
                                   QStringLiteral("yes"));
            }
            const bool keyChange = m == 0 || measure.fifths != c.measures[m - 1].fifths;
            const bool timeChange = m == 0 || measure.numerator != c.measures[m - 1].numerator
                                    || measure.denominator != c.measures[m - 1].denominator;
            if (keyChange || timeChange) {
                xml.writeStartElement(QStringLiteral("attributes"));
                if (m == 0) xml.writeTextElement(QStringLiteral("divisions"), QString::number(Score::kPpq));
                if (keyChange) {
                    xml.writeStartElement(QStringLiteral("key"));
                    xml.writeTextElement(QStringLiteral("fifths"), QString::number(measure.fifths));
                    xml.writeEndElement();
                }
                if (timeChange) {
                    xml.writeStartElement(QStringLiteral("time"));
                    xml.writeTextElement(QStringLiteral("beats"), QString::number(measure.numerator));
                    xml.writeTextElement(QStringLiteral("beat-type"), QString::number(measure.denominator));
                    xml.writeEndElement();
                }
                if (m == 0) {
                    xml.writeTextElement(QStringLiteral("staves"), QString::number(part.staves.size()));
                    for (int s = 0; s < part.staves.size(); ++s) writeClef(xml, s + 1, part.staves[s].clef);
                }
                xml.writeEndElement();
            }
            qint64 cursor = starts[m];
            int voiceIndex = 0;
            for (int s = 0; s < part.staves.size(); ++s)
                for (const ScoreVoice &voice : part.staves[s].voices) {
                    const QVector<ScoreTupletGroup> &voiceGroups = groups[voiceIndex++];
                    EventContext context;
                    context.staff = s + 1;
                    context.voice = QString::number(s * Score::kMaxVoices + voice.number);
                    for (int e = 0; e < voice.events.size(); ++e) {
                        const ScoreEvent &event = voice.events[e];
                        if (event.tick < starts[m] || event.tick >= starts[m + 1]) continue;
                        if (event.tick < cursor) {
                            xml.writeStartElement(QStringLiteral("backup"));
                            xml.writeTextElement(QStringLiteral("duration"), QString::number(cursor - event.tick));
                            xml.writeEndElement();
                        } else if (event.tick > cursor) {
                            xml.writeStartElement(QStringLiteral("forward"));
                            xml.writeTextElement(QStringLiteral("duration"), QString::number(event.tick - cursor));
                            xml.writeTextElement(QStringLiteral("voice"), context.voice);
                            xml.writeTextElement(QStringLiteral("staff"), QString::number(context.staff));
                            xml.writeEndElement();
                        }
                        context.tupletStart = context.tupletStop = false;
                        for (const ScoreTupletGroup &group : voiceGroups) {
                            context.tupletStart = context.tupletStart || group.first == e;
                            context.tupletStop = context.tupletStop || group.last == e;
                        }
                        writeEvent(xml, event, context, tiedInto);
                        ++events;
                        cursor = event.tick + Score::ticks(event.duration).value_or(0);
                    }
                }
            xml.writeEndElement(); // measure
        }
        xml.writeEndElement(); // part
    }
    xml.writeEndElement(); // score-partwise
    xml.writeEndDocument();
    if (xml.hasError() || !out.commit())
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), tr("Cannot write %1").arg(file)}};
    return {{QStringLiteral("ok"), true}, {QStringLiteral("parts"), written}, {QStringLiteral("events"), events}};
}

} // namespace QindaTK
