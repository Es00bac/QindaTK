// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QJsonObject>
#include <QString>
#include <QUuid>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>
#include <optional>
#include <utility>

namespace QindaTK {

// AGENT-CONTRACT (D-150): the MIDI data of an AudioSession (WP-064,
// docs/audio-midi.md). Every position is an exact musical tick at the session
// PPQ (TimeMap::ticksPerQuarter), relative to the pattern start; tempo turns
// ticks into samples only when the engine graph is compiled or a clip is
// launched. Velocity is 16-bit and controller values are 32-bit (the MIDI 2.0
// resolutions); 7- and 14-bit MIDI 1.0 values scale with Midi::scaleUp /
// scaleDown, so a MIDI 1.0 value survives any number of round trips.
enum class MidiController { ControlChange, PitchBend, ChannelPressure, NoteBend, NotePressure };
enum class MidiQuantize { None, Beat, Bar };

struct MidiNote {
    QUuid id;
    qint64 tick = 0;            // 0 <= tick < pattern length
    qint64 duration = 1;        // > 0; a note may sound past the pattern end
    int pitch = 60;             // 0..127
    int channel = 0;            // 0..15
    quint16 velocity = 0x8000;  // 1..65535
    qint64 end() const { return tick + duration; }
    friend bool operator==(const MidiNote &, const MidiNote &) = default;
};

struct MidiControlEvent {
    QUuid id;
    qint64 tick = 0;            // 0 <= tick < pattern length
    MidiController kind = MidiController::ControlChange;
    int number = 0;             // ControlChange: controller 0..127
    int channel = 0;            // 0..15
    quint32 value = 0;          // full 32-bit range
    QUuid noteId;               // NoteBend/NotePressure: the note it belongs to
    friend bool operator==(const MidiControlEvent &, const MidiControlEvent &) = default;
};

struct MidiStep {
    QUuid id;
    bool enabled = false;
    quint16 velocity = 0x8000;  // 1..65535
    double gate = 0.5;          // (0, 1] of the step
    double probability = 1.0;   // [0, 1]
    friend bool operator==(const MidiStep &, const MidiStep &) = default;
};

// A step row loops on its own length (steps × stepTicks), so rows of
// different lengths form a polymetric pattern.
struct MidiStepRow {
    QUuid id;
    QString name;
    int pitch = 36;
    int channel = 9;
    qint64 stepTicks = 240;
    QVector<MidiStep> steps;
    friend bool operator==(const MidiStepRow &, const MidiStepRow &) = default;
};

struct MidiPattern {
    QUuid id;
    QString name;
    qint64 length = 3840;       // notes and events loop on this length
    quint32 seed = 0;           // step probability seed (D-151)
    QVector<MidiNote> notes;    // sorted: tick, pitch, channel, id
    QVector<MidiControlEvent> events; // sorted: tick, kind, number, id
    QVector<MidiStepRow> rows;
    friend bool operator==(const MidiPattern &, const MidiPattern &) = default;
};

struct MidiScene {
    QUuid id;
    QString name;
    friend bool operator==(const MidiScene &, const MidiScene &) = default;
};

// A launcher slot: one pattern on one MIDI track in one scene (D-153).
struct MidiSlot {
    QUuid id;
    QUuid trackId;
    QUuid sceneId;
    QUuid patternId;
    QString label;
    friend bool operator==(const MidiSlot &, const MidiSlot &) = default;
};

struct MidiContent {
    QVector<MidiPattern> patterns;
    QVector<MidiScene> scenes;
    QVector<MidiSlot> launchSlots; // not "slots": a Qt keyword macro
    MidiQuantize quantize = MidiQuantize::Bar;
    friend bool operator==(const MidiContent &, const MidiContent &) = default;
};

namespace Midi {

constexpr int kMaxPatterns = 1024;
constexpr int kMaxNotes = 65536;    // per pattern
constexpr int kMaxEvents = 65536;   // per pattern
constexpr int kMaxRows = 64;
constexpr int kMaxSteps = 64;       // per row
constexpr int kMaxScenes = 256;
constexpr int kMaxSlots = 4096;
constexpr qint64 kMaxLength = qint64(1) << 40; // ticks

// MIDI 2.0 min-center-max scaling: the centre maps to the centre and the
// maximum to the maximum; scaleDown(scaleUp(v)) == v.
quint32 scaleUp(quint32 value, int fromBits, int toBits);
quint32 scaleDown(quint32 value, int fromBits, int toBits);
quint16 velocityFrom7(int velocity7);
int velocityTo7(quint16 velocity);  // 1..127 for any note velocity
quint16 velocityFromUnit(double unit); // 0..1 → 1..65535 (0 is not a note velocity)
double velocityToUnit(quint16 velocity);

int resolutionBits(MidiController kind);
bool perNote(MidiController kind);
quint32 defaultValue(MidiController kind);
QString controllerId(MidiController kind, int number = 0);
std::optional<std::pair<MidiController, int>> parseController(const QString &id);
// Normalized 0..1 at the controller's resolution <-> the stored 32-bit value.
quint32 valueFromUnit(MidiController kind, double unit);
double valueToUnit(MidiController kind, quint32 value);
QVariantMap descriptor(const QString &controllerId); // empty when unknown
QVariantList descriptors();

QString quantizeName(MidiQuantize quantize);
std::optional<MidiQuantize> parseQuantize(const QString &name);

QString idText(const QUuid &id);
QString patternResourceId(const QUuid &patternId);   // "pattern:<uuid>"
QUuid patternFromResource(const QString &resourceId); // null when not a pattern
// Stable 64-bit key of an ID for the device thread; never 0.
quint64 key(const QUuid &id);

void sortPattern(MidiPattern &pattern);
QString validate(const MidiPattern &pattern); // empty when valid
QString validate(const MidiContent &content);
const MidiPattern *find(const MidiContent &content, const QUuid &patternId);

QJsonObject toJson(const MidiContent &content, int ticksPerQuarter);
std::optional<MidiContent> fromJson(const QJsonObject &object, int ticksPerQuarter, QString *error);

} // namespace Midi
} // namespace QindaTK
