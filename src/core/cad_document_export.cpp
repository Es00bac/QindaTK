// SPDX-License-Identifier: LGPL-3.0-or-later
#include "cad_document.h"

#include <QSaveFile>
#include <algorithm>
#include <cmath>
#include <limits>

namespace QindaTK {
namespace {
constexpr double kPi = 3.14159265358979323846;
QString number(double value) { return QString::number(value == 0 ? 0 : value, 'g', 17); }
double normalized(double degrees)
{
    return std::fmod(std::fmod(degrees, 360.0) + 360.0, 360.0);
}
bool writeAtomic(const QString &path, const QByteArray &bytes)
{
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}
}

bool CadDocument::exportSvg(const QString &path)
{
    if (path.isEmpty() || m_state.points.isEmpty()) {
        setError(QStringLiteral("SVG export needs a path and nonempty sketch")); return false;
    }
    double left = std::numeric_limits<double>::infinity();
    double top = left, right = -left, bottom = -left;
    auto include = [&](double x, double y) {
        left = std::min(left, x); right = std::max(right, x);
        top = std::min(top, y); bottom = std::max(bottom, y);
    };
    for (const Point &p : m_state.points) include(p.x, p.y);
    for (const Circle &c : m_state.circles) {
        const Point &p = m_state.points.at(pointIndex(m_state, c.center));
        include(p.x - c.radius, p.y - c.radius);
        include(p.x + c.radius, p.y + c.radius);
    }
    for (const Arc &arc : m_state.arcs) {
        const Point &p = m_state.points.at(pointIndex(m_state, arc.center));
        auto includeAngle = [&](double degrees) {
            const double radians = degrees * kPi / 180.0;
            include(p.x + arc.radius * std::cos(radians),
                    p.y + arc.radius * std::sin(radians));
        };
        includeAngle(arc.start);
        includeAngle(arc.start + arc.sweep);
        for (double cardinal : {0.0, 90.0, 180.0, 270.0})
            if (normalized(cardinal - arc.start) <= arc.sweep) includeAngle(cardinal);
    }
    left -= 1; top -= 1; right += 1; bottom += 1;
    const QString width = number(std::max(1.0, right - left));
    const QString height = number(std::max(1.0, bottom - top));
    QString svg = QStringLiteral("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%1mm\" height=\"%2mm\" viewBox=\"%3 %4 %1 %2\">\n")
        .arg(width, height, number(left), number(top));
    svg += QStringLiteral("<g fill=\"none\" stroke=\"black\" stroke-width=\"0.25\">\n");
    for (const Segment &s : m_state.segments) {
        const Point &a = m_state.points.at(pointIndex(m_state, s.start));
        const Point &b = m_state.points.at(pointIndex(m_state, s.end));
        svg += QStringLiteral("<line x1=\"%1\" y1=\"%2\" x2=\"%3\" y2=\"%4\"/>\n")
            .arg(number(a.x), number(a.y), number(b.x), number(b.y));
    }
    for (const Circle &c : m_state.circles) {
        const Point &p = m_state.points.at(pointIndex(m_state, c.center));
        svg += QStringLiteral("<circle cx=\"%1\" cy=\"%2\" r=\"%3\"/>\n")
            .arg(number(p.x), number(p.y), number(c.radius));
    }
    for (const Arc &arc : m_state.arcs) {
        const Point &p = m_state.points.at(pointIndex(m_state, arc.center));
        const double start = arc.start * kPi / 180.0;
        const double end = (arc.start + arc.sweep) * kPi / 180.0;
        svg += QStringLiteral("<path d=\"M %1 %2 A %3 %3 0 %4 1 %5 %6\"/>\n")
            .arg(number(p.x + arc.radius * std::cos(start)),
                 number(p.y + arc.radius * std::sin(start)), number(arc.radius),
                 arc.sweep > 180 ? QStringLiteral("1") : QStringLiteral("0"),
                 number(p.x + arc.radius * std::cos(end)),
                 number(p.y + arc.radius * std::sin(end)));
    }
    svg += QStringLiteral("</g>\n<g fill=\"black\">\n");
    for (const Point &p : m_state.points)
        svg += QStringLiteral("<circle cx=\"%1\" cy=\"%2\" r=\"0.5\"/>\n")
            .arg(number(p.x), number(p.y));
    svg += QStringLiteral("</g>\n</svg>\n");
    if (!writeAtomic(path, svg.toUtf8())) {
        setError(QStringLiteral("Could not export SVG")); return false;
    }
    setError({}); return true;
}

bool CadDocument::exportDxf(const QString &path)
{
    if (path.isEmpty() || m_state.points.isEmpty()) {
        setError(QStringLiteral("DXF export needs a path and nonempty sketch")); return false;
    }
    QString dxf = QStringLiteral("0\nSECTION\n2\nHEADER\n9\n$ACADVER\n1\nAC1015\n9\n$INSUNITS\n70\n4\n0\nENDSEC\n0\nSECTION\n2\nENTITIES\n");
    auto pair = [&](int code, double value) { dxf += QString::number(code) + QChar('\n') + number(value) + QChar('\n'); };
    auto entity = [&](const QString &kind) { dxf += QStringLiteral("0\n") + kind + QChar('\n'); };
    for (const Point &p : m_state.points) {
        entity(QStringLiteral("POINT")); pair(10, p.x); pair(20, -p.y); pair(30, 0);
    }
    for (const Segment &s : m_state.segments) {
        const Point &a = m_state.points.at(pointIndex(m_state, s.start));
        const Point &b = m_state.points.at(pointIndex(m_state, s.end));
        entity(QStringLiteral("LINE"));
        pair(10, a.x); pair(20, -a.y); pair(30, 0);
        pair(11, b.x); pair(21, -b.y); pair(31, 0);
    }
    for (const Circle &c : m_state.circles) {
        const Point &p = m_state.points.at(pointIndex(m_state, c.center));
        entity(QStringLiteral("CIRCLE"));
        pair(10, p.x); pair(20, -p.y); pair(30, 0); pair(40, c.radius);
    }
    for (const Arc &arc : m_state.arcs) {
        const Point &p = m_state.points.at(pointIndex(m_state, arc.center));
        entity(QStringLiteral("ARC"));
        pair(10, p.x); pair(20, -p.y); pair(30, 0); pair(40, arc.radius);
        // DXF arcs are counterclockwise in Y-up coordinates. Reversing the
        // endpoints preserves the Y-down sketch locus after negating Y.
        pair(50, normalized(-arc.start - arc.sweep));
        pair(51, normalized(-arc.start));
    }
    dxf += QStringLiteral("0\nENDSEC\n0\nEOF\n");
    if (!writeAtomic(path, dxf.toUtf8())) {
        setError(QStringLiteral("Could not export DXF")); return false;
    }
    setError({}); return true;
}

} // namespace QindaTK
