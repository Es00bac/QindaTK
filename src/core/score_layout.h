// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "score_types.h"

#include <QHash>
#include <QSet>
#include <QSizeF>
#include <QVariantMap>

class QPainter;

namespace QindaTK {

// AGENT-CONTRACT (D-179, docs/notation.md#layout): the engraving of a score.
// Score::layoutScore is the only place that decides positions; ScoreView
// draws its snapshot (Score::pageSnapshot) and PDF/image output paints the
// same structure (Score::paintPage). Units are points on the page; a staff
// step is half a staff space and step 0 is the bottom staff line.
struct ScoreLayoutOptions {
    QSizeF pageSize{595.0, 842.0}; // A4 in points
    double staffSpace = 7.0;
    double margin = 42.0;
    QSet<QUuid> parts;             // empty: every part
};

struct ScoreHeadLayout {
    QUuid noteId;
    double step = 0;
    double dx = 0;           // head offset for seconds in a chord
    QString accidental;      // "", sharp, flat, natural, double-sharp, double-flat
    bool tieStart = false;
};

struct ScoreEventLayout {
    QUuid id;
    qint64 tick = 0;
    qint64 ticks = 0;
    bool rest = false;
    double x = 0;            // head centre
    double step = 4;         // first head, or the rest position
    int base = 2;
    int dots = 0;
    bool filled = true;
    int stem = 0;            // 1 up, -1 down, 0 none
    double stemEnd = 0;      // staff step of the stem tip
    int flags = 0;           // unbeamed flags
    bool beamed = false;
    QVector<ScoreHeadLayout> heads;
    QStringList articulations;
    double articulationStep = 0;
    QString dynamic;
    QString label;           // spoken description
    QString durationId;
};

struct ScoreVoiceLayout {
    QUuid id;
    int number = 1;
    QVector<ScoreEventLayout> events;
};

struct ScoreGlyphLayout {
    double x = 0;
    double step = 0;
    QString name; // sharp | flat | natural
};

struct ScoreMeasureLayout {
    QUuid id;
    int index = 0;
    qint64 start = 0;
    qint64 end = 0;
    double x0 = 0, x1 = 0, contentX = 0;
    bool showClef = false;
    double clefX = 0;
    QVector<ScoreGlyphLayout> key;
    bool showTime = false;
    double timeX = 0;
    int numerator = 4, denominator = 4;
    QVector<std::pair<qint64, double>> columns; // tick -> x, ascending
    QVector<ScoreVoiceLayout> voices;
};

struct ScoreTieLayout { double x0 = 0, x1 = 0, step = 0; bool above = true; };
struct ScoreBeamLayout { double x0 = 0, s0 = 0, x1 = 0, s1 = 0; bool up = true; };
struct ScoreTupletLayout { double x0 = 0, x1 = 0, step = 0; QString label; };

struct ScoreStaffLayout {
    QUuid id;
    QUuid partId;
    QString label;
    ScoreClef clef = ScoreClef::Treble;
    double top = 0;          // top staff line
    double x0 = 0, x1 = 0;
    QVector<ScoreMeasureLayout> measures;
    QVector<ScoreTieLayout> ties;
    QVector<ScoreBeamLayout> beams;
    QVector<ScoreTupletLayout> tuplets;
};

struct ScoreSystemLayout {
    int index = 0;
    int firstMeasure = 0;
    int lastMeasure = 0;
    double top = 0, bottom = 0;
    QVector<ScoreStaffLayout> staves;
};

struct ScorePageLayout {
    int index = 0;
    QVector<ScoreSystemLayout> systems;
};

struct ScoreLayout {
    QSizeF pageSize;
    double staffSpace = 7.0;
    double titleY = 0;
    QString title;
    QString composer;
    QVector<ScorePageLayout> pages;
};

namespace Score {
ScoreLayout layoutScore(const ScoreContent &content, const ScoreLayoutOptions &options);
// Note IDs a tie from the previous event ends on (no accidental is shown).
QSet<QUuid> tieContinuations(const ScoreContent &content);
// The accidental shown on every note of one staff (score_layout_notes.cpp):
// key signature, earlier alterations in the measure (every voice) and ties.
QHash<QUuid, QString> accidentals(const ScoreContent &content, const ScoreStaff &staff, const QSet<QUuid> &tiedInto);
// Stems, beams, flags, rest positions, ties, tuplets and articulations of
// one staff in one system, after its x positions are known.
void engraveStaff(const ScoreContent &content, const ScoreStaff &staff, ScoreStaffLayout &layout,
                  const QSet<QUuid> &tiedInto, double staffSpace);
QString describe(const ScoreContent &content, const ScoreEvent &event, int measureIndex, int voiceNumber);
// ScoreView's `layoutSnapshot` for one page, normalized to the page (score_render.cpp).
QVariantMap pageSnapshot(const ScoreLayout &layout, int page, const QString &scoreId, quint64 revision);
// Paints one page in points onto `painter` (PDF and images).
void paintPage(QPainter *painter, const ScoreLayout &layout, int page);
} // namespace Score

} // namespace QindaTK
