// SPDX-License-Identifier: LGPL-3.0-or-later
#include "midi_types.h"

#include "timeline_time.h"

#include <QCoreApplication>
#include <QHash>
#include <QJsonArray>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <tuple>

namespace QindaTK {
namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("Midi", text);
}

struct Name { MidiController kind; const char *id; };
constexpr Name kNames[] = {{MidiController::PitchBend, "pitchBend"},
                           {MidiController::ChannelPressure, "channelPressure"},
                           {MidiController::NoteBend, "noteBend"},
                           {MidiController::NotePressure, "notePressure"}};

// Integral JSON numbers in [minimum, maximum]; anything else is refused.
std::optional<qint64> integer(const QJsonValue &value, qint64 minimum, qint64 maximum)
{
    if (!value.isDouble()) return std::nullopt;
    const double number = value.toDouble();
    if (!std::isfinite(number) || std::floor(number) != number || number < double(minimum) || number > double(maximum))
        return std::nullopt;
    return qint64(number);
}

std::optional<qint64> ticks(const QJsonValue &value)
{
    return value.isString() ? TimelineTime::parseDecimal(value.toString()) : std::nullopt;
}

QUuid uuid(const QJsonValue &value)
{
    return value.isString() ? QUuid::fromString(value.toString()) : QUuid();
}

QString t(qint64 value)
{
    return QString::number(value);
}

} // namespace

namespace Midi {

quint32 scaleDown(quint32 value, int fromBits, int toBits)
{
    if (toBits >= fromBits) return value;
    return quint32(quint64(value) >> (fromBits - toBits));
}

quint32 scaleUp(quint32 value, int fromBits, int toBits)
{
    if (toBits <= fromBits) return scaleDown(value, fromBits, toBits);
    if (fromBits < 2) return value ? quint32((quint64(1) << toBits) - 1) : 0;
    const int scaleBits = toBits - fromBits;
    quint64 shifted = quint64(value) << scaleBits;
    const quint64 center = quint64(1) << (fromBits - 1);
    if (value <= center) return quint32(shifted);
    const int repeatBits = fromBits - 1;
    const quint64 mask = (quint64(1) << repeatBits) - 1;
    quint64 repeat = quint64(value) & mask;
    if (scaleBits > repeatBits) repeat <<= scaleBits - repeatBits;
    else repeat >>= repeatBits - scaleBits;
    while (repeat != 0) {
        shifted |= repeat;
        repeat >>= repeatBits;
    }
    return quint32(shifted);
}

quint16 velocityFrom7(int velocity7)
{
    return quint16(scaleUp(quint32(std::clamp(velocity7, 1, 127)), 7, 16));
}

int velocityTo7(quint16 velocity)
{
    return std::max(1, int(velocity >> 9));
}

quint16 velocityFromUnit(double unit)
{
    const double clamped = std::isfinite(unit) ? std::clamp(unit, 0.0, 1.0) : 0.0;
    return quint16(std::max<long long>(1, std::llround(clamped * 65535.0)));
}

double velocityToUnit(quint16 velocity)
{
    return double(velocity) / 65535.0;
}

int resolutionBits(MidiController kind)
{
    switch (kind) {
    case MidiController::PitchBend: return 14;
    case MidiController::NoteBend:
    case MidiController::NotePressure: return 32;
    case MidiController::ControlChange:
    case MidiController::ChannelPressure: break;
    }
    return 7;
}

bool perNote(MidiController kind)
{
    return kind == MidiController::NoteBend || kind == MidiController::NotePressure;
}

quint32 defaultValue(MidiController kind)
{
    return kind == MidiController::PitchBend || kind == MidiController::NoteBend ? 0x80000000u : 0u;
}

QString controllerId(MidiController kind, int number)
{
    if (kind == MidiController::ControlChange) return QStringLiteral("cc:%1").arg(number);
    for (const Name &name : kNames)
        if (name.kind == kind) return QString::fromLatin1(name.id);
    return {};
}

std::optional<std::pair<MidiController, int>> parseController(const QString &id)
{
    if (id.startsWith(QLatin1String("cc:"))) {
        bool ok = false;
        const int number = id.mid(3).toInt(&ok);
        if (!ok || number < 0 || number > 127 || QString::number(number) != id.mid(3)) return std::nullopt;
        return std::make_pair(MidiController::ControlChange, number);
    }
    for (const Name &name : kNames)
        if (id == QLatin1String(name.id)) return std::make_pair(name.kind, 0);
    return std::nullopt;
}

quint32 valueFromUnit(MidiController kind, double unit)
{
    const int bits = resolutionBits(kind);
    const double maximum = double((quint64(1) << bits) - 1);
    const double clamped = std::isfinite(unit) ? std::clamp(unit, 0.0, 1.0) : 0.0;
    return scaleUp(quint32(std::llround(clamped * maximum)), bits, 32);
}

double valueToUnit(MidiController kind, quint32 value)
{
    const int bits = resolutionBits(kind);
    return double(scaleDown(value, 32, bits)) / double((quint64(1) << bits) - 1);
}

QVariantMap descriptor(const QString &id)
{
    if (id == QLatin1String("velocity"))
        return {{QStringLiteral("id"), id}, {QStringLiteral("label"), tr("Velocity")},
                {QStringLiteral("resolutionBits"), 16}, {QStringLiteral("minimum"), 0.0},
                {QStringLiteral("maximum"), 1.0}, {QStringLiteral("defaultValue"), velocityToUnit(0x8000)},
                {QStringLiteral("perNote"), true}, {QStringLiteral("smf"), true}};
    const auto parsed = parseController(id);
    if (!parsed) return {};
    const MidiController kind = parsed->first;
    QString label;
    switch (kind) {
    case MidiController::ControlChange: label = tr("CC %1").arg(parsed->second); break;
    case MidiController::PitchBend: label = tr("Pitch bend"); break;
    case MidiController::ChannelPressure: label = tr("Channel pressure"); break;
    case MidiController::NoteBend: label = tr("Per-note pitch bend"); break;
    case MidiController::NotePressure: label = tr("Per-note pressure"); break;
    }
    return {{QStringLiteral("id"), id}, {QStringLiteral("label"), label},
            {QStringLiteral("resolutionBits"), resolutionBits(kind)}, {QStringLiteral("minimum"), 0.0},
            {QStringLiteral("maximum"), 1.0}, {QStringLiteral("defaultValue"), valueToUnit(kind, defaultValue(kind))},
            {QStringLiteral("perNote"), perNote(kind)}, {QStringLiteral("smf"), !perNote(kind)}};
}

QVariantList descriptors()
{
    QVariantList list;
    for (const char *id : {"velocity", "pitchBend", "channelPressure", "noteBend", "notePressure", "cc:1", "cc:7",
                           "cc:10", "cc:11", "cc:64", "cc:74"})
        list.push_back(descriptor(QString::fromLatin1(id)));
    return list;
}

QString quantizeName(MidiQuantize quantize)
{
    switch (quantize) {
    case MidiQuantize::None: return QStringLiteral("none");
    case MidiQuantize::Beat: return QStringLiteral("beat");
    case MidiQuantize::Bar: break;
    }
    return QStringLiteral("bar");
}

std::optional<MidiQuantize> parseQuantize(const QString &name)
{
    if (name == QLatin1String("none")) return MidiQuantize::None;
    if (name == QLatin1String("beat")) return MidiQuantize::Beat;
    if (name == QLatin1String("bar")) return MidiQuantize::Bar;
    return std::nullopt;
}

QString idText(const QUuid &id)
{
    return id.isNull() ? QString() : id.toString(QUuid::WithoutBraces);
}

QString patternResourceId(const QUuid &patternId)
{
    return QStringLiteral("pattern:") + idText(patternId);
}

QUuid patternFromResource(const QString &resourceId)
{
    return resourceId.startsWith(QLatin1String("pattern:")) ? QUuid::fromString(resourceId.mid(8)) : QUuid();
}

quint64 key(const QUuid &id)
{
    quint64 hash = 1469598103934665603ull; // FNV-1a
    const QByteArray bytes = id.toRfc4122();
    for (const char byte : bytes) {
        hash ^= quint8(byte);
        hash *= 1099511628211ull;
    }
    return hash ? hash : 1;
}

void sortPattern(MidiPattern &pattern)
{
    std::sort(pattern.notes.begin(), pattern.notes.end(), [](const MidiNote &a, const MidiNote &b) {
        return std::tie(a.tick, a.pitch, a.channel) != std::tie(b.tick, b.pitch, b.channel)
                   ? std::tie(a.tick, a.pitch, a.channel) < std::tie(b.tick, b.pitch, b.channel)
                   : a.id < b.id;
    });
    std::sort(pattern.events.begin(), pattern.events.end(), [](const MidiControlEvent &a, const MidiControlEvent &b) {
        const auto left = std::make_tuple(a.tick, int(a.kind), a.number, a.channel);
        const auto right = std::make_tuple(b.tick, int(b.kind), b.number, b.channel);
        return left != right ? left < right : a.id < b.id;
    });
}

QString validate(const MidiPattern &pattern)
{
    if (pattern.id.isNull()) return tr("A pattern needs an ID");
    if (pattern.length < 1 || pattern.length > kMaxLength) return tr("The pattern length must be 1 to 2^40 ticks");
    if (pattern.notes.size() > kMaxNotes || pattern.events.size() > kMaxEvents || pattern.rows.size() > kMaxRows)
        return tr("The pattern has too many notes, events or rows");
    QHash<QUuid, const MidiNote *> notes;
    for (const MidiNote &note : pattern.notes) {
        if (note.id.isNull() || notes.contains(note.id)) return tr("Every note needs its own ID");
        if (note.tick < 0 || note.tick >= pattern.length) return tr("A note starts outside the pattern");
        if (note.duration < 1 || note.duration > kMaxLength) return tr("A note needs a positive length");
        if (note.pitch < 0 || note.pitch > 127) return tr("A note pitch must be 0 to 127");
        if (note.channel < 0 || note.channel > 15) return tr("A MIDI channel must be 1 to 16");
        if (note.velocity < 1) return tr("A note velocity must be above zero");
        notes.insert(note.id, &note);
    }
    QSet<QUuid> ids;
    for (const MidiControlEvent &event : pattern.events) {
        if (event.id.isNull() || ids.contains(event.id)) return tr("Every controller event needs its own ID");
        ids.insert(event.id);
        if (event.tick < 0 || event.tick >= pattern.length) return tr("A controller event lies outside the pattern");
        if (event.channel < 0 || event.channel > 15) return tr("A MIDI channel must be 1 to 16");
        if (event.kind == MidiController::ControlChange && (event.number < 0 || event.number > 127))
            return tr("A controller number must be 0 to 127");
        if (perNote(event.kind)) {
            const MidiNote *note = notes.value(event.noteId);
            if (!note) return tr("A per-note event must belong to a note of the pattern");
            if (event.tick < note->tick || event.tick >= note->end() || event.channel != note->channel)
                return tr("A per-note event must lie within its note, on its channel");
        } else if (!event.noteId.isNull()) {
            return tr("Only per-note events belong to a note");
        }
    }
    ids.clear();
    for (const MidiStepRow &row : pattern.rows) {
        if (row.id.isNull() || ids.contains(row.id)) return tr("Every step row needs its own ID");
        ids.insert(row.id);
        if (row.steps.isEmpty() || row.steps.size() > kMaxSteps) return tr("A step row has 1 to 64 steps");
        if (row.stepTicks < 1 || row.stepTicks > kMaxLength / kMaxSteps) return tr("A step must be at least one tick");
        if (row.pitch < 0 || row.pitch > 127 || row.channel < 0 || row.channel > 15)
            return tr("A step row needs a pitch 0 to 127 and a channel 1 to 16");
        for (const MidiStep &step : row.steps) {
            if (step.id.isNull() || ids.contains(step.id)) return tr("Every step needs its own ID");
            ids.insert(step.id);
            if (step.velocity < 1 || !(step.gate > 0.0 && step.gate <= 1.0)
                || !(step.probability >= 0.0 && step.probability <= 1.0))
                return tr("A step needs a velocity, a gate in (0, 1] and a probability in [0, 1]");
        }
    }
    return {};
}

QString validate(const MidiContent &content)
{
    if (content.patterns.size() > kMaxPatterns || content.scenes.size() > kMaxScenes || content.launchSlots.size() > kMaxSlots)
        return tr("The MIDI content has too many patterns, scenes or slots");
    QSet<QUuid> patterns, scenes, slotIds;
    for (const MidiPattern &pattern : content.patterns) {
        if (const QString error = validate(pattern); !error.isEmpty()) return error;
        if (patterns.contains(pattern.id)) return tr("Every pattern needs its own ID");
        patterns.insert(pattern.id);
    }
    for (const MidiScene &scene : content.scenes) {
        if (scene.id.isNull() || scenes.contains(scene.id)) return tr("Every scene needs its own ID");
        scenes.insert(scene.id);
    }
    QSet<std::pair<QUuid, QUuid>> cells;
    for (const MidiSlot &slot : content.launchSlots) {
        if (slot.id.isNull() || slotIds.contains(slot.id)) return tr("Every slot needs its own ID");
        slotIds.insert(slot.id);
        if (slot.trackId.isNull() || !scenes.contains(slot.sceneId) || !patterns.contains(slot.patternId))
            return tr("A slot needs a track, one of the scenes and one of the patterns");
        if (cells.contains({slot.trackId, slot.sceneId})) return tr("A track has one slot per scene");
        cells.insert({slot.trackId, slot.sceneId});
    }
    return {};
}

const MidiPattern *find(const MidiContent &content, const QUuid &patternId)
{
    for (const MidiPattern &pattern : content.patterns)
        if (pattern.id == patternId) return &pattern;
    return nullptr;
}

QJsonObject toJson(const MidiContent &content, int ticksPerQuarter)
{
    QJsonArray patterns;
    for (const MidiPattern &pattern : content.patterns) {
        QJsonArray notes, events, rows;
        for (const MidiNote &note : pattern.notes)
            notes.push_back(QJsonObject{{QStringLiteral("id"), idText(note.id)}, {QStringLiteral("tick"), t(note.tick)},
                                        {QStringLiteral("duration"), t(note.duration)}, {QStringLiteral("pitch"), note.pitch},
                                        {QStringLiteral("channel"), note.channel}, {QStringLiteral("velocity"), int(note.velocity)}});
        for (const MidiControlEvent &event : pattern.events) {
            QJsonObject entry{{QStringLiteral("id"), idText(event.id)}, {QStringLiteral("tick"), t(event.tick)},
                              {QStringLiteral("controller"), controllerId(event.kind, event.number)},
                              {QStringLiteral("channel"), event.channel}, {QStringLiteral("value"), double(event.value)}};
            if (!event.noteId.isNull()) entry.insert(QStringLiteral("noteId"), idText(event.noteId));
            events.push_back(entry);
        }
        for (const MidiStepRow &row : pattern.rows) {
            QJsonArray steps;
            for (const MidiStep &step : row.steps)
                steps.push_back(QJsonObject{{QStringLiteral("id"), idText(step.id)}, {QStringLiteral("enabled"), step.enabled},
                                            {QStringLiteral("velocity"), int(step.velocity)}, {QStringLiteral("gate"), step.gate},
                                            {QStringLiteral("probability"), step.probability}});
            rows.push_back(QJsonObject{{QStringLiteral("id"), idText(row.id)}, {QStringLiteral("name"), row.name},
                                       {QStringLiteral("pitch"), row.pitch}, {QStringLiteral("channel"), row.channel},
                                       {QStringLiteral("stepTicks"), t(row.stepTicks)}, {QStringLiteral("steps"), steps}});
        }
        patterns.push_back(QJsonObject{{QStringLiteral("id"), idText(pattern.id)}, {QStringLiteral("name"), pattern.name},
                                       {QStringLiteral("length"), t(pattern.length)}, {QStringLiteral("seed"), double(pattern.seed)},
                                       {QStringLiteral("notes"), notes}, {QStringLiteral("events"), events},
                                       {QStringLiteral("rows"), rows}});
    }
    QJsonArray scenes, slotArray;
    for (const MidiScene &scene : content.scenes)
        scenes.push_back(QJsonObject{{QStringLiteral("id"), idText(scene.id)}, {QStringLiteral("name"), scene.name}});
    for (const MidiSlot &slot : content.launchSlots)
        slotArray.push_back(QJsonObject{{QStringLiteral("id"), idText(slot.id)}, {QStringLiteral("trackId"), idText(slot.trackId)},
                                    {QStringLiteral("sceneId"), idText(slot.sceneId)},
                                    {QStringLiteral("patternId"), idText(slot.patternId)}, {QStringLiteral("label"), slot.label}});
    return {{QStringLiteral("schema"), QStringLiteral("qindatk.midi/1")}, {QStringLiteral("ticksPerQuarter"), ticksPerQuarter},
            {QStringLiteral("quantization"), quantizeName(content.quantize)}, {QStringLiteral("patterns"), patterns},
            {QStringLiteral("scenes"), scenes}, {QStringLiteral("slots"), slotArray}};
}

std::optional<MidiContent> fromJson(const QJsonObject &object, int ticksPerQuarter, QString *error)
{
    const auto fail = [error](const QString &message) -> std::optional<MidiContent> {
        if (error) *error = message;
        return std::nullopt;
    };
    MidiContent content;
    if (object.isEmpty()) return content; // a session saved before WP-064
    if (object.value(QStringLiteral("schema")).toString() != QLatin1String("qindatk.midi/1"))
        return fail(tr("Unsupported MIDI content schema"));
    if (integer(object.value(QStringLiteral("ticksPerQuarter")), 1, 1 << 20) != ticksPerQuarter)
        return fail(tr("The MIDI content uses another PPQ than the session; it is never rescaled"));
    const auto quantize = parseQuantize(object.value(QStringLiteral("quantization")).toString());
    if (!quantize) return fail(tr("Unknown launch quantization"));
    content.quantize = *quantize;
    const QString bad = tr("A MIDI record is malformed");
    for (const QJsonValue &value : object.value(QStringLiteral("patterns")).toArray()) {
        const QJsonObject entry = value.toObject();
        MidiPattern pattern;
        pattern.id = uuid(entry.value(QStringLiteral("id")));
        pattern.name = entry.value(QStringLiteral("name")).toString();
        const auto length = ticks(entry.value(QStringLiteral("length")));
        const auto seed = integer(entry.value(QStringLiteral("seed")), 0, 0xFFFFFFFFll);
        if (!length || !seed) return fail(bad);
        pattern.length = *length;
        pattern.seed = quint32(*seed);
        for (const QJsonValue &n : entry.value(QStringLiteral("notes")).toArray()) {
            const QJsonObject o = n.toObject();
            MidiNote note;
            note.id = uuid(o.value(QStringLiteral("id")));
            const auto tick = ticks(o.value(QStringLiteral("tick")));
            const auto duration = ticks(o.value(QStringLiteral("duration")));
            const auto pitch = integer(o.value(QStringLiteral("pitch")), 0, 127);
            const auto channel = integer(o.value(QStringLiteral("channel")), 0, 15);
            const auto velocity = integer(o.value(QStringLiteral("velocity")), 1, 65535);
            if (!tick || !duration || !pitch || !channel || !velocity) return fail(bad);
            note.tick = *tick;
            note.duration = *duration;
            note.pitch = int(*pitch);
            note.channel = int(*channel);
            note.velocity = quint16(*velocity);
            pattern.notes.push_back(note);
        }
        for (const QJsonValue &e : entry.value(QStringLiteral("events")).toArray()) {
            const QJsonObject o = e.toObject();
            MidiControlEvent event;
            event.id = uuid(o.value(QStringLiteral("id")));
            const auto tick = ticks(o.value(QStringLiteral("tick")));
            const auto controller = parseController(o.value(QStringLiteral("controller")).toString());
            const auto channel = integer(o.value(QStringLiteral("channel")), 0, 15);
            const auto number = integer(o.value(QStringLiteral("value")), 0, 0xFFFFFFFFll);
            if (!tick || !controller || !channel || !number) return fail(bad);
            event.tick = *tick;
            event.kind = controller->first;
            event.number = controller->second;
            event.channel = int(*channel);
            event.value = quint32(*number);
            event.noteId = uuid(o.value(QStringLiteral("noteId")));
            pattern.events.push_back(event);
        }
        for (const QJsonValue &r : entry.value(QStringLiteral("rows")).toArray()) {
            const QJsonObject o = r.toObject();
            MidiStepRow row;
            row.id = uuid(o.value(QStringLiteral("id")));
            row.name = o.value(QStringLiteral("name")).toString();
            const auto pitch = integer(o.value(QStringLiteral("pitch")), 0, 127);
            const auto channel = integer(o.value(QStringLiteral("channel")), 0, 15);
            const auto stepTicks = ticks(o.value(QStringLiteral("stepTicks")));
            if (!pitch || !channel || !stepTicks) return fail(bad);
            row.pitch = int(*pitch);
            row.channel = int(*channel);
            row.stepTicks = *stepTicks;
            for (const QJsonValue &s : o.value(QStringLiteral("steps")).toArray()) {
                const QJsonObject so = s.toObject();
                MidiStep step;
                step.id = uuid(so.value(QStringLiteral("id")));
                step.enabled = so.value(QStringLiteral("enabled")).toBool();
                const auto velocity = integer(so.value(QStringLiteral("velocity")), 1, 65535);
                if (!velocity || !so.value(QStringLiteral("gate")).isDouble()
                    || !so.value(QStringLiteral("probability")).isDouble())
                    return fail(bad);
                step.velocity = quint16(*velocity);
                step.gate = so.value(QStringLiteral("gate")).toDouble();
                step.probability = so.value(QStringLiteral("probability")).toDouble();
                row.steps.push_back(step);
            }
            pattern.rows.push_back(row);
        }
        sortPattern(pattern);
        content.patterns.push_back(pattern);
    }
    for (const QJsonValue &value : object.value(QStringLiteral("scenes")).toArray())
        content.scenes.push_back({uuid(value.toObject().value(QStringLiteral("id"))),
                                  value.toObject().value(QStringLiteral("name")).toString()});
    for (const QJsonValue &value : object.value(QStringLiteral("slots")).toArray()) {
        const QJsonObject o = value.toObject();
        content.launchSlots.push_back({uuid(o.value(QStringLiteral("id"))), uuid(o.value(QStringLiteral("trackId"))),
                                 uuid(o.value(QStringLiteral("sceneId"))), uuid(o.value(QStringLiteral("patternId"))),
                                 o.value(QStringLiteral("label")).toString()});
    }
    if (const QString problem = validate(content); !problem.isEmpty()) return fail(problem);
    return content;
}

} // namespace Midi
} // namespace QindaTK
