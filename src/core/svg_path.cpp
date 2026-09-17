// SPDX-License-Identifier: LGPL-3.0-or-later
#include "svg_path.h"

#include <cmath>
#include <vector>

namespace QindaTK {

namespace {

class PathReader final {
public:
    explicit PathReader(QStringView d) : m_d(d) {}

    [[nodiscard]] bool atEnd()
    {
        skipSeparators();
        return m_pos >= m_d.size();
    }
    [[nodiscard]] bool peekIsCommand()
    {
        skipSeparators();
        return m_pos < m_d.size() && m_d[m_pos].isLetter();
    }
    [[nodiscard]] QChar readCommand()
    {
        skipSeparators();
        return m_d[m_pos++];
    }
    bool readNumber(qreal *out)
    {
        skipSeparators();
        const qsizetype start = m_pos;
        if (m_pos < m_d.size() && (m_d[m_pos] == u'-' || m_d[m_pos] == u'+')) {
            ++m_pos;
        }
        bool digits = false;
        while (m_pos < m_d.size() && m_d[m_pos].isDigit()) { ++m_pos; digits = true; }
        if (m_pos < m_d.size() && m_d[m_pos] == u'.') {
            ++m_pos;
            while (m_pos < m_d.size() && m_d[m_pos].isDigit()) { ++m_pos; digits = true; }
        }
        if (!digits) {
            m_pos = start;
            return false;
        }
        if (m_pos < m_d.size() && (m_d[m_pos] == u'e' || m_d[m_pos] == u'E')) {
            qsizetype save = m_pos;
            ++m_pos;
            if (m_pos < m_d.size() && (m_d[m_pos] == u'-' || m_d[m_pos] == u'+')) ++m_pos;
            bool expDigits = false;
            while (m_pos < m_d.size() && m_d[m_pos].isDigit()) { ++m_pos; expDigits = true; }
            if (!expDigits) m_pos = save;
        }
        bool ok = false;
        *out = m_d.mid(start, m_pos - start).toDouble(&ok);
        return ok;
    }
    // Arc flags are single characters and may be squeezed ("01").
    bool readFlag(bool *out)
    {
        skipSeparators();
        if (m_pos >= m_d.size() || (m_d[m_pos] != u'0' && m_d[m_pos] != u'1')) {
            return false;
        }
        *out = m_d[m_pos++] == u'1';
        return true;
    }

private:
    void skipSeparators()
    {
        while (m_pos < m_d.size() && (m_d[m_pos].isSpace() || m_d[m_pos] == u',')) {
            ++m_pos;
        }
    }
    QStringView m_d;
    qsizetype m_pos = 0;
};

// SVG implementation notes F.6.5: endpoint to centre parameterisation, then
// the arc is split into segments of at most 90 degrees, each a cubic.
void arcToBeziers(QPainterPath &path, QPointF from, qreal rx, qreal ry, qreal phiDeg, bool largeArc,
                  bool sweep, QPointF to)
{
    if (from == to) {
        return;
    }
    if (rx == 0 || ry == 0) {
        path.lineTo(to);
        return;
    }
    rx = std::abs(rx);
    ry = std::abs(ry);
    const qreal phi = phiDeg * M_PI / 180.0;
    const qreal cosPhi = std::cos(phi);
    const qreal sinPhi = std::sin(phi);
    const qreal dx = (from.x() - to.x()) / 2;
    const qreal dy = (from.y() - to.y()) / 2;
    const qreal x1p = cosPhi * dx + sinPhi * dy;
    const qreal y1p = -sinPhi * dx + cosPhi * dy;
    const qreal lambda = (x1p * x1p) / (rx * rx) + (y1p * y1p) / (ry * ry);
    if (lambda > 1) {
        rx *= std::sqrt(lambda);
        ry *= std::sqrt(lambda);
    }
    const qreal rx2 = rx * rx;
    const qreal ry2 = ry * ry;
    qreal num = rx2 * ry2 - rx2 * y1p * y1p - ry2 * x1p * x1p;
    const qreal den = rx2 * y1p * y1p + ry2 * x1p * x1p;
    qreal coef = den == 0 ? 0 : std::sqrt(std::max<qreal>(num / den, 0));
    if (largeArc == sweep) {
        coef = -coef;
    }
    const qreal cxp = coef * (rx * y1p / ry);
    const qreal cyp = coef * -(ry * x1p / rx);
    const qreal cx = cosPhi * cxp - sinPhi * cyp + (from.x() + to.x()) / 2;
    const qreal cy = sinPhi * cxp + cosPhi * cyp + (from.y() + to.y()) / 2;
    const auto angle = [](qreal ux, qreal uy, qreal vx, qreal vy) {
        const qreal dot = ux * vx + uy * vy;
        const qreal len = std::sqrt((ux * ux + uy * uy) * (vx * vx + vy * vy));
        qreal a = std::acos(std::clamp<qreal>(dot / len, -1, 1));
        if (ux * vy - uy * vx < 0) a = -a;
        return a;
    };
    const qreal theta1 = angle(1, 0, (x1p - cxp) / rx, (y1p - cyp) / ry);
    qreal delta = angle((x1p - cxp) / rx, (y1p - cyp) / ry, (-x1p - cxp) / rx, (-y1p - cyp) / ry);
    if (!sweep && delta > 0) delta -= 2 * M_PI;
    if (sweep && delta < 0) delta += 2 * M_PI;

    const int segments = std::max(1, static_cast<int>(std::ceil(std::abs(delta) / (M_PI / 2) - 1e-9)));
    const qreal step = delta / segments;
    qreal t = theta1;
    const auto point = [&](qreal a) {
        const qreal ex = rx * std::cos(a);
        const qreal ey = ry * std::sin(a);
        return QPointF(cosPhi * ex - sinPhi * ey + cx, sinPhi * ex + cosPhi * ey + cy);
    };
    const auto derivative = [&](qreal a) {
        const qreal ex = -rx * std::sin(a);
        const qreal ey = ry * std::cos(a);
        return QPointF(cosPhi * ex - sinPhi * ey, sinPhi * ex + cosPhi * ey);
    };
    for (int i = 0; i < segments; ++i) {
        const qreal t2 = t + step;
        const qreal alpha = std::sin(step) * (std::sqrt(4 + 3 * std::tan(step / 2) * std::tan(step / 2)) - 1) / 3;
        const QPointF p1 = point(t);
        const QPointF p2 = point(t2);
        const QPointF d1 = derivative(t);
        const QPointF d2 = derivative(t2);
        path.cubicTo(p1 + alpha * d1, p2 - alpha * d2, i == segments - 1 ? to : p2);
        t = t2;
    }
}

} // namespace

QPainterPath svgPathToPainterPath(QStringView d, bool *ok)
{
    QPainterPath path;
    PathReader reader(d);
    QChar command;
    QPointF current;
    QPointF subpathStart;
    QPointF lastControl;
    bool lastWasCubic = false;
    bool lastWasQuad = false;
    bool good = true;
    while (!reader.atEnd()) {
        if (reader.peekIsCommand()) {
            command = reader.readCommand();
        } else if (command.isNull()) {
            good = false;
            break;
        } else if (command == u'M') {
            command = u'L';
        } else if (command == u'm') {
            command = u'l';
        }
        const bool relative = command.isLower();
        const QChar c = command.toUpper();
        const QPointF base = relative ? current : QPointF(0, 0);
        bool cubic = false;
        bool quad = false;
        qreal a = 0, b = 0, e = 0, f = 0, g = 0, h = 0;
        switch (c.unicode()) {
        case u'M':
            if (!reader.readNumber(&a) || !reader.readNumber(&b)) { good = false; break; }
            current = base + QPointF(a, b);
            subpathStart = current;
            path.moveTo(current);
            break;
        case u'L':
            if (!reader.readNumber(&a) || !reader.readNumber(&b)) { good = false; break; }
            current = base + QPointF(a, b);
            path.lineTo(current);
            break;
        case u'H':
            if (!reader.readNumber(&a)) { good = false; break; }
            current.setX((relative ? current.x() : 0) + a);
            path.lineTo(current);
            break;
        case u'V':
            if (!reader.readNumber(&a)) { good = false; break; }
            current.setY((relative ? current.y() : 0) + a);
            path.lineTo(current);
            break;
        case u'C': {
            if (!reader.readNumber(&a) || !reader.readNumber(&b) || !reader.readNumber(&e)
                || !reader.readNumber(&f) || !reader.readNumber(&g) || !reader.readNumber(&h)) { good = false; break; }
            const QPointF c1 = base + QPointF(a, b);
            const QPointF c2 = base + QPointF(e, f);
            current = base + QPointF(g, h);
            path.cubicTo(c1, c2, current);
            lastControl = c2;
            cubic = true;
            break;
        }
        case u'S': {
            if (!reader.readNumber(&a) || !reader.readNumber(&b) || !reader.readNumber(&e) || !reader.readNumber(&f)) { good = false; break; }
            const QPointF c1 = lastWasCubic ? current * 2 - lastControl : current;
            const QPointF c2 = base + QPointF(a, b);
            current = base + QPointF(e, f);
            path.cubicTo(c1, c2, current);
            lastControl = c2;
            cubic = true;
            break;
        }
        case u'Q': {
            if (!reader.readNumber(&a) || !reader.readNumber(&b) || !reader.readNumber(&e) || !reader.readNumber(&f)) { good = false; break; }
            const QPointF c1 = base + QPointF(a, b);
            current = base + QPointF(e, f);
            path.quadTo(c1, current);
            lastControl = c1;
            quad = true;
            break;
        }
        case u'T': {
            if (!reader.readNumber(&a) || !reader.readNumber(&b)) { good = false; break; }
            const QPointF c1 = lastWasQuad ? current * 2 - lastControl : current;
            current = base + QPointF(a, b);
            path.quadTo(c1, current);
            lastControl = c1;
            quad = true;
            break;
        }
        case u'A': {
            bool large = false;
            bool sweep = false;
            if (!reader.readNumber(&a) || !reader.readNumber(&b) || !reader.readNumber(&e)
                || !reader.readFlag(&large) || !reader.readFlag(&sweep)
                || !reader.readNumber(&g) || !reader.readNumber(&h)) { good = false; break; }
            const QPointF to = base + QPointF(g, h);
            arcToBeziers(path, current, a, b, e, large, sweep, to);
            current = to;
            break;
        }
        case u'Z':
            path.closeSubpath();
            current = subpathStart;
            break;
        default:
            good = false;
            break;
        }
        if (!good) {
            break;
        }
        lastWasCubic = cubic;
        lastWasQuad = quad;
    }
    if (ok) {
        *ok = good;
    }
    return path;
}

} // namespace QindaTK
