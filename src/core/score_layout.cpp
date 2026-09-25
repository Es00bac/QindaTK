// SPDX-License-Identifier: LGPL-3.0-or-later
// Score engraving, part 1 (WP-067, D-179, docs/notation.md#layout): measure
// columns and proportional spacing, system and page breaking, justification
// and the placement of every staff, measure and event. Note-level engraving
// (accidentals, stems, beams, ties, tuplets) is score_layout_notes.cpp.
#include "score_layout.h"

#include <QObject>
#include <algorithm>
#include <cmath>
#include <numeric>

namespace QindaTK::Score {

namespace {

// All widths in staff spaces until placement multiplies by the space.
constexpr double kIndent = 6.0;      // system indent for part labels and the bracket
constexpr double kClefWidth = 3.0;
constexpr double kTimeWidth = 2.5;
constexpr double kLeftPad = 1.0;
constexpr double kRightPad = 0.5;
constexpr double kAccidentalPad = 1.2;
constexpr double kDotPad = 0.5;
constexpr double kHeadOffset = 0.6;
constexpr double kStaffGap = 6.0;    // between staves of a system (below the staff)
constexpr double kSystemGap = 8.0;
constexpr double kSystemTopPad = 2.0;
constexpr double kTitleBlock = 6.0;

struct Row {
    const ScorePart *part = nullptr;
    const ScoreStaff *staff = nullptr;
    bool firstOfPart = false;
};

struct Metrics {
    QVector<qint64> columns;
    QVector<double> segments;   // scalable
    QVector<double> accidental; // fixed, before each column
    QVector<double> dots;       // fixed, after each column
    double fixedWidth() const
    {
        return kLeftPad + kRightPad + std::accumulate(accidental.begin(), accidental.end(), 0.0)
               + std::accumulate(dots.begin(), dots.end(), 0.0);
    }
    double scalable() const { return std::accumulate(segments.begin(), segments.end(), 0.0); }
};

double keyWidth(int glyphs)
{
    return glyphs == 0 ? 0.0 : glyphs * 0.9 + 0.6;
}

// Key glyphs shown at a measure: at a system start the whole key, at a
// change the new key, or naturals cancelling the old one when it becomes C.
QVector<ScoreGlyphLayout> keyGlyphs(int fifths, int previous, bool systemStart, ScoreClef clef)
{
    static constexpr int sharps[7] = {8, 5, 9, 6, 3, 7, 4};
    static constexpr int flats[7] = {4, 7, 3, 6, 2, 5, 1};
    QVector<ScoreGlyphLayout> glyphs;
    const int offset = clefKeyOffset(clef);
    if (!systemStart && fifths == previous) return glyphs;
    if (fifths == 0 && !systemStart) {
        for (int i = 0; i < std::abs(previous); ++i)
            glyphs.push_back({0, double((previous > 0 ? sharps[i] : flats[i]) + offset), QStringLiteral("natural")});
        return glyphs;
    }
    for (int i = 0; i < std::abs(fifths); ++i)
        glyphs.push_back({0, double((fifths > 0 ? sharps[i] : flats[i]) + offset),
                          fifths > 0 ? QStringLiteral("sharp") : QStringLiteral("flat")});
    return glyphs;
}

bool timeChanges(const ScoreContent &content, int m)
{
    return m == 0 || content.measures[m].numerator != content.measures[m - 1].numerator
           || content.measures[m].denominator != content.measures[m - 1].denominator;
}

int keyGlyphCount(const ScoreContent &content, int m, bool systemStart)
{
    const int fifths = content.measures[m].fifths;
    const int previous = m > 0 ? content.measures[m - 1].fifths : 0;
    return int(keyGlyphs(fifths, previous, systemStart, ScoreClef::Treble).size());
}

double headerWidth(const ScoreContent &content, int m, bool systemStart)
{
    return (systemStart ? kClefWidth : 0.0) + keyWidth(keyGlyphCount(content, m, systemStart))
           + (timeChanges(content, m) ? kTimeWidth : 0.0);
}

Metrics measureMetrics(const QVector<Row> &rows, const QVector<QHash<QUuid, QString>> &accidentals,
                       qint64 start, qint64 end)
{
    QVector<qint64> ticksAt{start};
    QHash<qint64, bool> hasAccidental;
    QHash<qint64, int> dots;
    for (int r = 0; r < rows.size(); ++r)
        for (const ScoreVoice &voice : rows[r].staff->voices)
            for (const ScoreEvent &event : voice.events) {
                if (event.tick < start || event.tick >= end) continue;
                ticksAt.push_back(event.tick);
                for (const ScoreNote &note : event.notes)
                    if (!accidentals[r].value(note.id).isEmpty()) hasAccidental[event.tick] = true;
                dots[event.tick] = std::max(dots.value(event.tick), event.duration.dots);
            }
    std::sort(ticksAt.begin(), ticksAt.end());
    ticksAt.erase(std::unique(ticksAt.begin(), ticksAt.end()), ticksAt.end());
    Metrics metrics;
    metrics.columns = ticksAt;
    for (int i = 0; i < ticksAt.size(); ++i) {
        const qint64 next = i + 1 < ticksAt.size() ? ticksAt[i + 1] : end;
        const double d = double(next - ticksAt[i]);
        metrics.segments.push_back(1.0 + 1.4 * std::log2(1.0 + d / 240.0));
        metrics.accidental.push_back(hasAccidental.value(ticksAt[i]) ? kAccidentalPad : 0.0);
        metrics.dots.push_back(kDotPad * dots.value(ticksAt[i]));
    }
    return metrics;
}

} // namespace

ScoreLayout layoutScore(const ScoreContent &content, const ScoreLayoutOptions &options)
{
    ScoreLayout layout;
    layout.pageSize = options.pageSize;
    layout.staffSpace = options.staffSpace;
    layout.title = content.title;
    layout.composer = content.composer;
    const double sp = options.staffSpace;
    QVector<Row> rows;
    for (const ScorePart &part : content.parts) {
        if (!options.parts.isEmpty() && !options.parts.contains(part.id)) continue;
        for (int s = 0; s < part.staves.size(); ++s) rows.push_back({&part, &part.staves[s], s == 0});
    }
    if (rows.isEmpty() || content.measures.isEmpty() || sp <= 0) {
        layout.pages.push_back({});
        return layout;
    }
    const QSet<QUuid> tiedInto = tieContinuations(content);
    QVector<QHash<QUuid, QString>> accidentalMaps;
    for (const Row &row : rows) accidentalMaps.push_back(accidentals(content, *row.staff, tiedInto));
    const QVector<qint64> starts = measureStarts(content);
    const int measureCount = int(content.measures.size());
    QVector<Metrics> metrics;
    for (int m = 0; m < measureCount; ++m) metrics.push_back(measureMetrics(rows, accidentalMaps, starts[m], starts[m + 1]));

    // ---- line breaking: greedy, honouring forced breaks ----
    const double available = (options.pageSize.width() - 2 * options.margin) / sp - kIndent;
    const auto natural = [&](int m, bool systemStart) {
        return headerWidth(content, m, systemStart) + metrics[m].fixedWidth() + metrics[m].scalable();
    };
    QVector<std::pair<int, int>> systems;
    for (int m = 0; m < measureCount;) {
        const int first = m;
        double width = natural(m, true);
        ++m;
        while (m < measureCount && content.measures[m - 1].breakAfter == ScoreBreak::None) {
            const double more = natural(m, false);
            if (width + more > available) break;
            width += more;
            ++m;
        }
        systems.push_back({first, m - 1});
    }

    // ---- pages ----
    const double systemHeight = (rows.size() * 4.0 + (rows.size() - 1) * kStaffGap) * sp;
    const double bottomLimit = options.pageSize.height() - options.margin;
    double y = options.margin;
    if (!content.title.isEmpty()) {
        layout.titleY = options.margin + 2.0 * sp;
        y += kTitleBlock * sp;
    }
    layout.pages.push_back({0, {}});
    for (int s = 0; s < systems.size(); ++s) {
        const auto [first, last] = systems[s];
        const bool forced = s > 0 && content.measures[systems[s - 1].second].breakAfter == ScoreBreak::Page;
        const double top = y + kSystemTopPad * sp;
        if (!layout.pages.back().systems.isEmpty() && (forced || top + systemHeight > bottomLimit)) {
            layout.pages.push_back({int(layout.pages.size()), {}});
            y = options.margin;
        }
        ScoreSystemLayout system;
        system.index = s;
        system.firstMeasure = first;
        system.lastMeasure = last;
        system.top = y + kSystemTopPad * sp;
        system.bottom = system.top + systemHeight;
        y = system.bottom + kSystemGap * sp;

        // ---- justification ----
        double fixed = 0, scalable = 0;
        for (int m = first; m <= last; ++m) {
            fixed += headerWidth(content, m, m == first) + metrics[m].fixedWidth();
            scalable += metrics[m].scalable();
        }
        const bool lastSystem = s + 1 == systems.size();
        double factor = scalable > 0 ? (available - fixed) / scalable : 1.0;
        if (lastSystem && fixed + scalable < available * 0.75) factor = 1.0;
        factor = std::max(factor, 0.2);

        const double left = options.margin + kIndent * sp;
        for (int r = 0; r < rows.size(); ++r) {
            const Row &row = rows[r];
            ScoreStaffLayout staff;
            staff.id = row.staff->id;
            staff.partId = row.part->id;
            staff.label = !row.firstOfPart ? QString() : s == 0 ? row.part->name : row.part->abbreviation;
            staff.clef = row.staff->clef;
            staff.top = system.top + r * (4.0 + kStaffGap) * sp;
            staff.x0 = left;
            double x = left;
            for (int m = first; m <= last; ++m) {
                const ScoreMeasure &record = content.measures[m];
                ScoreMeasureLayout measure;
                measure.id = record.id;
                measure.index = m;
                measure.start = starts[m];
                measure.end = starts[m + 1];
                measure.numerator = record.numerator;
                measure.denominator = record.denominator;
                measure.phase = measurePhase(record);
                measure.x0 = x;
                double cursor = x + (m == first ? 0.5 * sp : 0.0);
                if (m == first) {
                    measure.showClef = true;
                    measure.clefX = cursor;
                    cursor += (kClefWidth - 0.5) * sp;
                }
                measure.key = keyGlyphs(record.fifths, m > 0 ? content.measures[m - 1].fifths : 0, m == first, staff.clef);
                for (ScoreGlyphLayout &glyph : measure.key) {
                    glyph.x = cursor + 0.45 * sp;
                    cursor += 0.9 * sp;
                }
                if (!measure.key.isEmpty()) cursor += 0.6 * sp;
                if (timeChanges(content, m)) {
                    measure.showTime = true;
                    measure.timeX = cursor + 1.0 * sp;
                    cursor += kTimeWidth * sp;
                }
                measure.contentX = cursor + kLeftPad * sp;
                double cx = measure.contentX;
                const Metrics &mm = metrics[m];
                for (int c = 0; c < mm.columns.size(); ++c) {
                    cx += mm.accidental[c] * sp;
                    measure.columns.push_back({mm.columns[c], cx + kHeadOffset * sp});
                    cx += mm.segments[c] * factor * sp + mm.dots[c] * sp;
                }
                measure.x1 = cx + kRightPad * sp;
                x = measure.x1;
                const auto columnX = [&measure](qint64 tick) {
                    for (const auto &[at, cxAt] : measure.columns)
                        if (at == tick) return cxAt;
                    return measure.columns.isEmpty() ? measure.contentX : measure.columns.front().second;
                };
                for (const ScoreVoice &voice : row.staff->voices) {
                    ScoreVoiceLayout voiceLayout;
                    voiceLayout.id = voice.id;
                    voiceLayout.number = voice.number;
                    for (const ScoreEvent &event : voice.events) {
                        if (event.tick < measure.start || event.tick >= measure.end) continue;
                        ScoreEventLayout e;
                        e.id = event.id;
                        e.tick = event.tick;
                        e.ticks = ticks(event.duration).value_or(0);
                        e.rest = event.rest;
                        e.x = columnX(event.tick);
                        e.base = event.duration.base;
                        e.dots = event.duration.dots;
                        e.filled = event.duration.base >= 2;
                        e.articulations = event.articulations;
                        e.dynamic = event.dynamic;
                        e.durationId = durationId(event.duration);
                        e.label = describe(content, event, m, voice.number);
                        for (const ScoreNote &note : event.notes) {
                            ScoreHeadLayout head;
                            head.noteId = note.id;
                            head.step = diatonic(note.pitch) - clefBottomLine(row.staff->clef);
                            head.accidental = accidentalMaps[r].value(note.id);
                            head.tieStart = note.tie;
                            e.heads.push_back(head);
                        }
                        if (!e.heads.isEmpty()) e.step = e.heads.front().step;
                        voiceLayout.events.push_back(e);
                    }
                    measure.voices.push_back(voiceLayout);
                }
                staff.measures.push_back(measure);
            }
            staff.x1 = x;
            engraveStaff(content, *row.staff, staff, tiedInto, sp);
            system.staves.push_back(staff);
        }
        layout.pages.back().systems.push_back(system);
    }
    return layout;
}

QString describe(const ScoreContent &content, const ScoreEvent &event, int measureIndex, int voiceNumber)
{
    const QVector<qint64> starts = measureStarts(content);
    const ScoreMeasure &measure = content.measures.value(measureIndex);
    const qint64 beatTicks = std::max<qint64>(1, kWholeTicks / std::max(1, measure.denominator));
    const qint64 offset = event.tick - starts.value(measureIndex) + measurePhase(measure);
    QString beat = QString::number(offset / beatTicks + 1);
    if (const qint64 rest = offset % beatTicks) {
        const qint64 divisor = std::gcd(rest, beatTicks);
        beat += QStringLiteral(" %1/%2").arg(rest / divisor).arg(beatTicks / divisor);
    }
    QString value = durationId(event.duration);
    value.replace(QLatin1Char('-'), QLatin1Char(' '));
    QString what;
    if (event.rest) {
        what = QObject::tr("%1 rest").arg(value);
    } else {
        QStringList pitches;
        for (const ScoreNote &note : event.notes) {
            static const char *alters[] = {" double flat", " flat", "", " sharp", " double sharp"};
            QString name = QString(QChar::fromLatin1("CDEFGAB"[std::clamp(note.pitch.step, 0, 6)]))
                           + QString::fromLatin1(alters[std::clamp(note.pitch.alter + 2, 0, 4)])
                           + QLatin1Char(' ') + QString::number(note.pitch.octave);
            if (note.tie) name += QObject::tr(" tied");
            pitches.push_back(name);
        }
        what = QObject::tr("%1 %2 %3").arg(value, event.notes.size() > 1 ? QObject::tr("chord") : QObject::tr("note"),
                                           pitches.join(QStringLiteral(", ")));
    }
    if (!event.articulations.isEmpty()) what += QStringLiteral(", ") + event.articulations.join(QStringLiteral(", "));
    if (!event.dynamic.isEmpty()) what += QStringLiteral(", ") + event.dynamic;
    return QObject::tr("Measure %1, beat %2, voice %3: %4").arg(measureIndex + 1).arg(beat).arg(voiceNumber).arg(what);
}

} // namespace QindaTK::Score
