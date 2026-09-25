// SPDX-License-Identifier: LGPL-3.0-or-later
// ScoreModel structure edits (WP-067, docs/notation.md#editing): parts,
// staves, voices, measures, meters, keys and breaks. Each is one undo step.
#include "score_model.h"

#include <algorithm>
#include <functional>

namespace QindaTK {

namespace {

void shiftEvents(ScoreContent &content, qint64 from, qint64 delta)
{
    for (ScorePart &part : content.parts)
        for (ScoreStaff &staff : part.staves)
            for (ScoreVoice &voice : staff.voices)
                for (ScoreEvent &event : voice.events)
                    if (event.tick >= from) event.tick += delta;
}

void removeEventsIn(ScoreContent &content, qint64 from, qint64 to)
{
    for (ScorePart &part : content.parts)
        for (ScoreStaff &staff : part.staves)
            for (ScoreVoice &voice : staff.voices)
                voice.events.erase(std::remove_if(voice.events.begin(), voice.events.end(),
                                                  [from, to](const ScoreEvent &event) {
                                                      return event.tick >= from && event.tick < to;
                                                  }),
                                   voice.events.end());
}

bool onlyRests(const ScoreContent &content, qint64 from, qint64 to)
{
    for (const ScorePart &part : content.parts)
        for (const ScoreStaff &staff : part.staves)
            for (const ScoreVoice &voice : staff.voices)
                for (const ScoreEvent &event : voice.events)
                    if (event.tick >= from && event.tick < to && !event.rest) return false;
    return true;
}

// The measure and the following ones that share its current value.
std::pair<int, int> runFrom(const ScoreContent &content, int index,
                            const std::function<bool(const ScoreMeasure &, const ScoreMeasure &)> &same)
{
    int last = index;
    while (last + 1 < content.measures.size() && same(content.measures[last + 1], content.measures[index])) ++last;
    return {index, last};
}

} // namespace

QVariantMap ScoreModel::newScore(const QString &title, int measures, const QString &time, int fifths)
{
    const QStringList parts = time.split(QLatin1Char('/'));
    ScoreContent content = Score::newScore(title, std::clamp(measures, 1, Score::kMaxMeasures),
                                           parts.value(0).toInt(), parts.value(1).toInt(), fifths);
    QString error;
    if (parts.size() != 2 || !resetContent(content, &error))
        return failure(error.isEmpty() ? tr("Unsupported time signature %1").arg(time) : error);
    const ScorePart &part = m_content.parts.front();
    return ok({{QStringLiteral("scoreId"), scoreId()},
               {QStringLiteral("partId"), Score::idText(part.id)},
               {QStringLiteral("staffId"), Score::idText(part.staves.front().id)},
               {QStringLiteral("voiceId"), Score::idText(part.staves.front().voices.front().id)}});
}

QVariantMap ScoreModel::addPart(const QString &name, const QStringList &clefs, int channel)
{
    QList<ScoreClef> parsed;
    for (const QString &clef : clefs) {
        const auto value = Score::parseClef(clef);
        if (!value) return failure(tr("Unknown clef %1").arg(clef));
        parsed.push_back(*value);
    }
    if (parsed.isEmpty() || parsed.size() > Score::kMaxStaves)
        return failure(tr("A part has 1 to %1 staves").arg(Score::kMaxStaves));
    const ScorePart part = Score::newPart(name, parsed, channel >= 0 ? channel : int(m_content.parts.size() % 16));
    QStringList staffIds;
    for (const ScoreStaff &staff : part.staves) staffIds.push_back(Score::idText(staff.id));
    return editContent(tr("Add part"), [&part](ScoreContent &c) {
        c.parts.push_back(part);
        return Score::completeVoices(c);
    }, {{QStringLiteral("partId"), Score::idText(part.id)}, {QStringLiteral("staffIds"), staffIds}});
}

QVariantMap ScoreModel::removePart(const QString &partId)
{
    const QUuid id = QUuid::fromString(partId);
    return editContent(tr("Remove part"), [&id](ScoreContent &c) {
        const auto it = std::find_if(c.parts.begin(), c.parts.end(), [&id](const ScorePart &p) { return p.id == id; });
        if (it == c.parts.end()) return tr("There is no part %1").arg(Score::idText(id));
        if (c.parts.size() == 1) return tr("A score keeps at least one part");
        c.parts.erase(it);
        return QString();
    });
}

QVariantMap ScoreModel::setPart(const QString &partId, const QVariantMap &fields)
{
    const QUuid id = QUuid::fromString(partId);
    return editContent(tr("Edit part"), [&](ScoreContent &c) {
        for (ScorePart &part : c.parts) {
            if (part.id != id) continue;
            if (fields.contains(QStringLiteral("name"))) part.name = fields.value(QStringLiteral("name")).toString();
            if (fields.contains(QStringLiteral("abbreviation")))
                part.abbreviation = fields.value(QStringLiteral("abbreviation")).toString();
            if (fields.contains(QStringLiteral("channel"))) part.channel = fields.value(QStringLiteral("channel")).toInt();
            return QString();
        }
        return tr("There is no part %1").arg(partId);
    });
}

QVariantMap ScoreModel::setClef(const QString &staffId, const QString &clef)
{
    const auto value = Score::parseClef(clef);
    if (!value) return failure(tr("Unknown clef %1").arg(clef));
    int p = -1, s = -1;
    if (!Score::findStaff(m_content, QUuid::fromString(staffId), &p, &s)) return failure(tr("There is no staff %1").arg(staffId));
    return editContent(tr("Change clef"), [&](ScoreContent &c) {
        c.parts[p].staves[s].clef = *value;
        return QString();
    });
}

QVariantMap ScoreModel::addVoice(const QString &staffId)
{
    int p = -1, s = -1;
    const ScoreStaff *staff = Score::findStaff(m_content, QUuid::fromString(staffId), &p, &s);
    if (!staff) return failure(tr("There is no staff %1").arg(staffId));
    int number = 1;
    for (const ScoreVoice &voice : staff->voices)
        if (voice.number == number) ++number;
    if (number > Score::kMaxVoices) return failure(tr("A staff has at most %1 voices").arg(Score::kMaxVoices));
    ScoreVoice voice;
    voice.id = QUuid::createUuid();
    voice.number = number;
    return editContent(tr("Add voice"), [&](ScoreContent &c) {
        auto &voices = c.parts[p].staves[s].voices;
        voices.push_back(voice);
        std::sort(voices.begin(), voices.end(), [](const ScoreVoice &a, const ScoreVoice &b) { return a.number < b.number; });
        return QString();
    }, {{QStringLiteral("voiceId"), Score::idText(voice.id)}, {QStringLiteral("number"), number}});
}

QVariantMap ScoreModel::appendMeasures(int count)
{
    if (count < 1 || m_content.measures.size() + count > Score::kMaxMeasures)
        return failure(tr("A score has at most %1 measures").arg(Score::kMaxMeasures));
    QVector<QUuid> created;
    QStringList ids;
    for (int i = 0; i < count; ++i) {
        created.push_back(QUuid::createUuid());
        ids.push_back(Score::idText(created.back()));
    }
    return editContent(tr("Add measures"), [&](ScoreContent &c) {
        for (const QUuid &id : created) {
            ScoreMeasure measure = c.measures.back();
            measure.id = id;
            measure.breakAfter = ScoreBreak::None;
            measure.pickup = 0;
            c.measures.push_back(measure);
        }
        return Score::completeVoices(c);
    }, {{QStringLiteral("measureIds"), ids}});
}

QVariantMap ScoreModel::insertMeasures(const QString &beforeMeasureId, int count)
{
    const int index = Score::measureIndex(m_content, QUuid::fromString(beforeMeasureId));
    if (index < 0) return failure(tr("There is no measure %1").arg(beforeMeasureId));
    if (count < 1 || m_content.measures.size() + count > Score::kMaxMeasures)
        return failure(tr("A score has at most %1 measures").arg(Score::kMaxMeasures));
    return editContent(tr("Insert measures"), [&](ScoreContent &c) {
        const qint64 at = Score::measureStarts(c)[index];
        ScoreMeasure measure = c.measures[index];
        measure.breakAfter = ScoreBreak::None;
        measure.pickup = 0;
        shiftEvents(c, at, Score::measureTicks(measure) * count);
        for (int i = 0; i < count; ++i) {
            measure.id = QUuid::createUuid();
            c.measures.insert(index, measure);
        }
        return Score::completeVoices(c);
    });
}

QVariantMap ScoreModel::removeMeasures(const QStringList &measureIds)
{
    QVector<int> indexes;
    for (const QString &id : measureIds) {
        const int index = Score::measureIndex(m_content, QUuid::fromString(id));
        if (index < 0) return failure(tr("There is no measure %1").arg(id));
        if (!indexes.contains(index)) indexes.push_back(index);
    }
    if (indexes.isEmpty() || indexes.size() >= m_content.measures.size())
        return failure(tr("A score keeps at least one measure"));
    std::sort(indexes.begin(), indexes.end(), std::greater<int>());
    return editContent(tr("Remove measures"), [&](ScoreContent &c) {
        for (int index : indexes) {
            const QVector<qint64> starts = Score::measureStarts(c);
            removeEventsIn(c, starts[index], starts[index + 1]);
            shiftEvents(c, starts[index + 1], starts[index] - starts[index + 1]);
            c.measures.removeAt(index);
        }
        c.measures.back().breakAfter = ScoreBreak::None;
        Score::dropBrokenTies(c);
        return QString();
    });
}

QVariantMap ScoreModel::setTimeSignature(const QString &measureId, int numerator, int denominator)
{
    const int index = Score::measureIndex(m_content, QUuid::fromString(measureId));
    if (index < 0) return failure(tr("There is no measure %1").arg(measureId));
    const auto [first, last] = runFrom(m_content, index, [](const ScoreMeasure &a, const ScoreMeasure &b) {
        return a.numerator == b.numerator && a.denominator == b.denominator;
    });
    return editContent(tr("Change time signature"), [&, first = first, last = last](ScoreContent &c) {
        const QVector<qint64> starts = Score::measureStarts(c);
        if (!onlyRests(c, starts[first], starts[last + 1]))
            return tr("Measures %1 to %2 hold notes; the meter changes only on measures of rests").arg(first + 1).arg(last + 1);
        removeEventsIn(c, starts[first], starts[last + 1]);
        ScoreMeasure changed = c.measures[first];
        changed.numerator = numerator;
        changed.denominator = denominator;
        changed.pickup = 0;
        const qint64 delta = Score::measureTicks(changed) * (last - first + 1) - (starts[last + 1] - starts[first]);
        shiftEvents(c, starts[last + 1], delta);
        for (int m = first; m <= last; ++m) {
            c.measures[m].pickup = 0;
            c.measures[m].numerator = numerator;
            c.measures[m].denominator = denominator;
        }
        Score::dropBrokenTies(c);
        return Score::completeVoices(c);
    });
}

QVariantMap ScoreModel::setPickup(const QString &measureId, const QString &ticks)
{
    const int index = Score::measureIndex(m_content, QUuid::fromString(measureId));
    bool parsed = false;
    const qint64 length = ticks.toLongLong(&parsed);
    if (index < 0 || !parsed) return failure(tr("Unknown measure or length %1").arg(ticks));
    return editContent(length > 0 ? tr("Set pickup") : tr("Remove pickup"), [&](ScoreContent &c) {
        const QVector<qint64> starts = Score::measureStarts(c);
        if (!onlyRests(c, starts[index], starts[index + 1]))
            return tr("Measure %1 holds notes; a pickup changes only a measure of rests").arg(index + 1);
        removeEventsIn(c, starts[index], starts[index + 1]);
        ScoreMeasure &measure = c.measures[index];
        const qint64 before = Score::measureTicks(measure);
        measure.pickup = length >= Score::meterTicks(measure) ? 0 : std::max<qint64>(0, length);
        shiftEvents(c, starts[index + 1], Score::measureTicks(measure) - before);
        Score::dropBrokenTies(c);
        return Score::completeVoices(c);
    });
}

QVariantMap ScoreModel::setKeySignature(const QString &measureId, int fifths)
{
    const int index = Score::measureIndex(m_content, QUuid::fromString(measureId));
    if (index < 0) return failure(tr("There is no measure %1").arg(measureId));
    const auto [first, last] = runFrom(m_content, index, [](const ScoreMeasure &a, const ScoreMeasure &b) {
        return a.fifths == b.fifths;
    });
    return editContent(tr("Change key signature"), [&, first = first, last = last](ScoreContent &c) {
        for (int m = first; m <= last; ++m) c.measures[m].fifths = fifths;
        return QString();
    });
}

QVariantMap ScoreModel::setBreak(const QString &measureId, const QString &kind)
{
    const int index = Score::measureIndex(m_content, QUuid::fromString(measureId));
    const auto value = Score::parseBreak(kind);
    if (index < 0 || !value) return failure(tr("Unknown measure or break %1").arg(kind));
    return editContent(tr("Set break"), [&](ScoreContent &c) {
        c.measures[index].breakAfter = *value;
        return QString();
    });
}

} // namespace QindaTK
