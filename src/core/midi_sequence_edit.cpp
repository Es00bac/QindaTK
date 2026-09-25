// SPDX-License-Identifier: LGPL-3.0-or-later
// MidiSequenceModel: notes, controller events and drag gestures (WP-064).
// Every operation plans on a copy of one pattern and commits it whole as one
// history step (midi_sequence_model.cpp), so undo is exact.
#include "midi_sequence_model.h"

#include "timeline_time.h"

#include <QCoreApplication>
#include <QSet>
#include <algorithm>
#include <cmath>

namespace QindaTK {
namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("MidiSequenceModel", text);
}

// Exact ticks from a decimal string or an integral QML number.
std::optional<qint64> ticksOf(const QVariant &value)
{
    if (!value.isValid()) return std::nullopt;
    if (value.typeId() == QMetaType::QString) return TimelineTime::parseDecimal(value.toString());
    bool ok = false;
    const double number = value.toDouble(&ok);
    if (!ok || !std::isfinite(number) || std::floor(number) != number || std::abs(number) > 9007199254740992.0)
        return std::nullopt;
    return qint64(number);
}

std::optional<quint16> velocityOf(const QVariantMap &map)
{
    if (map.contains(QStringLiteral("velocity16"))) {
        const int value = map.value(QStringLiteral("velocity16")).toInt();
        // 0 is refused by validation, so an out-of-range value is reported, never clamped.
        return quint16(value >= 1 && value <= 65535 ? value : 0);
    }
    if (map.contains(QStringLiteral("velocity")))
        return Midi::velocityFromUnit(map.value(QStringLiteral("velocity")).toDouble());
    return std::nullopt;
}

MidiNote *findNote(MidiPattern &pattern, const QUuid &id)
{
    for (MidiNote &note : pattern.notes)
        if (note.id == id) return &note;
    return nullptr;
}

MidiControlEvent *findEvent(MidiPattern &pattern, const QUuid &id)
{
    for (MidiControlEvent &event : pattern.events)
        if (event.id == id) return &event;
    return nullptr;
}

// AGENT-GUARD (AUD-23): per-note events never detach from their note. They
// move with it, stay inside it (and inside the pattern) and share its channel.
void keepAttached(MidiPattern &pattern, const MidiNote &note)
{
    const qint64 last = std::max(note.tick, std::min(note.end() - 1, pattern.length - 1));
    for (MidiControlEvent &event : pattern.events)
        if (event.noteId == note.id) {
            event.tick = std::clamp(event.tick, note.tick, last);
            event.channel = note.channel;
        }
}

} // namespace

namespace MidiEdit {

qint64 roundDiv(qint64 numerator, qint64 denominator)
{
    qint64 quotient = numerator / denominator;
    const qint64 remainder = numerator % denominator;
    if (2 * (remainder < 0 ? -remainder : remainder) >= denominator) quotient += numerator < 0 ? -1 : 1;
    return quotient;
}

QString moveNote(MidiPattern &pattern, const QUuid &noteId, qint64 tick, int pitch)
{
    MidiNote *note = findNote(pattern, noteId);
    if (!note) return tr("There is no note %1").arg(Midi::idText(noteId));
    if (tick < 0 || tick >= pattern.length) return tr("A note starts outside the pattern");
    if (pitch < 0 || pitch > 127) return tr("A note pitch must be 0 to 127");
    const qint64 delta = tick - note->tick;
    note->tick = tick;
    note->pitch = pitch;
    for (MidiControlEvent &event : pattern.events)
        if (event.noteId == noteId) event.tick += delta;
    keepAttached(pattern, *note);
    return {};
}

QString resizeNote(MidiPattern &pattern, const QUuid &noteId, qint64 end)
{
    MidiNote *note = findNote(pattern, noteId);
    if (!note) return tr("There is no note %1").arg(Midi::idText(noteId));
    if (end <= note->tick || end - note->tick > Midi::kMaxLength) return tr("A note needs a positive length");
    note->duration = end - note->tick;
    keepAttached(pattern, *note);
    return {};
}

} // namespace MidiEdit

QVariantMap MidiSequenceModel::insertNote(const QString &patternId, const QVariantMap &input)
{
    MidiNote note;
    note.id = QUuid::createUuid();
    std::optional<qint64> tick = ticksOf(input.value(QStringLiteral("tick")));
    std::optional<qint64> duration = ticksOf(input.value(QStringLiteral("duration")));
    if (input.contains(QStringLiteral("startTicks"))) {
        tick = ticksOf(input.value(QStringLiteral("startTicks")));
        const auto end = ticksOf(input.value(QStringLiteral("endTicks")));
        duration = tick && end ? std::optional<qint64>(*end - *tick) : std::nullopt;
    }
    if (!tick || !duration) return failure(tr("A note needs decimal tick positions"));
    note.tick = *tick;
    note.duration = *duration;
    note.pitch = input.value(QStringLiteral("pitch"), 60).toInt();
    note.channel = input.value(QStringLiteral("channel"), 0).toInt();
    note.velocity = velocityOf(input).value_or(Midi::velocityFrom7(100));
    return editPattern(patternId, tr("Insert note"), [&](MidiPattern &pattern) {
        if (pattern.notes.size() >= Midi::kMaxNotes) return tr("The pattern is full");
        pattern.notes.push_back(note);
        return QString();
    }, {{QStringLiteral("noteId"), Midi::idText(note.id)}});
}

QVariantMap MidiSequenceModel::setNote(const QString &patternId, const QString &noteId, const QVariantMap &fields)
{
    const QUuid id = QUuid::fromString(noteId);
    return editPattern(patternId, tr("Edit note"), [&](MidiPattern &pattern) -> QString {
        const MidiNote *current = findNote(pattern, id);
        if (!current) return tr("There is no note %1").arg(noteId);
        const auto tick = fields.contains(QStringLiteral("tick")) ? ticksOf(fields.value(QStringLiteral("tick")))
                                                                  : std::optional<qint64>(current->tick);
        const auto duration = fields.contains(QStringLiteral("duration"))
            ? ticksOf(fields.value(QStringLiteral("duration"))) : std::optional<qint64>(current->duration);
        if (!tick || !duration) return tr("A note needs decimal tick positions");
        const int pitch = fields.value(QStringLiteral("pitch"), current->pitch).toInt();
        if (const QString error = MidiEdit::moveNote(pattern, id, *tick, pitch); !error.isEmpty()) return error;
        if (const QString error = MidiEdit::resizeNote(pattern, id, *tick + *duration); !error.isEmpty()) return error;
        MidiNote *note = findNote(pattern, id);
        if (fields.contains(QStringLiteral("channel"))) note->channel = fields.value(QStringLiteral("channel")).toInt();
        if (const auto velocity = velocityOf(fields)) note->velocity = *velocity;
        keepAttached(pattern, *note);
        return {};
    });
}

QVariantMap MidiSequenceModel::moveNotes(const QString &patternId, const QStringList &noteIds,
                                         const QString &deltaTicks, int deltaPitch)
{
    const auto delta = TimelineTime::parseDecimal(deltaTicks);
    if (!delta || noteIds.isEmpty()) return failure(tr("Name the notes and a decimal tick offset"));
    return editPattern(patternId, noteIds.size() == 1 ? tr("Move note") : tr("Move notes"), [&](MidiPattern &pattern) {
        for (const QString &text : noteIds) {
            const QUuid id = QUuid::fromString(text);
            const MidiNote *note = findNote(pattern, id);
            if (!note) return tr("There is no note %1").arg(text);
            if (const QString error = MidiEdit::moveNote(pattern, id, note->tick + *delta, note->pitch + deltaPitch);
                !error.isEmpty())
                return error;
        }
        return QString();
    });
}

QVariantMap MidiSequenceModel::deleteNotes(const QString &patternId, const QStringList &noteIds)
{
    if (noteIds.isEmpty()) return failure(tr("Name the notes to delete"));
    return editPattern(patternId, noteIds.size() == 1 ? tr("Delete note") : tr("Delete notes"), [&](MidiPattern &pattern) {
        QSet<QUuid> targets;
        for (const QString &text : noteIds) {
            const QUuid id = QUuid::fromString(text);
            if (!findNote(pattern, id)) return tr("There is no note %1").arg(text);
            targets.insert(id);
        }
        pattern.notes.removeIf([&](const MidiNote &note) { return targets.contains(note.id); });
        pattern.events.removeIf([&](const MidiControlEvent &event) { return targets.contains(event.noteId); });
        return QString();
    });
}

QVariantMap MidiSequenceModel::quantizeNotes(const QString &patternId, const QStringList &noteIds,
                                             const QString &gridTicks, int strengthPercent, bool ends)
{
    const auto grid = TimelineTime::parseDecimal(gridTicks);
    if (!grid || *grid < 1 || strengthPercent < 0 || strengthPercent > 100)
        return failure(tr("Quantize needs a grid of at least one tick and a strength of 0 to 100 %"));
    return editPattern(patternId, tr("Quantize notes"), [&](MidiPattern &pattern) {
        QList<QUuid> targets;
        for (const QString &text : noteIds) targets.push_back(QUuid::fromString(text));
        if (targets.isEmpty())
            for (const MidiNote &note : pattern.notes) targets.push_back(note.id);
        // AGENT-NOTE: exact integer arithmetic; a position moves by
        // round((line - position) * strength / 100) toward the nearest line.
        const auto toward = [&](qint64 position) {
            qint64 line = MidiEdit::roundDiv(position, *grid) * *grid;
            return position + MidiEdit::roundDiv((line - position) * strengthPercent, 100);
        };
        for (const QUuid &id : targets) {
            const MidiNote *note = findNote(pattern, id);
            if (!note) return tr("There is no note %1").arg(Midi::idText(id));
            qint64 start = toward(note->tick);
            if (start >= pattern.length) start = (pattern.length - 1) / *grid * *grid; // the last line inside
            const qint64 end = std::max(start + 1, ends ? toward(note->end()) : start + note->duration);
            if (const QString error = MidiEdit::moveNote(pattern, id, start, note->pitch); !error.isEmpty()) return error;
            if (const QString error = MidiEdit::resizeNote(pattern, id, end); !error.isEmpty()) return error;
        }
        return QString();
    });
}

QVariantMap MidiSequenceModel::setEvent(const QString &patternId, const QVariantMap &input)
{
    const QString controller = input.value(QStringLiteral("controller")).toString();
    const QString idText = input.value(QStringLiteral("id")).toString();
    if (controller == QLatin1String("velocity")) { // the velocity lane edits notes
        QVariantMap fields;
        if (input.contains(QStringLiteral("raw")))
            fields.insert(QStringLiteral("velocity16"), input.value(QStringLiteral("raw")).toString().toInt());
        else fields.insert(QStringLiteral("velocity"), input.value(QStringLiteral("value")));
        const QString noteId = idText.isEmpty() ? input.value(QStringLiteral("noteId")).toString() : idText;
        return setNote(patternId, noteId, fields);
    }
    const bool inserting = idText.isEmpty();
    const QUuid id = inserting ? QUuid::createUuid() : QUuid::fromString(idText);
    return editPattern(patternId, inserting ? tr("Insert controller event") : tr("Edit controller event"),
                       [&](MidiPattern &pattern) -> QString {
        MidiControlEvent *existing = inserting ? nullptr : findEvent(pattern, id);
        if (!inserting && !existing) return tr("There is no controller event %1").arg(idText);
        MidiControlEvent event = existing ? *existing : MidiControlEvent{};
        event.id = id;
        if (!controller.isEmpty()) {
            const auto parsed = Midi::parseController(controller);
            if (!parsed) return tr("Unknown controller %1").arg(controller);
            event.kind = parsed->first;
            event.number = parsed->second;
        } else if (inserting) {
            return tr("A controller event needs a controller");
        }
        if (input.contains(QStringLiteral("noteId"))) event.noteId = QUuid::fromString(input.value(QStringLiteral("noteId")).toString());
        const MidiNote *note = Midi::perNote(event.kind) ? findNote(pattern, event.noteId) : nullptr;
        if (Midi::perNote(event.kind) && !note) return tr("A per-note event needs one of the pattern's notes");
        if (!note) event.noteId = QUuid();
        if (input.contains(QStringLiteral("tick"))) {
            const auto tick = ticksOf(input.value(QStringLiteral("tick")));
            if (!tick) return tr("A controller event needs a decimal tick");
            event.tick = *tick;
        } else if (inserting && note) {
            event.tick = note->tick;
        }
        if (note) event.channel = note->channel;
        else if (input.contains(QStringLiteral("channel"))) event.channel = input.value(QStringLiteral("channel")).toInt();
        if (input.contains(QStringLiteral("raw"))) {
            const auto raw = TimelineTime::parseDecimal(input.value(QStringLiteral("raw")).toString());
            if (!raw || *raw < 0 || *raw > 0xFFFFFFFFll) return tr("A raw controller value is a 32-bit decimal");
            event.value = quint32(*raw);
        } else if (input.contains(QStringLiteral("value"))) {
            event.value = Midi::valueFromUnit(event.kind, input.value(QStringLiteral("value")).toDouble());
        } else if (inserting) {
            event.value = Midi::defaultValue(event.kind);
        }
        if (existing) *existing = event;
        else if (pattern.events.size() >= Midi::kMaxEvents) return tr("The pattern is full");
        else pattern.events.push_back(event);
        return {};
    }, {{QStringLiteral("eventId"), Midi::idText(id)}});
}

QVariantMap MidiSequenceModel::deleteEvents(const QString &patternId, const QStringList &eventIds)
{
    if (eventIds.isEmpty()) return failure(tr("Name the controller events to delete"));
    return editPattern(patternId, tr("Delete controller events"), [&](MidiPattern &pattern) {
        QSet<QUuid> targets;
        for (const QString &text : eventIds) {
            const QUuid id = QUuid::fromString(text);
            if (findNote(pattern, id)) return tr("A velocity belongs to its note; delete the note instead");
            if (!findEvent(pattern, id)) return tr("There is no controller event %1").arg(text);
            targets.insert(id);
        }
        pattern.events.removeIf([&](const MidiControlEvent &event) { return targets.contains(event.id); });
        return QString();
    });
}

bool MidiSequenceModel::beginGesture(const QString &requestId, const QUuid &patternId, const QString &label,
                                     QString *error)
{
    const MidiPattern *source = pattern(patternId);
    if (!source) {
        *error = tr("There is no pattern %1").arg(Midi::idText(patternId));
        return false;
    }
    // A new gesture replaces one its control never finished; nothing of the
    // abandoned preview was ever committed.
    m_gesture = Gesture{requestId, patternId, *source, *source, label};
    emit previewChanged();
    return true;
}

QVariantMap MidiSequenceModel::previewGesture(const std::function<QString(MidiPattern &)> &edit)
{
    MidiPattern candidate = m_gesture->base; // every update plans from the committed pattern
    if (const QString error = edit(candidate); !error.isEmpty()) return failure(error);
    Midi::sortPattern(candidate);
    if (const QString error = Midi::validate(candidate); !error.isEmpty()) return failure(error);
    m_gesture->preview = candidate;
    emit previewChanged();
    return ok();
}

QVariantMap MidiSequenceModel::previewNoteEdit(const QString &requestId, const QString &patternId,
                                               const QString &noteId, const QString &operation,
                                               const QString &targetTicks, int targetPitch)
{
    clearError();
    if (!m_gesture || m_gesture->requestId != requestId) {
        QString error;
        const QString label = operation == QLatin1String("move") ? tr("Move note") : tr("Resize note");
        if (!beginGesture(requestId, QUuid::fromString(patternId), label, &error)) return failure(error);
    }
    const auto target = TimelineTime::parseDecimal(targetTicks);
    if (!target) return failure(tr("A note edit needs a decimal tick"));
    const QUuid id = QUuid::fromString(noteId);
    return previewGesture([&](MidiPattern &pattern) -> QString {
        const MidiNote *note = findNote(pattern, id);
        if (!note) return tr("There is no note %1").arg(noteId);
        if (operation == QLatin1String("move"))
            return MidiEdit::moveNote(pattern, id, std::clamp<qint64>(*target, 0, pattern.length - 1),
                                      std::clamp(targetPitch, 0, 127));
        if (operation == QLatin1String("resizeEnd")) return MidiEdit::resizeNote(pattern, id, std::max(*target, note->tick + 1));
        if (operation == QLatin1String("resizeStart")) {
            const qint64 end = note->end();
            const qint64 start = std::clamp<qint64>(*target, 0, std::min(end - 1, pattern.length - 1));
            if (const QString error = MidiEdit::moveNote(pattern, id, start, note->pitch); !error.isEmpty()) return error;
            return MidiEdit::resizeNote(pattern, id, end);
        }
        return tr("Unknown note edit %1").arg(operation);
    });
}

QVariantMap MidiSequenceModel::previewEventEdit(const QString &requestId, const QString &patternId,
                                                const QString &controllerId, const QString &eventId,
                                                const QString &ticks, double value)
{
    clearError();
    if (!m_gesture || m_gesture->requestId != requestId) {
        QString error;
        const QString label = tr("Edit %1").arg(Midi::descriptor(controllerId).value(QStringLiteral("label")).toString());
        if (!beginGesture(requestId, QUuid::fromString(patternId), label, &error)) return failure(error);
    }
    const QUuid id = QUuid::fromString(eventId);
    const auto tick = ticks.isEmpty() ? std::nullopt : TimelineTime::parseDecimal(ticks);
    return previewGesture([&](MidiPattern &pattern) -> QString {
        if (controllerId == QLatin1String("velocity")) {
            MidiNote *note = findNote(pattern, id);
            if (!note) return tr("There is no note %1").arg(eventId);
            note->velocity = Midi::velocityFromUnit(value);
            return {};
        }
        MidiControlEvent *event = findEvent(pattern, id);
        if (!event) return tr("There is no controller event %1").arg(eventId);
        if (Midi::controllerId(event->kind, event->number) != controllerId)
            return tr("The event belongs to another controller lane");
        event->value = Midi::valueFromUnit(event->kind, value);
        if (tick) {
            event->tick = std::clamp<qint64>(*tick, 0, pattern.length - 1);
            if (MidiNote *note = findNote(pattern, event->noteId)) keepAttached(pattern, *note);
        }
        return {};
    });
}

QVariantMap MidiSequenceModel::finishEdit(const QString &requestId)
{
    clearError();
    if (!m_gesture || m_gesture->requestId != requestId) return failure(tr("No edit %1 is open").arg(requestId));
    const Gesture gesture = *m_gesture;
    m_gesture.reset();
    emit previewChanged();
    MidiContent next = m_content;
    for (MidiPattern &pattern : next.patterns)
        if (pattern.id == gesture.patternId) pattern = gesture.preview;
    QString error;
    if (!commitContent(next, gesture.label, &error)) return failure(error);
    return ok();
}

bool MidiSequenceModel::cancelEdit(const QString &requestId)
{
    if (!m_gesture || m_gesture->requestId != requestId) return false;
    m_gesture.reset();
    emit previewChanged();
    return true;
}

} // namespace QindaTK
