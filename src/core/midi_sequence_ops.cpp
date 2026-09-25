// SPDX-License-Identifier: LGPL-3.0-or-later
// MidiSequenceModel: patterns, step rows, scenes and launcher slots (WP-064).
// Each operation is one history step or a refusal that changes nothing.
#include "midi_sequence_model.h"

#include "timeline_time.h"

#include <QCoreApplication>
#include <cmath>

namespace QindaTK {
namespace {

std::optional<qint64> ticksOf(const QVariant &value)
{
    if (value.typeId() == QMetaType::QString) return TimelineTime::parseDecimal(value.toString());
    bool ok = false;
    const double number = value.toDouble(&ok);
    if (!value.isValid() || !ok || !std::isfinite(number) || std::floor(number) != number) return std::nullopt;
    return qint64(number);
}

MidiStepRow *findRow(MidiPattern &pattern, const QUuid &id)
{
    for (MidiStepRow &row : pattern.rows)
        if (row.id == id) return &row;
    return nullptr;
}

MidiStep newStep()
{
    MidiStep step;
    step.id = QUuid::createUuid();
    step.velocity = Midi::velocityFrom7(100);
    return step;
}

} // namespace

QVariantMap MidiSequenceModel::addPattern(const QString &name, const QString &lengthTicks)
{
    MidiPattern pattern;
    pattern.id = QUuid::createUuid();
    pattern.name = name.isEmpty() ? tr("Pattern %1").arg(m_content.patterns.size() + 1) : name;
    pattern.length = qint64(4) * m_ppq; // one 4/4 bar
    if (!lengthTicks.isEmpty()) {
        const auto length = TimelineTime::parseDecimal(lengthTicks);
        if (!length) return failure(tr("The pattern length must be decimal ticks"));
        pattern.length = *length;
    }
    // AGENT-NOTE (D-151): the default seed derives from the pattern ID, so two
    // new patterns roll differently and a pattern always rolls the same way.
    pattern.seed = quint32(Midi::key(pattern.id));
    return editContent(tr("Add pattern"), [&](MidiContent &content) {
        content.patterns.push_back(pattern);
        return QString();
    }, {{QStringLiteral("patternId"), Midi::idText(pattern.id)},
        {QStringLiteral("resourceId"), Midi::patternResourceId(pattern.id)}});
}

QVariantMap MidiSequenceModel::removePattern(const QString &patternId)
{
    const QUuid id = QUuid::fromString(patternId);
    return editContent(tr("Remove pattern"), [&](MidiContent &content) {
        for (const MidiSlot &slot : content.launchSlots)
            if (slot.patternId == id) return tr("A launcher slot plays this pattern; clear the slot first");
        const qsizetype before = content.patterns.size();
        content.patterns.removeIf([&](const MidiPattern &pattern) { return pattern.id == id; });
        return content.patterns.size() == before ? tr("There is no pattern %1").arg(patternId) : QString();
    });
}

QVariantMap MidiSequenceModel::setPattern(const QString &patternId, const QVariantMap &fields)
{
    return editPattern(patternId, tr("Edit pattern"), [&](MidiPattern &pattern) -> QString {
        if (fields.contains(QStringLiteral("name"))) pattern.name = fields.value(QStringLiteral("name")).toString();
        if (fields.contains(QStringLiteral("length"))) {
            const auto length = ticksOf(fields.value(QStringLiteral("length")));
            if (!length) return tr("The pattern length must be decimal ticks");
            pattern.length = *length; // validation refuses a length that would cut off a note or event start
        }
        if (fields.contains(QStringLiteral("seed"))) {
            const auto seed = ticksOf(fields.value(QStringLiteral("seed")));
            if (!seed || *seed < 0 || *seed > 0xFFFFFFFFll) return tr("A seed is a whole number from 0 to 4294967295");
            pattern.seed = quint32(*seed);
        }
        return {};
    });
}

QVariantMap MidiSequenceModel::patternData(const QString &patternId) const
{
    // The committed pattern (what plays and saves); the snapshots below show a drag preview.
    const MidiPattern *source = pattern(QUuid::fromString(patternId));
    if (!source) return {};
    QVariantList notes, events;
    for (const MidiNote &note : source->notes)
        notes.push_back(QVariantMap{{QStringLiteral("id"), Midi::idText(note.id)},
                                    {QStringLiteral("tick"), QString::number(note.tick)},
                                    {QStringLiteral("duration"), QString::number(note.duration)},
                                    {QStringLiteral("pitch"), note.pitch}, {QStringLiteral("channel"), note.channel},
                                    {QStringLiteral("velocity16"), int(note.velocity)}});
    for (const MidiControlEvent &event : source->events)
        events.push_back(QVariantMap{{QStringLiteral("id"), Midi::idText(event.id)},
                                     {QStringLiteral("tick"), QString::number(event.tick)},
                                     {QStringLiteral("controller"), Midi::controllerId(event.kind, event.number)},
                                     {QStringLiteral("channel"), event.channel},
                                     {QStringLiteral("raw"), QString::number(event.value)},
                                     {QStringLiteral("noteId"), Midi::idText(event.noteId)}});
    return {{QStringLiteral("id"), Midi::idText(source->id)}, {QStringLiteral("name"), source->name},
            {QStringLiteral("length"), QString::number(source->length)}, {QStringLiteral("seed"), double(source->seed)},
            {QStringLiteral("notes"), notes}, {QStringLiteral("events"), events},
            {QStringLiteral("rows"), stepRows(patternId)}};
}

QVariantMap MidiSequenceModel::addRow(const QString &patternId, const QVariantMap &input)
{
    MidiStepRow row;
    row.id = QUuid::createUuid();
    row.name = input.value(QStringLiteral("name")).toString();
    row.pitch = input.value(QStringLiteral("pitch"), 36).toInt();
    row.channel = input.value(QStringLiteral("channel"), 9).toInt();
    const auto stepTicks = input.contains(QStringLiteral("stepTicks")) ? ticksOf(input.value(QStringLiteral("stepTicks")))
                                                                      : std::optional<qint64>(m_ppq / 4);
    const int count = input.value(QStringLiteral("steps"), 16).toInt();
    if (!stepTicks || count < 1 || count > Midi::kMaxSteps) return failure(tr("A step row has 1 to 64 steps of decimal ticks"));
    row.stepTicks = *stepTicks;
    for (int i = 0; i < count; ++i) row.steps.push_back(newStep());
    return editPattern(patternId, tr("Add step row"), [&](MidiPattern &pattern) {
        if (row.name.isEmpty()) row.name = tr("Row %1").arg(pattern.rows.size() + 1);
        pattern.rows.push_back(row);
        return QString();
    }, {{QStringLiteral("rowId"), Midi::idText(row.id)}});
}

QVariantMap MidiSequenceModel::setRow(const QString &patternId, const QString &rowId, const QVariantMap &fields)
{
    const QUuid id = QUuid::fromString(rowId);
    return editPattern(patternId, tr("Edit step row"), [&](MidiPattern &pattern) -> QString {
        MidiStepRow *row = findRow(pattern, id);
        if (!row) return tr("There is no step row %1").arg(rowId);
        if (fields.contains(QStringLiteral("name"))) row->name = fields.value(QStringLiteral("name")).toString();
        if (fields.contains(QStringLiteral("pitch"))) row->pitch = fields.value(QStringLiteral("pitch")).toInt();
        if (fields.contains(QStringLiteral("channel"))) row->channel = fields.value(QStringLiteral("channel")).toInt();
        if (fields.contains(QStringLiteral("stepTicks"))) {
            const auto stepTicks = ticksOf(fields.value(QStringLiteral("stepTicks")));
            if (!stepTicks) return tr("A step length must be decimal ticks");
            row->stepTicks = *stepTicks;
        }
        if (fields.contains(QStringLiteral("steps"))) { // polymetric length: add disabled steps or drop the last ones
            const int count = fields.value(QStringLiteral("steps")).toInt();
            if (count < 1 || count > Midi::kMaxSteps) return tr("A step row has 1 to 64 steps");
            while (row->steps.size() > count) row->steps.removeLast();
            while (row->steps.size() < count) row->steps.push_back(newStep());
        }
        return {};
    });
}

QVariantMap MidiSequenceModel::removeRow(const QString &patternId, const QString &rowId)
{
    const QUuid id = QUuid::fromString(rowId);
    return editPattern(patternId, tr("Remove step row"), [&](MidiPattern &pattern) {
        const qsizetype before = pattern.rows.size();
        pattern.rows.removeIf([&](const MidiStepRow &row) { return row.id == id; });
        return pattern.rows.size() == before ? tr("There is no step row %1").arg(rowId) : QString();
    });
}

QVariantMap MidiSequenceModel::setStep(const QString &patternId, const QString &rowId, const QString &stepId,
                                       const QString &field, const QVariant &value)
{
    const QUuid row = QUuid::fromString(rowId);
    const QUuid id = QUuid::fromString(stepId);
    return editPattern(patternId, tr("Edit step"), [&](MidiPattern &pattern) -> QString {
        MidiStepRow *target = findRow(pattern, row);
        if (!target) return tr("There is no step row %1").arg(rowId);
        for (MidiStep &step : target->steps) {
            if (step.id != id) continue;
            bool ok = true;
            if (field == QLatin1String("enabled")) step.enabled = value.toBool();
            else if (field == QLatin1String("velocity")) step.velocity = Midi::velocityFromUnit(value.toDouble(&ok));
            else if (field == QLatin1String("gate")) step.gate = value.toDouble(&ok);
            else if (field == QLatin1String("probability")) step.probability = value.toDouble(&ok);
            else return tr("A step has enabled, velocity, gate and probability");
            return ok ? QString() : tr("The step value must be a number");
        }
        return tr("There is no step %1").arg(stepId);
    });
}

QVariantMap MidiSequenceModel::addScene(const QString &name)
{
    MidiScene scene{QUuid::createUuid(), name};
    return editContent(tr("Add scene"), [&](MidiContent &content) {
        if (scene.name.isEmpty()) scene.name = tr("Scene %1").arg(content.scenes.size() + 1);
        content.scenes.push_back(scene);
        return QString();
    }, {{QStringLiteral("sceneId"), Midi::idText(scene.id)}});
}

QVariantMap MidiSequenceModel::removeScene(const QString &sceneId)
{
    const QUuid id = QUuid::fromString(sceneId);
    return editContent(tr("Remove scene"), [&](MidiContent &content) {
        const qsizetype before = content.scenes.size();
        content.scenes.removeIf([&](const MidiScene &scene) { return scene.id == id; });
        content.launchSlots.removeIf([&](const MidiSlot &slot) { return slot.sceneId == id; });
        return content.scenes.size() == before ? tr("There is no scene %1").arg(sceneId) : QString();
    });
}

QVariantMap MidiSequenceModel::setSlot(const QString &trackId, const QString &sceneId, const QString &patternId,
                                       const QString &label)
{
    MidiSlot slot{QUuid::createUuid(), QUuid::fromString(trackId), QUuid::fromString(sceneId),
                  QUuid::fromString(patternId), label};
    // One slot per track and scene: an existing cell keeps its ID and gets
    // the new pattern, so a launched slot keeps playing under its key.
    for (const MidiSlot &existing : m_content.launchSlots)
        if (existing.trackId == slot.trackId && existing.sceneId == slot.sceneId) slot.id = existing.id;
    return editContent(tr("Set launcher slot"), [&](MidiContent &content) {
        content.launchSlots.removeIf([&](const MidiSlot &existing) { return existing.id == slot.id; });
        content.launchSlots.push_back(slot);
        return QString();
    }, {{QStringLiteral("slotId"), Midi::idText(slot.id)}});
}

QVariantMap MidiSequenceModel::clearSlot(const QString &slotId)
{
    const QUuid id = QUuid::fromString(slotId);
    return editContent(tr("Clear launcher slot"), [&](MidiContent &content) {
        const qsizetype before = content.launchSlots.size();
        content.launchSlots.removeIf([&](const MidiSlot &slot) { return slot.id == id; });
        return content.launchSlots.size() == before ? tr("There is no slot %1").arg(slotId) : QString();
    });
}

} // namespace QindaTK
