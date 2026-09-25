// SPDX-License-Identifier: LGPL-3.0-or-later
// Score value types: note values, spelled pitches, keys, measures and lookup
// (WP-067, docs/notation.md). Rhythm editing is score_rhythm.cpp; validation
// and the native codec are score_codec.cpp.
#include "score_types.h"

#include <QRegularExpression>
#include <algorithm>

namespace QindaTK::Score {

namespace {
constexpr int kSemitones[7] = {0, 2, 4, 5, 7, 9, 11};
constexpr int kSharpOrder[7] = {3, 0, 4, 1, 5, 2, 6}; // F C G D A E B
constexpr int kFlatOrder[7] = {6, 2, 5, 1, 4, 0, 3};  // B E A D G C F
const char kSteps[] = "CDEFGAB";
} // namespace

QString idText(const QUuid &id)
{
    return id.isNull() ? QString() : id.toString(QUuid::WithoutBraces);
}

QString clefName(ScoreClef clef)
{
    switch (clef) {
    case ScoreClef::Bass: return QStringLiteral("bass");
    case ScoreClef::Alto: return QStringLiteral("alto");
    case ScoreClef::Treble: break;
    }
    return QStringLiteral("treble");
}

std::optional<ScoreClef> parseClef(const QString &name)
{
    if (name == QLatin1String("treble")) return ScoreClef::Treble;
    if (name == QLatin1String("bass")) return ScoreClef::Bass;
    if (name == QLatin1String("alto")) return ScoreClef::Alto;
    return std::nullopt;
}

QString breakName(ScoreBreak value)
{
    return value == ScoreBreak::System ? QStringLiteral("system")
           : value == ScoreBreak::Page ? QStringLiteral("page") : QStringLiteral("none");
}

std::optional<ScoreBreak> parseBreak(const QString &name)
{
    if (name.isEmpty() || name == QLatin1String("none")) return ScoreBreak::None;
    if (name == QLatin1String("system")) return ScoreBreak::System;
    if (name == QLatin1String("page")) return ScoreBreak::Page;
    return std::nullopt;
}

const QStringList &articulationNames()
{
    static const QStringList names{QStringLiteral("staccato"), QStringLiteral("staccatissimo"),
                                   QStringLiteral("tenuto"), QStringLiteral("accent"),
                                   QStringLiteral("marcato"), QStringLiteral("fermata")};
    return names;
}

const QStringList &dynamicNames()
{
    static const QStringList names{QStringLiteral("ppp"), QStringLiteral("pp"), QStringLiteral("p"),
                                   QStringLiteral("mp"), QStringLiteral("mf"), QStringLiteral("f"),
                                   QStringLiteral("ff"), QStringLiteral("fff")};
    return names;
}

QStringList canonicalArticulations(const QStringList &names)
{
    QStringList result;
    for (const QString &known : articulationNames())
        if (names.contains(known)) result.push_back(known);
    return result;
}

// ---- durations ----

qint64 baseTicks(int base)
{
    return base < 0 || base > 6 ? 0 : kWholeTicks >> base;
}

ScoreDuration normalized(ScoreDuration duration)
{
    if (duration.actual <= 1 || duration.normal <= 0 || duration.actual == duration.normal) {
        duration.actual = 1;
        duration.normal = 1;
        duration.unit = duration.base;
    }
    return duration;
}

std::optional<qint64> ticks(const ScoreDuration &duration)
{
    const qint64 base = baseTicks(duration.base);
    if (base == 0 || duration.dots < 0 || duration.dots > kMaxDots) return std::nullopt;
    if (duration.actual < 1 || duration.actual > 32 || duration.normal < 1 || duration.normal > 32)
        return std::nullopt;
    if (duration.actual > 1 && baseTicks(duration.unit) == 0) return std::nullopt;
    qint64 total = base;
    qint64 add = base;
    for (int d = 0; d < duration.dots; ++d) {
        if (add % 2) return std::nullopt;
        add /= 2;
        total += add;
    }
    if ((total * duration.normal) % duration.actual) return std::nullopt;
    return total * duration.normal / duration.actual;
}

QString baseName(int base)
{
    static const char *names[] = {"whole", "half", "quarter", "eighth", "16th", "32nd", "64th"};
    return base < 0 || base > 6 ? QString() : QString::fromLatin1(names[base]);
}

std::optional<int> parseBaseName(const QString &name)
{
    for (int base = 0; base <= 6; ++base)
        if (baseName(base) == name) return base;
    return std::nullopt;
}

QString durationId(const ScoreDuration &value)
{
    const ScoreDuration duration = normalized(value);
    static const char *dots[] = {"", "dotted-", "double-dotted-", "triple-dotted-"};
    QString text = QString::fromLatin1(dots[std::clamp(duration.dots, 0, kMaxDots)]);
    if (duration.actual > 1) {
        if (duration.actual == 3 && duration.normal == 2 && duration.unit == duration.base)
            text += QStringLiteral("triplet-");
        else if (duration.unit == duration.base)
            text += QStringLiteral("tuplet-%1:%2-").arg(duration.actual).arg(duration.normal);
        else
            text += QStringLiteral("tuplet-%1:%2:%3-").arg(duration.actual).arg(duration.normal)
                        .arg(baseName(duration.unit));
    }
    return text + baseName(duration.base);
}

std::optional<ScoreDuration> parseDuration(const QString &id)
{
    static const QRegularExpression pattern(QStringLiteral(
        "^(dotted-|double-dotted-|triple-dotted-)?(triplet-|tuplet-(\\d+):(\\d+)(?::(\\w+))?-)?(\\w+)$"));
    const auto match = pattern.match(id);
    if (!match.hasMatch()) return std::nullopt;
    ScoreDuration duration;
    const QString dots = match.captured(1);
    duration.dots = dots.isEmpty() ? 0 : dots.startsWith(QLatin1String("dotted")) ? 1
                    : dots.startsWith(QLatin1String("double")) ? 2 : 3;
    const auto base = parseBaseName(match.captured(6));
    if (!base) return std::nullopt;
    duration.base = *base;
    duration.unit = *base;
    if (match.captured(2) == QLatin1String("triplet-")) {
        duration.actual = 3;
        duration.normal = 2;
    } else if (!match.captured(2).isEmpty()) {
        duration.actual = match.captured(3).toInt();
        duration.normal = match.captured(4).toInt();
        if (!match.captured(5).isEmpty()) {
            const auto unit = parseBaseName(match.captured(5));
            if (!unit) return std::nullopt;
            duration.unit = *unit;
        }
        if (duration.actual < 2 || duration.normal < 1 || duration.actual == duration.normal) return std::nullopt;
    }
    duration = normalized(duration);
    if (!ticks(duration)) return std::nullopt;
    return duration;
}

// ---- pitch ----

int midiPitch(const ScorePitch &pitch)
{
    return (pitch.octave + 1) * 12 + kSemitones[std::clamp(pitch.step, 0, 6)] + pitch.alter;
}

int diatonic(const ScorePitch &pitch)
{
    return pitch.octave * 7 + pitch.step;
}

QString pitchName(const ScorePitch &pitch)
{
    QString text(QChar::fromLatin1(kSteps[std::clamp(pitch.step, 0, 6)]));
    if (pitch.alter > 0) text += QString(pitch.alter, QLatin1Char('#'));
    if (pitch.alter < 0) text += QString(-pitch.alter, QLatin1Char('b'));
    return text + QString::number(pitch.octave);
}

std::optional<ScorePitch> parsePitch(const QString &name)
{
    static const QRegularExpression pattern(QStringLiteral("^([A-Ga-g])(##|#|x|bb|b)?(-?\\d)$"));
    const auto match = pattern.match(name.trimmed());
    if (!match.hasMatch()) return std::nullopt;
    ScorePitch pitch;
    pitch.step = int(QByteArray(kSteps).indexOf(char(match.captured(1).toUpper().at(0).toLatin1())));
    const QString accidental = match.captured(2);
    pitch.alter = accidental == QLatin1String("##") || accidental == QLatin1String("x") ? 2
                  : accidental == QLatin1String("#") ? 1 : accidental == QLatin1String("bb") ? -2
                  : accidental == QLatin1String("b") ? -1 : 0;
    pitch.octave = match.captured(3).toInt();
    const int midi = midiPitch(pitch);
    if (midi < 0 || midi > 127) return std::nullopt;
    return pitch;
}

int keyAlter(int fifths, int step)
{
    for (int i = 0; i < std::min(std::abs(fifths), 7); ++i)
        if ((fifths > 0 ? kSharpOrder[i] : kFlatOrder[i]) == step) return fifths > 0 ? 1 : -1;
    return 0;
}

static ScorePitch spelled(int midi, int step, int alter)
{
    ScorePitch pitch;
    pitch.step = step;
    pitch.alter = alter;
    pitch.octave = (midi - alter - kSemitones[step]) / 12 - 1;
    return pitch;
}

ScorePitch spell(int midi, int fifths)
{
    const int pc = ((midi % 12) + 12) % 12;
    for (int step = 0; step < 7; ++step) { // a tone of the key first
        const int alter = keyAlter(fifths, step);
        if (((kSemitones[step] + alter) % 12 + 12) % 12 == pc) return spelled(midi, step, alter);
    }
    for (int step = 0; step < 7; ++step)
        if (kSemitones[step] == pc) return spelled(midi, step, 0);
    for (int step = 0; step < 7; ++step) {
        const int alter = fifths >= 0 ? 1 : -1;
        if (((kSemitones[step] + alter) % 12 + 12) % 12 == pc) return spelled(midi, step, alter);
    }
    return spelled(midi, 0, 0);
}

QVector<ScorePitch> enharmonics(const ScorePitch &pitch)
{
    const int midi = midiPitch(pitch);
    QVector<ScorePitch> result;
    for (int octave = pitch.octave - 1; octave <= pitch.octave + 1; ++octave)
        for (int step = 0; step < 7; ++step) {
            const int alter = midi - (octave + 1) * 12 - kSemitones[step];
            if (alter >= -2 && alter <= 2) result.push_back({step, alter, octave});
        }
    std::sort(result.begin(), result.end(),
              [](const ScorePitch &a, const ScorePitch &b) { return diatonic(a) < diatonic(b); });
    return result;
}

int clefBottomLine(ScoreClef clef)
{
    switch (clef) {
    case ScoreClef::Bass: return 2 * 7 + 4; // G2
    case ScoreClef::Alto: return 3 * 7 + 3; // F3
    case ScoreClef::Treble: break;
    }
    return 4 * 7 + 2; // E4
}

int clefKeyOffset(ScoreClef clef)
{
    return clef == ScoreClef::Bass ? -2 : clef == ScoreClef::Alto ? -1 : 0;
}

// ---- measures ----

qint64 meterTicks(const ScoreMeasure &measure)
{
    return measure.denominator <= 0 ? 0 : qint64(measure.numerator) * kWholeTicks / measure.denominator;
}

qint64 measureTicks(const ScoreMeasure &measure)
{
    return measure.pickup > 0 ? measure.pickup : meterTicks(measure);
}

qint64 measurePhase(const ScoreMeasure &measure)
{
    return measure.pickup > 0 ? meterTicks(measure) - measure.pickup : 0;
}

QVector<qint64> measureStarts(const ScoreContent &content)
{
    QVector<qint64> starts;
    starts.reserve(content.measures.size() + 1);
    qint64 at = 0;
    starts.push_back(at);
    for (const ScoreMeasure &measure : content.measures) {
        at += measureTicks(measure);
        starts.push_back(at);
    }
    return starts;
}

int measureIndexAt(const QVector<qint64> &starts, qint64 tick)
{
    if (starts.size() < 2 || tick < starts.front() || tick >= starts.back()) return -1;
    const auto it = std::upper_bound(starts.begin(), starts.end(), tick);
    return int(it - starts.begin()) - 1;
}

qint64 totalTicks(const ScoreContent &content)
{
    qint64 total = 0;
    for (const ScoreMeasure &measure : content.measures) total += measureTicks(measure);
    return total;
}

// ---- lookup ----

EventRef findEvent(const ScoreContent &content, const QUuid &eventId)
{
    for (int p = 0; p < content.parts.size(); ++p)
        for (int s = 0; s < content.parts[p].staves.size(); ++s)
            for (int v = 0; v < content.parts[p].staves[s].voices.size(); ++v) {
                const auto &events = content.parts[p].staves[s].voices[v].events;
                for (int e = 0; e < events.size(); ++e)
                    if (events[e].id == eventId) return {p, s, v, e};
            }
    return {};
}

EventRef findNote(const ScoreContent &content, const QUuid &noteId, int *noteIndex)
{
    for (int p = 0; p < content.parts.size(); ++p)
        for (int s = 0; s < content.parts[p].staves.size(); ++s)
            for (int v = 0; v < content.parts[p].staves[s].voices.size(); ++v) {
                const auto &events = content.parts[p].staves[s].voices[v].events;
                for (int e = 0; e < events.size(); ++e)
                    for (int n = 0; n < events[e].notes.size(); ++n)
                        if (events[e].notes[n].id == noteId) {
                            if (noteIndex) *noteIndex = n;
                            return {p, s, v, e};
                        }
            }
    return {};
}

const ScoreStaff *findStaff(const ScoreContent &content, const QUuid &staffId, int *partIndex, int *staffIndex)
{
    for (int p = 0; p < content.parts.size(); ++p)
        for (int s = 0; s < content.parts[p].staves.size(); ++s)
            if (content.parts[p].staves[s].id == staffId) {
                if (partIndex) *partIndex = p;
                if (staffIndex) *staffIndex = s;
                return &content.parts[p].staves[s];
            }
    return nullptr;
}

int measureIndex(const ScoreContent &content, const QUuid &measureId)
{
    for (int m = 0; m < content.measures.size(); ++m)
        if (content.measures[m].id == measureId) return m;
    return -1;
}

int tieTarget(const ScoreNote &note, const ScoreEvent &next)
{
    if (!note.tie || next.rest) return -1;
    const int midi = midiPitch(note.pitch);
    for (int n = 0; n < next.notes.size(); ++n)
        if (midiPitch(next.notes[n].pitch) == midi) return n;
    return -1;
}

} // namespace QindaTK::Score
