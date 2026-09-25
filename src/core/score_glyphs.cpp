// SPDX-License-Identifier: LGPL-3.0-or-later
// Engraving glyphs as vector paths (WP-067, D-179, docs/notation.md#output).
// AGENT-NOTE: shapes follow common engraving proportions in staff spaces (a
// G clef about 7.5 spaces tall, noteheads 1.18 spaces wide tilted 20
// degrees) so the score reads the same on every machine without a SMuFL
// font; y grows downward. The painter's brush and pen colour are the ink.
#include "score_paint.h"

#include <QPainter>
#include <QPolygonF>
#include <cmath>
#include <initializer_list>

namespace QindaTK::ScoreGlyph {

namespace {

struct Cubic { double c1x, c1y, c2x, c2y, x, y; };

// A path from (x0, y0) through cubic segments, all in spaces from `origin`.
QPainterPath curve(const QPointF &origin, double sp, double x0, double y0, std::initializer_list<Cubic> segments)
{
    QPainterPath path(origin + QPointF(x0 * sp, y0 * sp));
    for (const Cubic &c : segments)
        path.cubicTo(origin + QPointF(c.c1x * sp, c.c1y * sp), origin + QPointF(c.c2x * sp, c.c2y * sp),
                     origin + QPointF(c.x * sp, c.y * sp));
    return path;
}

void strokePath(QPainter *p, const QPainterPath &path, double width)
{
    QPen pen(p->brush().color(), width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p->strokePath(path, pen);
}

void polygon(QPainter *p, const QPointF &origin, double sp, std::initializer_list<QPointF> points)
{
    QPolygonF poly;
    for (const QPointF &point : points) poly << origin + point * sp;
    p->setPen(Qt::NoPen);
    p->drawPolygon(poly);
}

// A slanted thick bar (accidentals) from (x0, y0) to (x1, y1), in spaces.
void slab(QPainter *p, const QPointF &origin, double sp, double x0, double y0, double x1, double y1, double thickness)
{
    const double h = thickness / 2;
    polygon(p, origin, sp, {{x0, y0 - h}, {x1, y1 - h}, {x1, y1 + h}, {x0, y0 + h}});
}

void line(QPainter *p, const QPointF &a, const QPointF &b, double width)
{
    p->setPen(QPen(p->brush().color(), width, Qt::SolidLine, Qt::FlatCap));
    p->drawLine(a, b);
}

void dot(QPainter *p, const QPointF &centre, double radius)
{
    p->setPen(Qt::NoPen);
    p->drawEllipse(centre, radius, radius);
}

void flat(QPainter *p, const QPointF &o, double sp)
{
    line(p, o + QPointF(-0.3 * sp, -1.75 * sp), o + QPointF(-0.3 * sp, 0.5 * sp), 0.12 * sp);
    QPainterPath bowl = curve(o, sp, -0.3, 0.5, {{0.55, 0.0, 0.6, -0.75, -0.3, -0.25}});
    strokePath(p, bowl, 0.2 * sp);
}

} // namespace

void clef(QPainter *p, const QString &name, double x, double staffTop, double sp)
{
    if (name == QLatin1String("bass")) {
        const QPointF o(x + 0.25 * sp, staffTop + sp); // F line
        dot(p, o + QPointF(0.25 * sp, 0), 0.34 * sp);
        strokePath(p, curve(o, sp, 0.05, -0.2, {{0.3, -1.05, 1.65, -1.15, 1.75, -0.1}, {1.85, 1.15, 0.9, 2.15, -0.1, 2.65}}),
                   0.24 * sp);
        dot(p, o + QPointF(2.25 * sp, -0.5 * sp), 0.15 * sp);
        dot(p, o + QPointF(2.25 * sp, 0.5 * sp), 0.15 * sp);
    } else if (name == QLatin1String("alto")) {
        const QPointF o(x + 0.3 * sp, staffTop + 2 * sp); // C line
        p->setPen(Qt::NoPen);
        p->drawRect(QRectF(o.x(), o.y() - 2 * sp, 0.4 * sp, 4 * sp));
        p->drawRect(QRectF(o.x() + 0.6 * sp, o.y() - 2 * sp, 0.13 * sp, 4 * sp));
        for (int side = -1; side <= 1; side += 2) {
            strokePath(p, curve(o, sp, 0.75, 0, {{1.0, -0.35 * side, 1.1, -0.75 * side, 1.3, -1.2 * side},
                                                 {1.5, -1.9 * side, 2.3, -2.0 * side, 2.2, -1.2 * side}}),
                       0.22 * sp);
            dot(p, o + QPointF(1.95 * sp, -1.15 * side * sp), 0.26 * sp);
        }
    } else {
        const QPointF o(x + 1.1 * sp, staffTop + 3 * sp); // G line, spiral centre
        strokePath(p, curve(o, sp, 0.2, 0.05, {{0.2, -0.45, -0.5, -0.45, -0.55, 0.1}, {-0.6, 0.75, 0.25, 1.05, 0.7, 0.75},
                                               {1.25, 0.4, 1.1, -0.65, 0.35, -0.8}, {-0.6, -0.95, -1.05, -1.7, -0.5, -2.45},
                                               {-0.1, -3.0, 0.45, -3.45, 0.45, -4.1}, {0.45, -4.75, -0.05, -4.7, -0.12, -4.15},
                                               {-0.25, -3.4, 0.05, -1.0, 0.32, 1.8}, {0.38, 2.45, -0.05, 2.75, -0.42, 2.45}}),
                   0.2 * sp);
        dot(p, o + QPointF(-0.3 * sp, 2.3 * sp), 0.3 * sp);
    }
}

void accidental(QPainter *p, const QString &name, double x, double y, double sp)
{
    const QPointF o(x, y);
    if (name == QLatin1String("sharp")) {
        line(p, o + QPointF(-0.2 * sp, -1.25 * sp), o + QPointF(-0.2 * sp, 1.05 * sp), 0.11 * sp);
        line(p, o + QPointF(0.2 * sp, -1.05 * sp), o + QPointF(0.2 * sp, 1.25 * sp), 0.11 * sp);
        slab(p, o, sp, -0.42, -0.25, 0.42, -0.55, 0.26);
        slab(p, o, sp, -0.42, 0.55, 0.42, 0.25, 0.26);
    } else if (name == QLatin1String("flat")) {
        flat(p, o, sp);
    } else if (name == QLatin1String("natural")) {
        line(p, o + QPointF(-0.22 * sp, -1.3 * sp), o + QPointF(-0.22 * sp, 0.6 * sp), 0.11 * sp);
        line(p, o + QPointF(0.22 * sp, -0.6 * sp), o + QPointF(0.22 * sp, 1.3 * sp), 0.11 * sp);
        slab(p, o, sp, -0.22, -0.3, 0.22, -0.45, 0.24);
        slab(p, o, sp, -0.22, 0.45, 0.22, 0.3, 0.24);
    } else if (name == QLatin1String("double-flat")) {
        flat(p, o + QPointF(-0.3 * sp, 0), sp);
        flat(p, o + QPointF(0.35 * sp, 0), sp);
    } else if (name == QLatin1String("double-sharp")) {
        // Four arrowhead corners joined through the centre.
        for (int dx = -1; dx <= 1; dx += 2)
            for (int dy = -1; dy <= 1; dy += 2) {
                line(p, o, o + QPointF(0.3 * dx * sp, 0.3 * dy * sp), 0.12 * sp);
                polygon(p, o, sp, {{0.2 * dx, 0.42 * dy}, {0.42 * dx, 0.42 * dy}, {0.42 * dx, 0.2 * dy}});
            }
    }
}

void notehead(QPainter *p, double x, double y, double sp, int base, const QColor &hole)
{
    p->save();
    p->translate(x, y);
    p->setPen(Qt::NoPen);
    if (base == 0) {
        p->drawEllipse(QPointF(0, 0), 0.75 * sp, 0.5 * sp);
        p->setBrush(hole);
        p->rotate(57);
        p->drawEllipse(QPointF(0, 0), 0.36 * sp, 0.24 * sp);
    } else {
        p->rotate(-20);
        p->drawEllipse(QPointF(0, 0), 0.62 * sp, 0.42 * sp);
        if (base == 1) {
            p->setBrush(hole);
            p->drawEllipse(QPointF(0, 0), 0.5 * sp, 0.18 * sp);
        }
    }
    p->restore();
}

void rest(QPainter *p, int base, double x, double y, double sp)
{
    const QPointF o(x, y);
    p->setPen(Qt::NoPen);
    if (base == 0) { // hangs from the fourth line
        p->drawRect(QRectF(x - 0.6 * sp, y - sp, 1.2 * sp, 0.5 * sp));
    } else if (base == 1) { // sits on the middle line
        p->drawRect(QRectF(x - 0.6 * sp, y - 0.5 * sp, 1.2 * sp, 0.5 * sp));
    } else if (base == 2) {
        QPainterPath z(o + QPointF(-0.2 * sp, -1.45 * sp));
        z.lineTo(o + QPointF(0.35 * sp, -0.8 * sp));
        z.lineTo(o + QPointF(-0.25 * sp, -0.15 * sp));
        z.lineTo(o + QPointF(0.3 * sp, 0.45 * sp));
        z.quadTo(o + QPointF(-0.45 * sp, 0.2 * sp), o + QPointF(-0.05 * sp, 1.2 * sp));
        strokePath(p, z, 0.22 * sp);
        slab(p, o, sp, -0.15, -0.8, 0.3, -0.8, 0.5);
    } else {
        const int hooks = base - 2;
        const double top = -0.5 - 0.5 * (hooks - 1);
        const double lean = 0.2 * (hooks - 1);
        line(p, o + QPointF((0.45 + lean) * sp, top * sp), o + QPointF(-0.15 * sp, (1.0 + lean) * sp), 0.12 * sp);
        for (int h = 0; h < hooks; ++h) {
            const QPointF end = o + QPointF((0.45 + lean - 0.2 * h) * sp, (top + h) * sp);
            dot(p, end + QPointF(-0.65 * sp, 0.05 * sp), 0.22 * sp);
            QPainterPath hook(end + QPointF(-0.65 * sp, 0.2 * sp));
            hook.quadTo(end + QPointF(-0.3 * sp, 0.35 * sp), end);
            strokePath(p, hook, 0.12 * sp);
        }
    }
}

void flags(QPainter *p, double x, double tipY, double sp, int count, bool up)
{
    const double d = up ? 1 : -1;
    p->setPen(Qt::NoPen);
    for (int f = 0; f < count; ++f) {
        const double y = tipY + d * f * 0.8 * sp;
        QPainterPath flag(QPointF(x, y));
        flag.cubicTo(QPointF(x + 0.15 * sp, y + d * 1.0 * sp), QPointF(x + 1.3 * sp, y + d * 1.3 * sp),
                     QPointF(x + 0.85 * sp, y + d * 2.8 * sp));
        flag.cubicTo(QPointF(x + 1.0 * sp, y + d * 1.75 * sp), QPointF(x + 0.35 * sp, y + d * 1.35 * sp),
                     QPointF(x, y + d * 1.1 * sp));
        flag.closeSubpath();
        p->drawPath(flag);
    }
}

void tie(QPainter *p, double x0, double x1, double y, double sp, bool above)
{
    const double d = above ? -1 : 1;
    const double mid = (x0 + x1) / 2;
    const double lift = std::min(1.2 * sp, 0.25 * (x1 - x0) + 0.4 * sp);
    QPainterPath path(QPointF(x0, y));
    path.quadTo(QPointF(mid, y + d * lift * 1.25), QPointF(x1, y));
    path.quadTo(QPointF(mid, y + d * lift * 0.9), QPointF(x0, y));
    p->setPen(Qt::NoPen);
    p->drawPath(path);
}

void articulation(QPainter *p, const QString &name, double x, double y, double sp, bool above)
{
    const QPointF o(x, y);
    const double d = above ? -1 : 1; // away from the note
    if (name == QLatin1String("staccato")) {
        dot(p, o, 0.18 * sp);
    } else if (name == QLatin1String("staccatissimo")) {
        polygon(p, o, sp, {{-0.2, 0.5 * d}, {0.2, 0.5 * d}, {0, -0.4 * d}});
    } else if (name == QLatin1String("tenuto")) {
        p->setPen(Qt::NoPen);
        p->drawRect(QRectF(x - 0.6 * sp, y - 0.07 * sp, 1.2 * sp, 0.14 * sp));
    } else if (name == QLatin1String("accent")) {
        line(p, o + QPointF(-0.6 * sp, -0.35 * sp), o + QPointF(0.6 * sp, 0), 0.13 * sp);
        line(p, o + QPointF(0.6 * sp, 0), o + QPointF(-0.6 * sp, 0.35 * sp), 0.13 * sp);
    } else if (name == QLatin1String("marcato")) {
        polygon(p, o, sp, {{-0.45, -0.45 * d}, {0, 0.55 * d}, {0.45, -0.45 * d}, {0.22, -0.45 * d}, {0, 0.05 * d}});
    } else if (name == QLatin1String("fermata")) {
        // An arc opening toward the note with a dot inside it.
        const QPointF left = o + QPointF(-1.1 * sp, -0.3 * d * sp), right = o + QPointF(1.1 * sp, -0.3 * d * sp);
        QPainterPath arc(left);
        arc.quadTo(o + QPointF(0, d * 1.5 * sp), right);
        arc.quadTo(o + QPointF(0, d * 1.1 * sp), left);
        p->setPen(Qt::NoPen);
        p->drawPath(arc);
        dot(p, o + QPointF(0, -0.05 * d * sp), 0.18 * sp);
    }
}

} // namespace QindaTK::ScoreGlyph
