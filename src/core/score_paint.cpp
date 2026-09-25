// SPDX-License-Identifier: LGPL-3.0-or-later
// The score painter (WP-067, D-179, docs/notation.md#output): draws a layout
// snapshot with QPainter for ScoreView (ScoreCanvasItem), PDF pages and
// images. Glyph shapes are score_glyphs.cpp.
#include "score_paint.h"

#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QVariantList>
#include <algorithm>
#include <cmath>

namespace QindaTK::Score {

namespace {

struct Frame {
    double w = 1, h = 1;
    double x(const QVariant &value) const { return value.toDouble() * w; }
    double y(const QVariant &value) const { return value.toDouble() * h; }
};

double stepY(double top, double sp, double step)
{
    return top + (8.0 - step) * sp / 2.0;
}

void text(QPainter *p, const ScorePaintStyle &style, const QString &value, const QPointF &at, double size,
          Qt::Alignment align = Qt::AlignCenter, bool bold = false, bool italic = false)
{
    QFont font = p->font();
    if (!style.fontFamily.isEmpty()) font.setFamily(style.fontFamily);
    font.setPixelSize(std::max(1, qRound(size)));
    font.setBold(bold);
    font.setItalic(italic);
    p->setFont(font);
    p->setPen(p->brush().color());
    const double width = QFontMetricsF(font).horizontalAdvance(value) + size;
    const double left = align & Qt::AlignRight ? at.x() - width : align & Qt::AlignLeft ? at.x() : at.x() - width / 2;
    p->drawText(QRectF(left, at.y() - size, width, 2 * size), int(align & Qt::AlignHorizontal_Mask) | Qt::AlignVCenter, value);
}

void setInk(QPainter *p, const QColor &color)
{
    p->setBrush(color);
    p->setPen(QPen(color));
}

void paintEvent(QPainter *p, const QVariantMap &event, const Frame &f, double top, double sp, const ScorePaintStyle &style)
{
    const double x = f.x(event.value(QStringLiteral("x")));
    const int base = event.value(QStringLiteral("base"), 2).toInt();
    const int dots = std::clamp(event.value(QStringLiteral("dotCount")).toInt(), 0, 3);
    if (event.value(QStringLiteral("kind")).toString() == QLatin1String("rest")) {
        const double y = stepY(top, sp, event.value(QStringLiteral("staffStep"), 4).toDouble());
        ScoreGlyph::rest(p, base, x, y, sp);
        for (int d = 0; d < dots; ++d) {
            p->setPen(Qt::NoPen);
            p->drawEllipse(QPointF(x + (1.0 + 0.5 * d) * sp, y - 0.5 * sp), 0.16 * sp, 0.16 * sp);
        }
        const QStringList marks = event.value(QStringLiteral("articulations")).toStringList();
        if (marks.contains(QStringLiteral("fermata")))
            ScoreGlyph::articulation(p, QStringLiteral("fermata"), x, stepY(top, sp, 10.5), sp, true);
        return;
    }
    QVariantList heads = event.value(QStringLiteral("heads")).toList();
    if (heads.isEmpty())
        heads.push_back(QVariantMap{{QStringLiteral("staffStep"), event.value(QStringLiteral("staffStep"))},
                                    {QStringLiteral("accidental"), event.value(QStringLiteral("accidental"))}});
    double low = 1e9, high = -1e9;
    for (const QVariant &value : heads) {
        const QVariantMap head = value.toMap();
        const double step = head.value(QStringLiteral("staffStep")).toDouble();
        low = std::min(low, step);
        high = std::max(high, step);
        const double hx = x + head.value(QStringLiteral("dx")).toDouble() * f.w;
        const double y = stepY(top, sp, step);
        p->setPen(QPen(p->brush().color(), 0.13 * sp));
        for (double ledger = -2; ledger >= step - 0.01; ledger -= 2)
            p->drawLine(QPointF(hx - 0.95 * sp, stepY(top, sp, ledger)), QPointF(hx + 0.95 * sp, stepY(top, sp, ledger)));
        for (double ledger = 10; ledger <= step + 0.01; ledger += 2)
            p->drawLine(QPointF(hx - 0.95 * sp, stepY(top, sp, ledger)), QPointF(hx + 0.95 * sp, stepY(top, sp, ledger)));
        ScoreGlyph::notehead(p, hx, y, sp, base, style.surface);
        const QString accidental = head.value(QStringLiteral("accidental")).toString();
        if (!accidental.isEmpty()) ScoreGlyph::accidental(p, accidental, hx - 1.45 * sp, y, sp);
        const double dotY = std::fmod(std::abs(step), 2.0) < 0.01 ? y - sp / 2 : y;
        p->setPen(Qt::NoPen);
        for (int d = 0; d < dots; ++d) p->drawEllipse(QPointF(x + (1.1 + 0.5 * d) * sp, dotY), 0.16 * sp, 0.16 * sp);
    }
    const QString stem = event.value(QStringLiteral("stemDirection")).toString();
    if (!stem.isEmpty()) {
        const bool up = stem != QLatin1String("down");
        const double sx = event.contains(QStringLiteral("stemX")) ? f.x(event.value(QStringLiteral("stemX")))
                                                                  : x + (up ? 0.55 : -0.55) * sp;
        const double end = event.contains(QStringLiteral("stemEndStep")) ? event.value(QStringLiteral("stemEndStep")).toDouble()
                                                                         : up ? high + 7 : low - 7;
        p->setPen(QPen(p->brush().color(), 0.12 * sp, Qt::SolidLine, Qt::FlatCap));
        p->drawLine(QPointF(sx, stepY(top, sp, up ? low : high)), QPointF(sx, stepY(top, sp, end)));
        ScoreGlyph::flags(p, sx, stepY(top, sp, end), sp, event.value(QStringLiteral("flags")).toInt(), up);
    }
    const QStringList marks = event.value(QStringLiteral("articulations")).toStringList();
    const bool above = stem == QLatin1String("down");
    double step = event.value(QStringLiteral("articulationStep")).toDouble();
    for (const QString &mark : marks) {
        if (mark == QLatin1String("fermata")) {
            ScoreGlyph::articulation(p, mark, x, stepY(top, sp, std::max(high + 3.0, 10.5)), sp, true);
            continue;
        }
        ScoreGlyph::articulation(p, mark, x, stepY(top, sp, step), sp, above);
        step += above ? 1.6 : -1.6;
    }
    const QString dynamic = event.value(QStringLiteral("dynamic")).toString();
    if (!dynamic.isEmpty())
        text(p, style, dynamic, QPointF(x, stepY(top, sp, std::min(-5.0, low - 4.0))), 2.0 * sp, Qt::AlignHCenter, true, true);
}

void paintStaff(QPainter *p, const QVariantMap &staff, const Frame &f, const ScorePaintStyle &style,
                const QString &selectedEventId, int &painted, int maxEvents, bool numbers)
{
    const double top = f.y(staff.value(QStringLiteral("top")));
    const double sp = f.y(staff.value(QStringLiteral("lineGap")));
    const double x0 = f.x(staff.value(QStringLiteral("x0"))), x1 = f.x(staff.value(QStringLiteral("x1")));
    p->setPen(QPen(style.muted, 0.1 * sp));
    for (int line = 0; line < 5; ++line) p->drawLine(QPointF(x0, top + line * sp), QPointF(x1, top + line * sp));
    const QString label = staff.value(QStringLiteral("label")).toString();
    setInk(p, style.muted);
    if (!label.isEmpty()) text(p, style, label, QPointF(x0 - 1.6 * sp, top + 2 * sp), 1.5 * sp, Qt::AlignRight);
    const QVariantList measures = staff.value(QStringLiteral("measures")).toList();
    for (const QVariant &measureValue : measures) {
        const QVariantMap measure = measureValue.toMap();
        const double bx1 = f.x(measure.value(QStringLiteral("x1")));
        if (measure.value(QStringLiteral("final")).toBool()) {
            p->setPen(QPen(style.border, 0.16 * sp));
            p->drawLine(QPointF(bx1 - 0.9 * sp, top), QPointF(bx1 - 0.9 * sp, top + 4 * sp));
            p->fillRect(QRectF(bx1 - 0.5 * sp, top, 0.5 * sp, 4 * sp), style.ink);
        } else {
            p->setPen(QPen(style.border, 0.16 * sp));
            p->drawLine(QPointF(bx1, top), QPointF(bx1, top + 4 * sp));
        }
        const QString number = measure.value(QStringLiteral("number")).toString();
        setInk(p, style.muted);
        if (numbers && !number.isEmpty()) // above the system's top staff only
            text(p, style, number, QPointF(f.x(measure.value(QStringLiteral("x0"))), top - 1.6 * sp), 1.3 * sp, Qt::AlignLeft,
                 false, true);
        setInk(p, style.ink);
        if (measure.contains(QStringLiteral("clefX")))
            ScoreGlyph::clef(p, staff.value(QStringLiteral("clef")).toString(), f.x(measure.value(QStringLiteral("clefX"))), top, sp);
        for (const QVariant &keyValue : measure.value(QStringLiteral("keyGlyphs")).toList()) {
            const QVariantMap key = keyValue.toMap();
            ScoreGlyph::accidental(p, key.value(QStringLiteral("name")).toString(), f.x(key.value(QStringLiteral("x"))),
                                   stepY(top, sp, key.value(QStringLiteral("staffStep")).toDouble()), sp);
        }
        if (measure.contains(QStringLiteral("time"))) {
            const QVariantMap time = measure.value(QStringLiteral("time")).toMap();
            const double tx = f.x(time.value(QStringLiteral("x")));
            text(p, style, time.value(QStringLiteral("numerator")).toString(), QPointF(tx, top + sp), 2.9 * sp, Qt::AlignHCenter, true);
            text(p, style, time.value(QStringLiteral("denominator")).toString(), QPointF(tx, top + 3 * sp), 2.9 * sp, Qt::AlignHCenter, true);
        }
        for (const QVariant &voiceValue : measure.value(QStringLiteral("voices")).toList())
            for (const QVariant &eventValue : voiceValue.toMap().value(QStringLiteral("events")).toList()) {
                if (painted >= maxEvents) return;
                const QVariantMap event = eventValue.toMap();
                setInk(p, event.value(QStringLiteral("id")).toString() == selectedEventId && !selectedEventId.isEmpty()
                              ? style.accent : style.ink);
                paintEvent(p, event, f, top, sp, style);
                ++painted;
            }
    }
    setInk(p, style.ink);
    p->setPen(Qt::NoPen);
    for (const QVariant &value : staff.value(QStringLiteral("beams")).toList()) {
        const QVariantMap beam = value.toMap();
        const double thickness = (beam.value(QStringLiteral("up")).toBool() ? 0.5 : -0.5) * sp;
        const QPointF a(f.x(beam.value(QStringLiteral("x0"))), stepY(top, sp, beam.value(QStringLiteral("step0")).toDouble()));
        const QPointF b(f.x(beam.value(QStringLiteral("x1"))), stepY(top, sp, beam.value(QStringLiteral("step1")).toDouble()));
        p->drawPolygon(QPolygonF{a, b, b + QPointF(0, thickness), a + QPointF(0, thickness)});
    }
    for (const QVariant &value : staff.value(QStringLiteral("ties")).toList()) {
        const QVariantMap tie = value.toMap();
        const bool tieAbove = tie.value(QStringLiteral("above")).toBool();
        const double y = stepY(top, sp, tie.value(QStringLiteral("staffStep")).toDouble()) + (tieAbove ? -0.6 : 0.6) * sp;
        ScoreGlyph::tie(p, f.x(tie.value(QStringLiteral("x0"))), f.x(tie.value(QStringLiteral("x1"))), y, sp, tieAbove);
    }
    for (const QVariant &value : staff.value(QStringLiteral("tuplets")).toList()) {
        const QVariantMap tuplet = value.toMap();
        const double tx0 = f.x(tuplet.value(QStringLiteral("x0"))), tx1 = f.x(tuplet.value(QStringLiteral("x1")));
        const double step = tuplet.value(QStringLiteral("staffStep")).toDouble();
        const double y = stepY(top, sp, step), hook = (step >= 4 ? 0.6 : -0.6) * sp, mid = (tx0 + tx1) / 2;
        p->setPen(QPen(style.ink, 0.1 * sp));
        p->drawPolyline(QPolygonF{{tx0, y + hook}, {tx0, y}, {mid - 0.9 * sp, y}});
        p->drawPolyline(QPolygonF{{mid + 0.9 * sp, y}, {tx1, y}, {tx1, y + hook}});
        text(p, style, tuplet.value(QStringLiteral("label")).toString(), QPointF(mid, y), 1.5 * sp, Qt::AlignHCenter, true, true);
    }
}

} // namespace

int paintSnapshot(QPainter *p, const QVariantMap &snapshot, const QSizeF &size, const ScorePaintStyle &style,
                  const QString &selectedEventId, double cursorX, int maxEvents)
{
    if (!p) return 0;
    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);
    p->setRenderHint(QPainter::TextAntialiasing, true);
    if (style.fillBackground) p->fillRect(QRectF(QPointF(0, 0), size), style.surface);
    int painted = 0;
    if (snapshot.value(QStringLiteral("available")).toBool()) {
        const Frame f{size.width(), size.height()};
        const QString title = snapshot.value(QStringLiteral("title")).toString();
        if (!title.isEmpty()) {
            const double y = f.y(snapshot.value(QStringLiteral("titleY")));
            const QVariantList systems = snapshot.value(QStringLiteral("systems")).toList();
            const double sp = systems.isEmpty() ? size.height() / 100
                                                : f.y(systems.first().toMap().value(QStringLiteral("staves")).toList().value(0)
                                                          .toMap().value(QStringLiteral("lineGap")));
            setInk(p, style.ink);
            text(p, style, title, QPointF(size.width() / 2, y), 2.8 * sp, Qt::AlignHCenter, true);
            const QString composer = snapshot.value(QStringLiteral("composer")).toString();
            if (!composer.isEmpty())
                text(p, style, composer, QPointF(size.width() - 4 * sp, y + 2.2 * sp), 1.5 * sp, Qt::AlignRight);
        }
        for (const QVariant &systemValue : snapshot.value(QStringLiteral("systems")).toList()) {
            const QVariantList staves = systemValue.toMap().value(QStringLiteral("staves")).toList();
            if (staves.isEmpty()) continue;
            // The system's opening bar line and bracket join every staff.
            const QVariantMap first = staves.first().toMap(), last = staves.last().toMap();
            const double sp = f.y(first.value(QStringLiteral("lineGap")));
            const double x0 = f.x(first.value(QStringLiteral("x0")));
            const double y0 = f.y(first.value(QStringLiteral("top")));
            const double y1 = f.y(last.value(QStringLiteral("top"))) + 4 * f.y(last.value(QStringLiteral("lineGap")));
            p->setPen(QPen(style.border, 0.16 * sp));
            p->drawLine(QPointF(x0, y0), QPointF(x0, y1));
            if (staves.size() > 1) {
                p->fillRect(QRectF(x0 - 1.1 * sp, y0 - 0.5 * sp, 0.5 * sp, y1 - y0 + sp), style.ink);
                setInk(p, style.ink);
                ScoreGlyph::tie(p, x0 - 1.1 * sp, x0 + 0.3 * sp, y0 - 0.5 * sp, sp, true);
                ScoreGlyph::tie(p, x0 - 1.1 * sp, x0 + 0.3 * sp, y1 + 0.5 * sp, sp, false);
            }
            for (qsizetype s = 0; s < staves.size(); ++s)
                paintStaff(p, staves[s].toMap(), f, style, selectedEventId, painted, maxEvents, s == 0);
        }
        if (cursorX >= 0) {
            p->setPen(QPen(style.accent, std::max(1.0, size.width() / 800)));
            p->drawLine(QPointF(cursorX * size.width(), 0), QPointF(cursorX * size.width(), size.height()));
        }
    }
    p->restore();
    return painted;
}

} // namespace QindaTK::Score
