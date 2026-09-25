// SPDX-License-Identifier: LGPL-3.0-or-later
// MusicXML 4.0 partwise import (WP-067, D-181, docs/notation.md#interchange).
// Reads the declared feature set; everything else is reported by element
// name in `unsupported`, never dropped silently.
#include "score_model.h"

#include <QFile>
#include <QXmlStreamReader>
#include <algorithm>

namespace QindaTK {

namespace {

QUuid fromXmlId(const QString &text)
{
    const qsizetype dash = text.indexOf(QLatin1Char('-'));
    const QUuid id = dash > 0 ? QUuid::fromString(text.mid(dash + 1)) : QUuid();
    return id.isNull() ? QUuid::createUuid() : id;
}

// Visits the children of the current element: elements through `element`
// (which must consume them) and processing instructions through `pi`.
template<typename Element, typename Instruction>
void xmlChildren(QXmlStreamReader &xml, Element element, Instruction pi)
{
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) element(xml.name().toString());
        else if (xml.isEndElement()) return;
        else if (xml.isProcessingInstruction()) pi(xml.processingInstructionTarget().toString(),
                                                   xml.processingInstructionData().toString().trimmed());
    }
}

template<typename Element>
void xmlChildren(QXmlStreamReader &xml, Element element)
{
    xmlChildren(xml, element, [](const QString &, const QString &) {});
}

struct XmlNote {
    bool chord = false, rest = false, grace = false, cue = false, hasType = false;
    ScorePitch pitch;
    ScoreDuration duration;
    qint64 divisions = 0;
    bool tieStart = false;
    int staff = 1;
    QString voice = QStringLiteral("1");
    QString id;
    QStringList articulations;
};

struct PartState {
    int part = -1;
    qint64 divisions = 1;
    int staves = 1;
    QVector<ScoreClef> clefs{ScoreClef::Treble};
    QHash<int, QUuid> staffIds;                  // 0-based staff
    QHash<QPair<int, int>, QUuid> voiceIds;      // (staff, number)
    QHash<QPair<int, QString>, int> voiceNumbers; // (staff, MusicXML voice) -> 1..4
    QHash<QPair<int, int>, QVector<ScoreEvent>> events;
    QHash<QPair<int, QString>, QString> dynamics; // pending, (staff, voice or "")
    QUuid pendingEvent;
};

struct Import {
    ScoreContent content;
    QHash<QString, int> partIndex; // MusicXML part id -> index
    QSet<QString> unsupported;
    int mismatches = 0;
    int rounded = 0;
    QString error;
};

void unsupported(Import &import, QXmlStreamReader &xml, const QString &name)
{
    import.unsupported.insert(name);
    xml.skipCurrentElement();
}

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
                    else xml.skipCurrentElement();
                });
            else xml.skipCurrentElement(); // score-instrument, part-name-display: presentation only
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
                    xml.skipCurrentElement();
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
                else xml.skipCurrentElement();
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
                    xml.skipCurrentElement(); // groups follow from the ratios (D-181)
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
            xml.skipCurrentElement(); // engraving hints: the layout recomputes them
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
        } else if (name == QLatin1String("sound") || name == QLatin1String("offset")) {
            xml.skipCurrentElement();
        } else {
            unsupported(import, xml, name);
        }
    });
    if (!dynamic.isEmpty()) state.dynamics.insert({staff - 1, voice}, dynamic);
}

int voiceNumber(PartState &state, int staff, const QString &xmlVoice)
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
            import.content.measures.push_back(record);
        } else if (measure >= import.content.measures.size()) {
            import.error = QObject::tr("The parts have different numbers of measures");
            xml.skipCurrentElement();
            return;
        }
        const qint64 start = Score::measureStarts(import.content)[measure];
        qint64 cursor = start;
        xmlChildren(xml, [&](const QString &element) {
            if (element == QLatin1String("attributes")) {
                readAttributes(xml, import, state, measure, firstPart);
            } else if (element == QLatin1String("note")) {
                addNote(import, state, readNote(xml, import), cursor);
            } else if (element == QLatin1String("backup") || element == QLatin1String("forward")) {
                const bool back = element == QLatin1String("backup");
                xmlChildren(xml, [&](const QString &field) {
                    const QString text = xml.readElementText();
                    if (field != QLatin1String("duration")) return;
                    const qint64 divisions = text.toLongLong();
                    if ((divisions * Score::kPpq) % state.divisions) ++import.rounded;
                    cursor += (back ? -1 : 1) * divisions * Score::kPpq / state.divisions;
                });
            } else if (element == QLatin1String("direction")) {
                readDirection(xml, import, state);
            } else if (element == QLatin1String("print")) {
                if (firstPart && measure > 0) {
                    ScoreMeasure &previous = import.content.measures[measure - 1];
                    if (xml.attributes().value(QStringLiteral("new-page")) == QLatin1String("yes")) previous.breakAfter = ScoreBreak::Page;
                    else if (xml.attributes().value(QStringLiteral("new-system")) == QLatin1String("yes"))
                        previous.breakAfter = ScoreBreak::System;
                }
                xml.skipCurrentElement();
            } else if (element == QLatin1String("barline")) {
                xmlChildren(xml, [&](const QString &part) {
                    if (part == QLatin1String("bar-style")) xml.skipCurrentElement();
                    else unsupported(import, xml, part); // repeats, endings
                });
            } else if (element == QLatin1String("sound")) {
                xml.skipCurrentElement(); // tempo belongs to the session tempo map
            } else {
                unsupported(import, xml, element);
            }
        }, instruction);
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
                else xml.skipCurrentElement();
            });
        } else if (name == QLatin1String("movement-title")) {
            const QString title = xml.readElementText();
            if (import.content.title.isEmpty()) import.content.title = title;
        } else if (name == QLatin1String("identification")) {
            xmlChildren(xml, [&](const QString &field) {
                if (field == QLatin1String("creator") && xml.attributes().value(QStringLiteral("type")) == QLatin1String("composer"))
                    import.content.composer = xml.readElementText();
                else xml.skipCurrentElement();
            });
        } else if (name == QLatin1String("part-list")) {
            readPartList(xml, import);
        } else if (name == QLatin1String("part")) {
            readPart(xml, import, firstPart);
            firstPart = false;
        } else if (name == QLatin1String("defaults") || name == QLatin1String("credit")) {
            xml.skipCurrentElement(); // page appearance: the layout options own it
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
    QStringList names(import.unsupported.begin(), import.unsupported.end());
    names.sort();
    return ok({{QStringLiteral("parts"), int(import.content.parts.size())},
               {QStringLiteral("measures"), int(import.content.measures.size())},
               {QStringLiteral("events"), after},
               {QStringLiteral("filledRests"), after - before},
               {QStringLiteral("durationMismatches"), import.mismatches},
               {QStringLiteral("roundedPositions"), import.rounded},
               {QStringLiteral("unsupported"), names}});
}

} // namespace QindaTK
