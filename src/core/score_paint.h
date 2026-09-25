// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QColor>
#include <QPainterPath>
#include <QSizeF>
#include <QString>
#include <QVariantMap>

class QPainter;

namespace QindaTK {

// AGENT-CONTRACT (D-179, docs/notation.md#output): the one score painter.
// Score::paintSnapshot draws a ScoreView layout snapshot (Score::pageSnapshot,
// coordinates normalized to `size`) with a QPainter; ScoreView paints through
// it (ScoreCanvasItem) and PDF and image output through Score::paintPage, so
// the screen and the page cannot drift. Positions come only from the
// snapshot; this painter decides glyph shapes.
struct ScorePaintStyle {
    QColor surface = Qt::white; // page, and the holes of open noteheads
    QColor ink = Qt::black;
    QColor muted = Qt::black;   // staff lines, part labels, measure numbers
    QColor border = Qt::black;  // bar lines
    QColor accent = Qt::black;  // the selection and the playback cursor
    QString fontFamily;         // time signatures, dynamics, labels (empty: default)
    bool fillBackground = true;
};

namespace ScoreGlyph {
// Engraving glyphs as vector paths in staff spaces (score_glyphs.cpp), so the
// score needs no music font. `sp` is the staff space in painter units.
void clef(QPainter *painter, const QString &name, double x, double staffTop, double sp);
void accidental(QPainter *painter, const QString &name, double x, double y, double sp);
void notehead(QPainter *painter, double x, double y, double sp, int base, const QColor &hole);
void rest(QPainter *painter, int base, double x, double middleLineY, double sp);
void flags(QPainter *painter, double stemX, double tipY, double sp, int count, bool up);
void tie(QPainter *painter, double x0, double x1, double y, double sp, bool above);
void articulation(QPainter *painter, const QString &name, double x, double y, double sp, bool above);
} // namespace ScoreGlyph

namespace Score {
// Returns the number of events drawn (at most `maxEvents`).
int paintSnapshot(QPainter *painter, const QVariantMap &snapshot, const QSizeF &size, const ScorePaintStyle &style,
                  const QString &selectedEventId = {}, double cursorX = -1, int maxEvents = 1 << 20);
} // namespace Score

} // namespace QindaTK
