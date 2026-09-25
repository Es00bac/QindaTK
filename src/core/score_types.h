// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QUuid>
#include <QVariantMap>
#include <QVector>
#include <optional>

namespace QindaTK {

// AGENT-CONTRACT (D-178, docs/notation.md): the notation model of WP-067.
// A score is written music, not a performance: pitches are spelled (step,
// alteration, octave), durations are note values (base, dots, tuplet ratio)
// and every position is an exact score tick at Score::kPpq. It is separate
// from the MIDI pattern model (D-150); Score::toMidi (score_midi.cpp) is the
// only bridge and keeps spelling, voices, tuplets, articulations and ties in
// the score. Every object has a UUID that survives edits, the native file and
// MusicXML (processing instructions and `id` attributes).
enum class ScoreClef { Treble, Bass, Alto };
enum class ScoreBreak { None, System, Page };

struct ScorePitch {
    int step = 0;   // 0..6 = C D E F G A B
    int alter = 0;  // -2..2 semitones
    int octave = 4; // scientific pitch notation: C4 is middle C (MIDI 60)
    friend bool operator==(const ScorePitch &, const ScorePitch &) = default;
};

// A written note value. `base` is the power of two dividing a whole note
// (0 whole .. 6 sixty-fourth); a tuplet plays `actual` notes in the time of
// `normal` notes of `unit`. Non-tuplets are canonical: actual = normal = 1,
// unit = base (Score::normalized).
struct ScoreDuration {
    int base = 2;
    int dots = 0;
    int actual = 1;
    int normal = 1;
    int unit = 2;
    friend bool operator==(const ScoreDuration &, const ScoreDuration &) = default;
};

struct ScoreNote {
    QUuid id;
    ScorePitch pitch;
    bool tie = false; // tied to the same sounding pitch in the next event of its voice
    friend bool operator==(const ScoreNote &, const ScoreNote &) = default;
};

// A chord (one or more notes) or a rest in one voice.
struct ScoreEvent {
    QUuid id;
    qint64 tick = 0;           // absolute score tick
    ScoreDuration duration;
    bool rest = false;
    QVector<ScoreNote> notes;  // empty for a rest; written order
    QStringList articulations; // canonical order (Score::articulationNames)
    QString dynamic;           // "" or one of Score::dynamicNames
    friend bool operator==(const ScoreEvent &, const ScoreEvent &) = default;
};

struct ScoreVoice {
    QUuid id;
    int number = 1;             // 1..4 within its staff
    QVector<ScoreEvent> events; // sorted by tick, never overlapping
    friend bool operator==(const ScoreVoice &, const ScoreVoice &) = default;
};

struct ScoreStaff {
    QUuid id;
    ScoreClef clef = ScoreClef::Treble;
    QVector<ScoreVoice> voices; // sorted by number; voice 1 always exists
    friend bool operator==(const ScoreStaff &, const ScoreStaff &) = default;
};

struct ScorePart {
    QUuid id;
    QString name;
    QString abbreviation;
    int channel = 0; // MIDI channel 0..15 for playback
    QVector<ScoreStaff> staves;
    friend bool operator==(const ScorePart &, const ScorePart &) = default;
};

// One measure column of the whole score (every staff shares it).
struct ScoreMeasure {
    QUuid id;
    int numerator = 4;
    int denominator = 4;
    int fifths = 0; // key signature: -7 (7 flats) .. 7 (7 sharps)
    ScoreBreak breakAfter = ScoreBreak::None;
    qint64 pickup = 0; // > 0: a pickup (anacrusis) this many ticks long, shorter than the meter
    friend bool operator==(const ScoreMeasure &, const ScoreMeasure &) = default;
};

struct ScoreContent {
    QUuid id;
    QString title;
    QString composer;
    QVector<ScoreMeasure> measures;
    QVector<ScorePart> parts;
    friend bool operator==(const ScoreContent &, const ScoreContent &) = default;
};

// A tuplet group: events [first, last] of one voice, complete when their
// ticks sum to `normal` units.
struct ScoreTupletGroup {
    int first = 0;
    int last = 0;
    int actual = 3;
    int normal = 2;
    qint64 span = 0;
};

namespace Score {
constexpr int kPpq = 960;
constexpr qint64 kWholeTicks = 4 * kPpq;
constexpr int kMaxMeasures = 4096;
constexpr int kMaxParts = 64;
constexpr int kMaxStaves = 4;   // per part
constexpr int kMaxVoices = 4;   // per staff
constexpr int kMaxChord = 16;   // notes per event
constexpr int kMaxDots = 3;

QString idText(const QUuid &id);
QString clefName(ScoreClef clef);
std::optional<ScoreClef> parseClef(const QString &name);
QString breakName(ScoreBreak value);
std::optional<ScoreBreak> parseBreak(const QString &name);
const QStringList &articulationNames(); // staccato staccatissimo tenuto accent marcato fermata
const QStringList &dynamicNames();      // ppp pp p mp mf f ff fff
QStringList canonicalArticulations(const QStringList &names); // known names, canonical order, unique

// ---- durations ----
qint64 baseTicks(int base); // whole = 3840 .. sixty-fourth = 60
ScoreDuration normalized(ScoreDuration duration);
// Exact ticks, or nothing when the value is not a whole number of ticks.
std::optional<qint64> ticks(const ScoreDuration &duration);
QString baseName(int base); // whole half quarter eighth 16th 32nd 64th
std::optional<int> parseBaseName(const QString &name);
// "quarter", "dotted-half", "double-dotted-eighth", "triplet-eighth",
// "dotted-triplet-quarter", "tuplet-5:4-16th" (unit = base).
QString durationId(const ScoreDuration &duration);
std::optional<ScoreDuration> parseDuration(const QString &id);

// ---- pitch ----
int midiPitch(const ScorePitch &pitch);
int diatonic(const ScorePitch &pitch); // octave * 7 + step
QString pitchName(const ScorePitch &pitch); // "C4", "F#5", "Bb3", "C##4", "Dbb2"
std::optional<ScorePitch> parsePitch(const QString &name);
int keyAlter(int fifths, int step);     // the alteration the key gives a step
ScorePitch spell(int midi, int fifths); // the key's spelling of a MIDI pitch
QVector<ScorePitch> enharmonics(const ScorePitch &pitch); // same MIDI pitch, |alter| <= 2, by step
int clefBottomLine(ScoreClef clef);     // diatonic number of the bottom staff line
int clefKeyOffset(ScoreClef clef);      // staff-step offset of key signatures vs treble

// ---- measures ----
qint64 meterTicks(const ScoreMeasure &measure);  // the full bar of its time signature
qint64 measureTicks(const ScoreMeasure &measure); // its length: the pickup when set
qint64 measurePhase(const ScoreMeasure &measure); // ticks of the meter before a pickup starts
QVector<qint64> measureStarts(const ScoreContent &content); // size = measures + 1
int measureIndexAt(const QVector<qint64> &starts, qint64 tick); // -1 outside
qint64 totalTicks(const ScoreContent &content);

// ---- lookup ----
struct EventRef { int part = -1, staff = -1, voice = -1, event = -1; bool valid() const { return event >= 0; } };
EventRef findEvent(const ScoreContent &content, const QUuid &eventId);
EventRef findNote(const ScoreContent &content, const QUuid &noteId, int *noteIndex);
const ScoreStaff *findStaff(const ScoreContent &content, const QUuid &staffId, int *partIndex = nullptr,
                            int *staffIndex = nullptr);
int measureIndex(const ScoreContent &content, const QUuid &measureId);
// The note in `next` a tie from `note` continues into, or -1.
int tieTarget(const ScoreNote &note, const ScoreEvent &next);

// ---- rhythm (score_rhythm.cpp) ----
// Tuplet groups of a sorted voice; an empty `error` when every group closes.
QVector<ScoreTupletGroup> tupletGroups(const QVector<ScoreEvent> &events, QString *error);
// Rest values that exactly fill `length` ticks starting `offset` ticks into a
// measure (largest aligned value first); nothing when no rest can fill it.
std::optional<QVector<ScoreDuration>> restFill(qint64 offset, qint64 length);
// Writes `event` into `voice` over [tick, tick + duration): overlapped events
// (whole tuplet groups) are removed and the uncovered remainder becomes rests.
// Returns an error text and leaves the voice unchanged when it cannot.
QString writeEvent(ScoreVoice &voice, ScoreEvent event, qint64 measureStart, qint64 measureEnd,
                   bool fillWholeMeasure, qint64 phase = 0);
// Clears ties that no longer end on a note of the same pitch (after an edit
// replaced or removed the event they led into).
void dropBrokenTies(ScoreContent &content);
// Fills voice 1 of every staff with rests where it has gaps.
QString completeVoices(ScoreContent &content);
// Per measure, staff and voice: {measureId, staffId, voiceId, number, filled,
// length, status: complete | partial | empty}.
QVariantList rhythmReport(const ScoreContent &content);

// ---- validation and native codec (score_codec.cpp) ----
QString validate(const ScoreContent &content); // empty when valid
ScoreContent newScore(const QString &title, int measures, int numerator, int denominator, int fifths);
ScorePart newPart(const QString &name, const QList<ScoreClef> &clefs, int channel);
QJsonObject toJson(const ScoreContent &content);
std::optional<ScoreContent> fromJson(const QJsonObject &object, QString *error);
QVariantMap eventMap(const ScoreEvent &event);
} // namespace Score

} // namespace QindaTK
