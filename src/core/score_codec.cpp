// SPDX-License-Identifier: LGPL-3.0-or-later
// Score validation, construction and the native JSON codec
// (`qindatk.score/1`, WP-067, docs/notation.md#native-file).
#include "score_types.h"

#include <QJsonArray>
#include <QObject>
#include <QSet>
#include <algorithm>

namespace QindaTK::Score {

namespace {

bool validDenominator(int denominator)
{
    return denominator == 1 || denominator == 2 || denominator == 4 || denominator == 8 || denominator == 16
           || denominator == 32;
}

QString validateVoice(const ScoreVoice &voice, const QVector<qint64> &starts, QSet<QUuid> &ids)
{
    const qint64 total = starts.back();
    for (int e = 0; e < voice.events.size(); ++e) {
        const ScoreEvent &event = voice.events[e];
        const QString where = QObject::tr("event %1").arg(idText(event.id));
        if (event.id.isNull() || ids.contains(event.id)) return QObject::tr("Duplicate or missing ID at %1").arg(where);
        ids.insert(event.id);
        if (normalized(event.duration) != event.duration) return QObject::tr("The duration of %1 is not canonical").arg(where);
        const auto length = ticks(event.duration);
        if (!length) return QObject::tr("The duration of %1 is not a whole number of ticks").arg(where);
        const int measure = measureIndexAt(starts, event.tick);
        if (measure < 0 || event.tick + *length > starts[measure + 1] || event.tick + *length > total)
            return QObject::tr("%1 crosses a bar line or the end of the score").arg(where);
        if (e > 0) {
            const ScoreEvent &previous = voice.events[e - 1];
            if (previous.tick + ticks(previous.duration).value_or(0) > event.tick)
                return QObject::tr("%1 overlaps the event before it").arg(where);
        }
        if (event.rest != event.notes.isEmpty()) return QObject::tr("%1 must be a rest or hold notes").arg(where);
        if (event.notes.size() > kMaxChord) return QObject::tr("%1 has too many notes").arg(where);
        if (canonicalArticulations(event.articulations) != event.articulations)
            return QObject::tr("%1 has unknown or unordered articulations").arg(where);
        if (!event.dynamic.isEmpty() && !dynamicNames().contains(event.dynamic))
            return QObject::tr("%1 has an unknown dynamic").arg(where);
        for (const ScoreNote &note : event.notes) {
            if (note.id.isNull() || ids.contains(note.id)) return QObject::tr("Duplicate or missing note ID in %1").arg(where);
            ids.insert(note.id);
            const int midi = midiPitch(note.pitch);
            if (note.pitch.step < 0 || note.pitch.step > 6 || note.pitch.alter < -2 || note.pitch.alter > 2
                || midi < 0 || midi > 127)
                return QObject::tr("A note of %1 has an invalid pitch").arg(where);
            if (note.tie) {
                const bool hasNext = e + 1 < voice.events.size()
                                     && voice.events[e + 1].tick == event.tick + *length;
                if (!hasNext || tieTarget(note, voice.events[e + 1]) < 0)
                    return QObject::tr("The tie from %1 in %2 has no note to end on").arg(pitchName(note.pitch), where);
            }
        }
    }
    QString problem;
    const QVector<ScoreTupletGroup> groups = tupletGroups(voice.events, &problem);
    if (!problem.isEmpty()) return problem;
    for (const ScoreTupletGroup &group : groups)
        if (measureIndexAt(starts, voice.events[group.first].tick) != measureIndexAt(starts, voice.events[group.last].tick))
            return QObject::tr("A tuplet crosses a bar line");
    if (voice.number == 1)
        for (int m = 0; m + 1 < starts.size(); ++m) {
            qint64 filled = 0;
            for (const ScoreEvent &event : voice.events)
                if (event.tick >= starts[m] && event.tick < starts[m + 1]) filled += *ticks(event.duration);
            if (filled != starts[m + 1] - starts[m])
                return QObject::tr("Voice 1 of measure %1 is not complete").arg(m + 1);
        }
    return {};
}

QJsonArray stringArray(const QStringList &list)
{
    QJsonArray array;
    for (const QString &item : list) array.append(item);
    return array;
}

} // namespace

QString validate(const ScoreContent &content)
{
    if (content.id.isNull()) return QObject::tr("The score has no ID");
    if (content.measures.isEmpty() || content.measures.size() > kMaxMeasures)
        return QObject::tr("A score has 1 to %1 measures").arg(kMaxMeasures);
    if (content.parts.isEmpty() || content.parts.size() > kMaxParts)
        return QObject::tr("A score has 1 to %1 parts").arg(kMaxParts);
    QSet<QUuid> ids{content.id};
    for (int m = 0; m < content.measures.size(); ++m) {
        const ScoreMeasure &measure = content.measures[m];
        if (measure.id.isNull() || ids.contains(measure.id)) return QObject::tr("Measure %1 has no unique ID").arg(m + 1);
        ids.insert(measure.id);
        if (measure.numerator < 1 || measure.numerator > 32 || !validDenominator(measure.denominator))
            return QObject::tr("Measure %1 has an unsupported time signature").arg(m + 1);
        if (measure.fifths < -7 || measure.fifths > 7) return QObject::tr("Measure %1 has an invalid key").arg(m + 1);
        if (measure.pickup < 0 || measure.pickup >= meterTicks(measure))
            return QObject::tr("The pickup of measure %1 must be shorter than its meter").arg(m + 1);
    }
    if (content.measures.back().breakAfter != ScoreBreak::None)
        return QObject::tr("The last measure cannot end a system or page");
    const QVector<qint64> starts = measureStarts(content);
    for (const ScorePart &part : content.parts) {
        if (part.id.isNull() || ids.contains(part.id)) return QObject::tr("A part has no unique ID");
        ids.insert(part.id);
        if (part.channel < 0 || part.channel > 15) return QObject::tr("Part %1 has an invalid MIDI channel").arg(part.name);
        if (part.staves.isEmpty() || part.staves.size() > kMaxStaves)
            return QObject::tr("Part %1 needs 1 to %2 staves").arg(part.name).arg(kMaxStaves);
        for (const ScoreStaff &staff : part.staves) {
            if (staff.id.isNull() || ids.contains(staff.id)) return QObject::tr("A staff has no unique ID");
            ids.insert(staff.id);
            if (staff.voices.isEmpty() || staff.voices.size() > kMaxVoices || staff.voices.front().number != 1)
                return QObject::tr("A staff of %1 needs voice 1 and at most %2 voices").arg(part.name).arg(kMaxVoices);
            for (int v = 0; v < staff.voices.size(); ++v) {
                const ScoreVoice &voice = staff.voices[v];
                if (voice.id.isNull() || ids.contains(voice.id)) return QObject::tr("A voice has no unique ID");
                ids.insert(voice.id);
                if (voice.number < 1 || voice.number > kMaxVoices || (v > 0 && staff.voices[v - 1].number >= voice.number))
                    return QObject::tr("Voices of a staff are numbered 1 to %1 in order").arg(kMaxVoices);
                if (const QString problem = validateVoice(voice, starts, ids); !problem.isEmpty()) return problem;
            }
        }
    }
    return {};
}

ScorePart newPart(const QString &name, const QList<ScoreClef> &clefs, int channel)
{
    ScorePart part;
    part.id = QUuid::createUuid();
    part.name = name;
    part.abbreviation = name.left(3);
    part.channel = channel;
    for (ScoreClef clef : clefs) {
        ScoreStaff staff;
        staff.id = QUuid::createUuid();
        staff.clef = clef;
        ScoreVoice voice;
        voice.id = QUuid::createUuid();
        voice.number = 1;
        staff.voices.push_back(voice);
        part.staves.push_back(staff);
    }
    return part;
}

ScoreContent newScore(const QString &title, int measures, int numerator, int denominator, int fifths)
{
    ScoreContent content;
    content.id = QUuid::createUuid();
    content.title = title;
    for (int m = 0; m < std::max(1, measures); ++m) {
        ScoreMeasure measure;
        measure.id = QUuid::createUuid();
        measure.numerator = numerator;
        measure.denominator = denominator;
        measure.fifths = fifths;
        content.measures.push_back(measure);
    }
    content.parts.push_back(newPart(QObject::tr("Part"), {ScoreClef::Treble}, 0));
    completeVoices(content);
    return content;
}

QVariantMap eventMap(const ScoreEvent &event)
{
    QVariantList notes;
    for (const ScoreNote &note : event.notes)
        notes.push_back(QVariantMap{{QStringLiteral("id"), idText(note.id)},
                                    {QStringLiteral("pitch"), pitchName(note.pitch)},
                                    {QStringLiteral("midi"), midiPitch(note.pitch)},
                                    {QStringLiteral("tie"), note.tie}});
    return {{QStringLiteral("id"), idText(event.id)},
            {QStringLiteral("tick"), QString::number(event.tick)},
            {QStringLiteral("ticks"), QString::number(ticks(event.duration).value_or(0))},
            {QStringLiteral("durationId"), durationId(event.duration)},
            {QStringLiteral("rest"), event.rest},
            {QStringLiteral("notes"), notes},
            {QStringLiteral("articulations"), event.articulations},
            {QStringLiteral("dynamic"), event.dynamic}};
}

QJsonObject toJson(const ScoreContent &content)
{
    QJsonArray measures;
    for (const ScoreMeasure &measure : content.measures)
        measures.append(QJsonObject{{QStringLiteral("id"), idText(measure.id)},
                                    {QStringLiteral("time"), QStringLiteral("%1/%2").arg(measure.numerator).arg(measure.denominator)},
                                    {QStringLiteral("fifths"), measure.fifths},
                                    {QStringLiteral("break"), breakName(measure.breakAfter)},
                                    {QStringLiteral("pickup"), QString::number(measure.pickup)}});
    QJsonArray parts;
    for (const ScorePart &part : content.parts) {
        QJsonArray staves;
        for (const ScoreStaff &staff : part.staves) {
            QJsonArray voices;
            for (const ScoreVoice &voice : staff.voices) {
                QJsonArray events;
                for (const ScoreEvent &event : voice.events) {
                    QJsonArray notes;
                    for (const ScoreNote &note : event.notes)
                        notes.append(QJsonObject{{QStringLiteral("id"), idText(note.id)},
                                                 {QStringLiteral("pitch"), pitchName(note.pitch)},
                                                 {QStringLiteral("tie"), note.tie}});
                    events.append(QJsonObject{{QStringLiteral("id"), idText(event.id)},
                                              {QStringLiteral("tick"), QString::number(event.tick)},
                                              {QStringLiteral("duration"), durationId(event.duration)},
                                              {QStringLiteral("rest"), event.rest},
                                              {QStringLiteral("notes"), notes},
                                              {QStringLiteral("articulations"), stringArray(event.articulations)},
                                              {QStringLiteral("dynamic"), event.dynamic}});
                }
                voices.append(QJsonObject{{QStringLiteral("id"), idText(voice.id)},
                                          {QStringLiteral("number"), voice.number},
                                          {QStringLiteral("events"), events}});
            }
            staves.append(QJsonObject{{QStringLiteral("id"), idText(staff.id)},
                                      {QStringLiteral("clef"), clefName(staff.clef)},
                                      {QStringLiteral("voices"), voices}});
        }
        parts.append(QJsonObject{{QStringLiteral("id"), idText(part.id)},
                                 {QStringLiteral("name"), part.name},
                                 {QStringLiteral("abbreviation"), part.abbreviation},
                                 {QStringLiteral("channel"), part.channel},
                                 {QStringLiteral("staves"), staves}});
    }
    return {{QStringLiteral("schema"), QStringLiteral("qindatk.score/1")},
            {QStringLiteral("ppq"), kPpq},
            {QStringLiteral("id"), idText(content.id)},
            {QStringLiteral("title"), content.title},
            {QStringLiteral("composer"), content.composer},
            {QStringLiteral("measures"), measures},
            {QStringLiteral("parts"), parts}};
}

std::optional<ScoreContent> fromJson(const QJsonObject &object, QString *error)
{
    const auto fail = [error](const QString &message) -> std::optional<ScoreContent> {
        if (error) *error = message;
        return std::nullopt;
    };
    if (object.value(QStringLiteral("schema")).toString() != QLatin1String("qindatk.score/1"))
        return fail(QObject::tr("Not a qindatk.score/1 document"));
    if (object.value(QStringLiteral("ppq")).toInt() != kPpq)
        return fail(QObject::tr("The score resolution must be %1 ticks per quarter").arg(kPpq));
    const auto uuid = [](const QJsonValue &value) { return QUuid::fromString(value.toString()); };
    ScoreContent content;
    content.id = uuid(object.value(QStringLiteral("id")));
    content.title = object.value(QStringLiteral("title")).toString();
    content.composer = object.value(QStringLiteral("composer")).toString();
    for (const QJsonValue &value : object.value(QStringLiteral("measures")).toArray()) {
        const QJsonObject m = value.toObject();
        ScoreMeasure measure;
        measure.id = uuid(m.value(QStringLiteral("id")));
        const QStringList time = m.value(QStringLiteral("time")).toString().split(QLatin1Char('/'));
        if (time.size() != 2) return fail(QObject::tr("A measure has no time signature"));
        measure.numerator = time[0].toInt();
        measure.denominator = time[1].toInt();
        measure.fifths = m.value(QStringLiteral("fifths")).toInt();
        measure.pickup = m.value(QStringLiteral("pickup")).toString().toLongLong(); // absent: 0
        const auto breakAfter = parseBreak(m.value(QStringLiteral("break")).toString());
        if (!breakAfter) return fail(QObject::tr("A measure has an unknown break"));
        measure.breakAfter = *breakAfter;
        content.measures.push_back(measure);
    }
    for (const QJsonValue &partValue : object.value(QStringLiteral("parts")).toArray()) {
        const QJsonObject p = partValue.toObject();
        ScorePart part;
        part.id = uuid(p.value(QStringLiteral("id")));
        part.name = p.value(QStringLiteral("name")).toString();
        part.abbreviation = p.value(QStringLiteral("abbreviation")).toString();
        part.channel = p.value(QStringLiteral("channel")).toInt();
        for (const QJsonValue &staffValue : p.value(QStringLiteral("staves")).toArray()) {
            const QJsonObject s = staffValue.toObject();
            ScoreStaff staff;
            staff.id = uuid(s.value(QStringLiteral("id")));
            const auto clef = parseClef(s.value(QStringLiteral("clef")).toString());
            if (!clef) return fail(QObject::tr("A staff has an unknown clef"));
            staff.clef = *clef;
            for (const QJsonValue &voiceValue : s.value(QStringLiteral("voices")).toArray()) {
                const QJsonObject v = voiceValue.toObject();
                ScoreVoice voice;
                voice.id = uuid(v.value(QStringLiteral("id")));
                voice.number = v.value(QStringLiteral("number")).toInt();
                for (const QJsonValue &eventValue : v.value(QStringLiteral("events")).toArray()) {
                    const QJsonObject e = eventValue.toObject();
                    ScoreEvent event;
                    event.id = uuid(e.value(QStringLiteral("id")));
                    bool okTick = false;
                    event.tick = e.value(QStringLiteral("tick")).toString().toLongLong(&okTick);
                    const auto duration = parseDuration(e.value(QStringLiteral("duration")).toString());
                    if (!okTick || !duration) return fail(QObject::tr("An event has an invalid tick or duration"));
                    event.duration = *duration;
                    event.rest = e.value(QStringLiteral("rest")).toBool();
                    for (const QJsonValue &noteValue : e.value(QStringLiteral("notes")).toArray()) {
                        const QJsonObject n = noteValue.toObject();
                        const auto pitch = parsePitch(n.value(QStringLiteral("pitch")).toString());
                        if (!pitch) return fail(QObject::tr("A note has an invalid pitch"));
                        event.notes.push_back({uuid(n.value(QStringLiteral("id"))), *pitch,
                                               n.value(QStringLiteral("tie")).toBool()});
                    }
                    for (const QJsonValue &name : e.value(QStringLiteral("articulations")).toArray())
                        event.articulations.push_back(name.toString());
                    event.dynamic = e.value(QStringLiteral("dynamic")).toString();
                    voice.events.push_back(event);
                }
                staff.voices.push_back(voice);
            }
            part.staves.push_back(staff);
        }
        content.parts.push_back(part);
    }
    if (const QString problem = validate(content); !problem.isEmpty()) return fail(problem);
    return content;
}

} // namespace QindaTK::Score
