// SPDX-License-Identifier: LGPL-3.0-or-later
// MusicXML 4.0 partwise import (WP-067, D-181, docs/notation.md#interchange).
// Reads the declared feature set; every other element is reported by name,
// in `unsupported` (content not kept) or `recomputed` (engraving hints and
// metadata the layout and export regenerate), never dropped silently.
#include "score_model.h"
#include "score_musicxml_read_p.h"

#include <QFile>
#include <QXmlStreamReader>
#include <algorithm>

namespace QindaTK {

namespace {

using namespace ScoreXml;

void readPartList(QXmlStreamReader &xml, Import &import)
{
    xmlChildren(xml, [&](const QString &name) {
        if (name != QLatin1String("score-part")) return unsupported(import, xml, name);
        const QString xmlPartId = xml.attributes().value(QStringLiteral("id")).toString();
        ScorePart part;
        part.id = fromXmlId(xmlPartId);
        xmlChildren(xml, [&](const QString &child) {
            if (child == QLatin1String("part-name")) part.name = xml.readElementText();
            else if (child == QLatin1String("part-abbreviation")) part.abbreviation = xml.readElementText();
            else if (child == QLatin1String("midi-instrument"))
                xmlChildren(xml, [&](const QString &field) {
                    if (field == QLatin1String("midi-channel")) part.channel = std::clamp(xml.readElementText().toInt() - 1, 0, 15);
                    else unsupported(import, xml, field); // midi-program, volume, pan
                });
            else if (child.endsWith(QLatin1String("-display"))) recomputed(import, xml, child);
            else unsupported(import, xml, child); // score-instrument, group, ...
        });
        import.partIndex.insert(xmlPartId, int(import.content.parts.size()));
        import.content.parts.push_back(part);
    });
}

void readAttributes(QXmlStreamReader &xml, Import &import, PartState &state, int measure, bool first)
{
    ScoreMeasure &record = import.content.measures[measure];
    xmlChildren(xml, [&](const QString &name) {
        if (name == QLatin1String("divisions")) {
            state.divisions = std::max<qint64>(1, xml.readElementText().toLongLong());
        } else if (name == QLatin1String("key")) {
            xmlChildren(xml, [&](const QString &field) {
                if (field == QLatin1String("fifths")) {
                    const int fifths = xml.readElementText().toInt();
                    if (first) record.fifths = fifths;
                } else if (field == QLatin1String("mode")) {
                    recomputed(import, xml, QStringLiteral("key/mode"));
                } else {
                    unsupported(import, xml, QStringLiteral("key/") + field);
                }
            });
        } else if (name == QLatin1String("time")) {
            xmlChildren(xml, [&](const QString &field) {
                const int value = xml.readElementText(QXmlStreamReader::SkipChildElements).toInt();
                if (!first) return;
                if (field == QLatin1String("beats")) record.numerator = value;
                else if (field == QLatin1String("beat-type")) record.denominator = value;
            });
        } else if (name == QLatin1String("staves")) {
            state.staves = std::clamp(xml.readElementText().toInt(), 1, Score::kMaxStaves);
            state.clefs.resize(state.staves, ScoreClef::Treble);
        } else if (name == QLatin1String("clef")) {
            const int number = std::max(1, xml.attributes().value(QStringLiteral("number")).toInt());
            QString sign, line;
            xmlChildren(xml, [&](const QString &field) {
                if (field == QLatin1String("sign")) sign = xml.readElementText();
                else if (field == QLatin1String("line")) line = xml.readElementText();
                else unsupported(import, xml, QStringLiteral("clef/") + field); // octave change
            });
            const ScoreClef clef = sign == QLatin1String("F") ? ScoreClef::Bass
                                   : sign == QLatin1String("C") ? ScoreClef::Alto : ScoreClef::Treble;
            if ((sign == QLatin1String("G") && line != QLatin1String("2")) || (sign == QLatin1String("F") && line != QLatin1String("4"))
                || (sign == QLatin1String("C") && line != QLatin1String("3"))
                || (sign != QLatin1String("G") && sign != QLatin1String("F") && sign != QLatin1String("C")))
                import.unsupported.insert(QStringLiteral("clef %1%2").arg(sign, line));
            if (measure > 0) import.unsupported.insert(QStringLiteral("clef change"));
            else if (number <= Score::kMaxStaves) {
                if (state.clefs.size() < number) state.clefs.resize(number, ScoreClef::Treble);
                state.clefs[number - 1] = clef;
            }
        } else {
            unsupported(import, xml, name);
        }
    });
}

void readPart(QXmlStreamReader &xml, Import &import, bool firstPart)
{
    const QString xmlPartId = xml.attributes().value(QStringLiteral("id")).toString();
    PartState state;
    state.part = import.partIndex.value(xmlPartId, -1);
    if (state.part < 0) {
        import.error = QObject::tr("Part %1 is not in the part list").arg(xmlPartId);
        xml.skipCurrentElement();
        return;
    }
    int measure = -1;
    const auto instruction = [&](const QString &target, const QString &data) {
        const QStringList fields = data.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (target == QLatin1String("qindatk-staff") && fields.size() == 2)
            state.staffIds.insert(fields[0].toInt() - 1, QUuid::fromString(fields[1]));
        else if (target == QLatin1String("qindatk-voice") && fields.size() == 3)
            state.voiceIds.insert({fields[0].toInt() - 1, fields[1].toInt()}, QUuid::fromString(fields[2]));
        else if (target == QLatin1String("qindatk-event") && fields.size() == 1)
            state.pendingEvent = QUuid::fromString(fields[0]);
    };
    xmlChildren(xml, [&](const QString &name) {
        if (name != QLatin1String("measure")) return unsupported(import, xml, name);
        ++measure;
        if (firstPart) {
            ScoreMeasure record = measure > 0 ? import.content.measures.back() : ScoreMeasure();
            record.id = fromXmlId(xml.attributes().value(QStringLiteral("id")).toString());
            record.breakAfter = ScoreBreak::None;
            record.pickup = 0;
            import.content.measures.push_back(record);
        } else if (measure >= import.content.measures.size()) {
            import.error = QObject::tr("The parts have different numbers of measures");
            xml.skipCurrentElement();
            return;
        }
        const bool implicit = xml.attributes().value(QStringLiteral("implicit")) == QLatin1String("yes");
        const qint64 start = Score::measureStarts(import.content)[measure];
        qint64 cursor = start, reached = start;
        xmlChildren(xml, [&](const QString &element) {
            if (element == QLatin1String("attributes")) {
                readAttributes(xml, import, state, measure, firstPart);
            } else if (element == QLatin1String("note")) {
                addNote(import, state, readNote(xml, import), cursor);
                reached = std::max(reached, cursor);
            } else if (element == QLatin1String("backup") || element == QLatin1String("forward")) {
                const bool back = element == QLatin1String("backup");
                xmlChildren(xml, [&](const QString &field) {
                    const QString text = xml.readElementText();
                    if (field != QLatin1String("duration")) return;
                    const qint64 divisions = text.toLongLong();
                    if ((divisions * Score::kPpq) % state.divisions) ++import.rounded;
                    cursor += (back ? -1 : 1) * divisions * Score::kPpq / state.divisions;
                });
                reached = std::max(reached, cursor);
            } else if (element == QLatin1String("direction")) {
                readDirection(xml, import, state);
            } else if (element == QLatin1String("print")) {
                if (firstPart && measure > 0) {
                    ScoreMeasure &previous = import.content.measures[measure - 1];
                    if (xml.attributes().value(QStringLiteral("new-page")) == QLatin1String("yes")) previous.breakAfter = ScoreBreak::Page;
                    else if (xml.attributes().value(QStringLiteral("new-system")) == QLatin1String("yes"))
                        previous.breakAfter = ScoreBreak::System;
                }
                recomputed(import, xml, QStringLiteral("print")); // page and system layout
            } else if (element == QLatin1String("barline")) {
                xmlChildren(xml, [&](const QString &part) {
                    if (part == QLatin1String("bar-style")) recomputed(import, xml, QStringLiteral("barline/bar-style"));
                    else unsupported(import, xml, part); // repeats, endings
                });
            } else if (element == QLatin1String("sound")) {
                unsupported(import, xml, xml.attributes().hasAttribute(QStringLiteral("tempo")) ? QStringLiteral("tempo")
                                                                                                 : QStringLiteral("sound"));
            } else {
                unsupported(import, xml, element);
            }
        }, instruction);
        // A pickup (implicit) measure is as long as its content (D-181).
        if (implicit && firstPart) {
            ScoreMeasure &record = import.content.measures[measure];
            const qint64 length = reached - start;
            if (length > 0 && length < Score::meterTicks(record)) {
                record.pickup = length;
                ++import.pickups;
            }
        }
    }, instruction);
    ScorePart &part = import.content.parts[state.part];
    part.staves.clear();
    for (int s = 0; s < std::max(state.staves, int(state.clefs.size())); ++s) {
        ScoreStaff staff;
        staff.id = state.staffIds.value(s, QUuid::createUuid());
        staff.clef = state.clefs.value(s, ScoreClef::Treble);
        for (int number = 1; number <= Score::kMaxVoices; ++number) {
            if (number > 1 && !state.events.contains({s, number}) && !state.voiceIds.contains({s, number})) continue;
            ScoreVoice voice;
            voice.id = state.voiceIds.value({s, number}, QUuid::createUuid());
            voice.number = number;
            voice.events = state.events.value({s, number});
            std::stable_sort(voice.events.begin(), voice.events.end(),
                             [](const ScoreEvent &a, const ScoreEvent &b) { return a.tick < b.tick; });
            staff.voices.push_back(voice);
        }
        part.staves.push_back(staff);
    }
}

} // namespace

QVariantMap ScoreModel::importMusicXml(const QString &file)
{
    QFile in(file);
    if (!in.open(QIODevice::ReadOnly)) return failure(tr("Cannot read %1").arg(file));
    QXmlStreamReader xml(&in);
    Import import;
    import.content.id = QUuid::createUuid();
    while (!xml.atEnd() && !xml.isStartElement()) xml.readNext();
    if (xml.name() != QLatin1String("score-partwise"))
        return failure(tr("Only partwise MusicXML is supported (found %1)").arg(xml.name().toString()));
    bool firstPart = true;
    xmlChildren(xml, [&](const QString &name) {
        if (name == QLatin1String("work")) {
            xmlChildren(xml, [&](const QString &field) {
                if (field == QLatin1String("work-title")) import.content.title = xml.readElementText();
                else unsupported(import, xml, QStringLiteral("work/") + field);
            });
        } else if (name == QLatin1String("movement-title")) {
            const QString title = xml.readElementText();
            if (import.content.title.isEmpty()) import.content.title = title;
        } else if (name == QLatin1String("identification")) {
            xmlChildren(xml, [&](const QString &field) {
                if (field == QLatin1String("creator") && xml.attributes().value(QStringLiteral("type")) == QLatin1String("composer"))
                    import.content.composer = xml.readElementText();
                else if (field == QLatin1String("encoding")) recomputed(import, xml, QStringLiteral("encoding"));
                else unsupported(import, xml, QStringLiteral("identification/") + field); // rights, other creators
            });
        } else if (name == QLatin1String("part-list")) {
            readPartList(xml, import);
        } else if (name == QLatin1String("part")) {
            readPart(xml, import, firstPart);
            firstPart = false;
        } else if (name == QLatin1String("defaults") || name == QLatin1String("credit")) {
            recomputed(import, xml, name); // page appearance: the layout options own it
        } else {
            unsupported(import, xml, name);
        }
    }, [&](const QString &target, const QString &data) {
        if (target == QLatin1String("qindatk-score")) import.content.id = fromXmlId(QStringLiteral("S-") + data);
    });
    if (xml.hasError()) return failure(tr("The MusicXML file is not well formed: %1").arg(xml.errorString()));
    if (!import.error.isEmpty()) return failure(import.error);
    if (import.content.measures.isEmpty() || import.content.parts.isEmpty()) return failure(tr("The file holds no music"));
    import.content.measures.back().breakAfter = ScoreBreak::None;
    int before = 0, after = 0;
    for (const ScorePart &part : import.content.parts)
        for (const ScoreStaff &staff : part.staves)
            for (const ScoreVoice &voice : staff.voices) before += int(voice.events.size());
    if (const QString problem = Score::completeVoices(import.content); !problem.isEmpty()) return failure(problem);
    Score::dropBrokenTies(import.content);
    for (const ScorePart &part : import.content.parts)
        for (const ScoreStaff &staff : part.staves)
            for (const ScoreVoice &voice : staff.voices) after += int(voice.events.size());
    QString error;
    if (!commitContent(import.content, tr("Import MusicXML"), &error)) return failure(error);
    const QStringList names = sortedList(import.unsupported);
    return ok({{QStringLiteral("parts"), int(import.content.parts.size())},
               {QStringLiteral("measures"), int(import.content.measures.size())},
               {QStringLiteral("events"), after},
               {QStringLiteral("filledRests"), after - before},
               {QStringLiteral("durationMismatches"), import.mismatches},
               {QStringLiteral("roundedPositions"), import.rounded},
               {QStringLiteral("pickups"), import.pickups},
               {QStringLiteral("unsupported"), names},
               {QStringLiteral("recomputed"), sortedList(import.recomputed)}});
}

} // namespace QindaTK
