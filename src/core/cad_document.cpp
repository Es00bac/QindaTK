// SPDX-License-Identifier: LGPL-3.0-or-later
#include "cad_document.h"

#include <QSet>
#include <QUuid>
#include <cmath>

namespace QindaTK {
namespace {
constexpr double kMaxLength = 1000000.0;
constexpr double kMinimumLength = 1e-9;
constexpr double kPi = 3.14159265358979323846;

bool validNumber(double value) { return std::isfinite(value) && std::abs(value) <= kMaxLength; }
QString freshId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
double length(double dx, double dy) { return std::hypot(dx, dy); }
}

CadDocument::CadDocument(QObject *parent) : QObject(parent) {}

void CadDocument::setError(const QString &message)
{
    if (m_error == message) return;
    m_error = message;
    emit errorChanged();
}

void CadDocument::rememberEdit()
{
    ++m_revision;
    // AGENT-GUARD (D-283): inside a transaction only the first edit records
    // the pre-transaction state; recording each edit would split one tool
    // gesture into several undo steps.
    if (m_transactionDepth > 0) {
        if (m_transactionRecorded) return;
        m_transactionRecorded = true;
    }
    m_undo.append(m_state);
    if (m_undo.size() > 128) m_undo.removeFirst();
    m_redo.clear();
}

int CadDocument::pointIndex(const State &state, const QString &id) const
{
    for (int i = 0; i < state.points.size(); ++i)
        if (state.points.at(i).id == id) return i;
    return -1;
}

int CadDocument::segmentIndex(const State &state, const QString &id) const
{
    for (int i = 0; i < state.segments.size(); ++i)
        if (state.segments.at(i).id == id) return i;
    return -1;
}

int CadDocument::circleIndex(const State &state, const QString &id) const
{
    for (int i = 0; i < state.circles.size(); ++i)
        if (state.circles.at(i).id == id) return i;
    return -1;
}

int CadDocument::arcIndex(const State &state, const QString &id) const
{
    for (int i = 0; i < state.arcs.size(); ++i)
        if (state.arcs.at(i).id == id) return i;
    return -1;
}

int CadDocument::constraintIndex(const State &state, const QString &id) const
{
    for (int i = 0; i < state.constraints.size(); ++i)
        if (state.constraints.at(i).id == id) return i;
    return -1;
}

double CadDocument::unitScale() const { return m_state.units == QLatin1String("in") ? 25.4 : 1.0; }

QVariantList CadDocument::points() const
{
    QVariantList result;
    const double scale = unitScale();
    for (const Point &p : m_state.points)
        result.append(QVariantMap{{QStringLiteral("entityId"), p.id},
            {QStringLiteral("x"), p.x / scale}, {QStringLiteral("y"), p.y / scale}});
    return result;
}

QVariantList CadDocument::segments() const
{
    QVariantList result;
    const double scale = unitScale();
    for (const Segment &s : m_state.segments) {
        const Point &a = m_state.points.at(pointIndex(m_state, s.start));
        const Point &b = m_state.points.at(pointIndex(m_state, s.end));
        QString distanceId, angleId;
        double distanceValue = 0, angleValue = 0;
        for (const Constraint &c : m_state.constraints) {
            if (c.kind == QLatin1String("distance")
                && ((c.first == s.start && c.second == s.end)
                    || (c.first == s.end && c.second == s.start))) {
                distanceId = c.id; distanceValue = c.value / scale;
            } else if (c.kind == QLatin1String("angle") && c.first == s.id) {
                angleId = c.id; angleValue = c.value;
            }
        }
        result.append(QVariantMap{{QStringLiteral("entityId"), s.id},
            {QStringLiteral("startPointId"), s.start}, {QStringLiteral("endPointId"), s.end},
            {QStringLiteral("x1"), a.x / scale}, {QStringLiteral("y1"), a.y / scale},
            {QStringLiteral("x2"), b.x / scale}, {QStringLiteral("y2"), b.y / scale},
            {QStringLiteral("length"), length(b.x - a.x, b.y - a.y) / scale},
            {QStringLiteral("distanceConstraintId"), distanceId},
            {QStringLiteral("distanceValue"), distanceValue},
            {QStringLiteral("angleConstraintId"), angleId},
            {QStringLiteral("angleValue"), angleValue}});
    }
    return result;
}

QVariantList CadDocument::circles() const
{
    QVariantList result;
    const double scale = unitScale();
    for (const Circle &c : m_state.circles) {
        const Point &p = m_state.points.at(pointIndex(m_state, c.center));
        result.append(QVariantMap{{QStringLiteral("entityId"), c.id},
            {QStringLiteral("centerPointId"), c.center}, {QStringLiteral("x"), p.x / scale},
            {QStringLiteral("y"), p.y / scale}, {QStringLiteral("radius"), c.radius / scale}});
    }
    return result;
}

QVariantList CadDocument::arcs() const
{
    QVariantList result;
    const double scale = unitScale();
    for (const Arc &arc : m_state.arcs) {
        const Point &p = m_state.points.at(pointIndex(m_state, arc.center));
        result.append(QVariantMap{{QStringLiteral("entityId"), arc.id},
            {QStringLiteral("centerPointId"), arc.center},
            {QStringLiteral("x"), p.x / scale}, {QStringLiteral("y"), p.y / scale},
            {QStringLiteral("radius"), arc.radius / scale},
            {QStringLiteral("startDegrees"), arc.start},
            {QStringLiteral("sweepDegrees"), arc.sweep},
            {QStringLiteral("length"), arc.radius * arc.sweep * kPi / 180.0 / scale}});
    }
    return result;
}

QVariantList CadDocument::constraints() const
{
    QVariantList result;
    const double scale = unitScale();
    for (const Constraint &c : m_state.constraints)
        result.append(QVariantMap{{QStringLiteral("constraintId"), c.id},
            {QStringLiteral("kind"), c.kind}, {QStringLiteral("firstId"), c.first},
            {QStringLiteral("secondId"), c.second},
            {QStringLiteral("value"), c.kind == QLatin1String("distance") ? c.value / scale : c.value}});
    return result;
}

QString CadDocument::addPoint(double x, double y)
{
    x *= unitScale(); y *= unitScale();
    if (!validNumber(x) || !validNumber(y) || m_state.points.size() >= 1000) {
        setError(QStringLiteral("Invalid or out-of-range point")); return {};
    }
    const QString id = freshId();
    rememberEdit();
    m_state.points.append({id, x, y});
    setError({}); emit changed();
    return id;
}

QString CadDocument::addSegment(const QString &startPointId, const QString &endPointId)
{
    const int a = pointIndex(m_state, startPointId), b = pointIndex(m_state, endPointId);
    if (a < 0 || b < 0 || a == b || m_state.segments.size() >= 1500) {
        setError(QStringLiteral("Segment needs two distinct points")); return {};
    }
    if (length(m_state.points.at(a).x - m_state.points.at(b).x,
               m_state.points.at(a).y - m_state.points.at(b).y) <= kMinimumLength) {
        setError(QStringLiteral("Degenerate zero-length segment")); return {};
    }
    const QString id = freshId();
    rememberEdit(); m_state.segments.append({id, startPointId, endPointId});
    setError({}); emit changed();
    return id;
}

QString CadDocument::addCircle(const QString &centerPointId, double radius)
{
    radius *= unitScale();
    if (pointIndex(m_state, centerPointId) < 0 || !validNumber(radius)
        || radius <= kMinimumLength || m_state.circles.size() >= 1000) {
        setError(QStringLiteral("Circle needs a center and positive radius")); return {};
    }
    const QString id = freshId();
    rememberEdit(); m_state.circles.append({id, centerPointId, radius});
    setError({}); emit changed();
    return id;
}

QString CadDocument::addArc(const QString &centerPointId, double radius,
                            double startDegrees, double sweepDegrees)
{
    radius *= unitScale();
    if (pointIndex(m_state, centerPointId) < 0 || !validNumber(radius)
        || radius <= kMinimumLength || !validNumber(startDegrees)
        || !std::isfinite(sweepDegrees) || sweepDegrees <= 0 || sweepDegrees >= 360
        || m_state.arcs.size() >= 1000) {
        setError(QStringLiteral("Arc needs a center, positive radius and sweep below 360 degrees")); return {};
    }
    const QString id = freshId();
    const double start = std::fmod(std::fmod(startDegrees, 360.0) + 360.0, 360.0);
    rememberEdit(); m_state.arcs.append({id, centerPointId, radius, start, sweepDegrees});
    setError({}); emit changed();
    return id;
}

QString CadDocument::addConstraint(const QString &kind, const QString &firstId,
                                    const QString &secondId, double value)
{
    const bool lineKind = kind == QLatin1String("horizontal") || kind == QLatin1String("vertical")
        || kind == QLatin1String("angle");
    const bool pointKind = kind == QLatin1String("coincident") || kind == QLatin1String("distance");
    const bool tangentKind = kind == QLatin1String("tangent");
    const double canonical = kind == QLatin1String("distance") ? value * unitScale()
        : kind == QLatin1String("angle") ? std::remainder(value, 360.0) : 0;
    if ((!lineKind && !pointKind && !tangentKind) || m_state.constraints.size() >= 1000
        || (lineKind && (segmentIndex(m_state, firstId) < 0 || !secondId.isEmpty()))
        || (pointKind && (pointIndex(m_state, firstId) < 0
                          || pointIndex(m_state, secondId) < 0 || firstId == secondId))
        || (tangentKind && (segmentIndex(m_state, firstId) < 0
                            || circleIndex(m_state, secondId) < 0))
        || (kind == QLatin1String("distance")
            && (!validNumber(canonical) || canonical < 0))
        || (kind == QLatin1String("angle") && !validNumber(value))) {
        setError(QStringLiteral("Invalid CAD constraint or references")); return {};
    }
    State candidate = m_state;
    const QString id = freshId();
    candidate.constraints.append({id, kind, firstId, secondId, canonical});
    QString diagnostic;
    if (!solve(&candidate, {}, &diagnostic)) { setError(diagnostic); return {}; }
    rememberEdit(); m_state = std::move(candidate);
    setError({}); emit changed();
    return id;
}

bool CadDocument::movePoint(const QString &pointId, double x, double y)
{
    x *= unitScale(); y *= unitScale();
    const int index = pointIndex(m_state, pointId);
    if (index < 0 || !validNumber(x) || !validNumber(y)) {
        setError(QStringLiteral("Invalid CAD point move")); return false;
    }
    if (m_state.points.at(index).x == x && m_state.points.at(index).y == y) return true;
    State candidate = m_state;
    candidate.points[index].x = x; candidate.points[index].y = y;
    QString diagnostic;
    if (!solve(&candidate, pointId, &diagnostic)) { setError(diagnostic); return false; }
    rememberEdit(); m_state = std::move(candidate);
    setError({}); emit changed();
    return true;
}

bool CadDocument::setCircleRadius(const QString &circleId, double radius)
{
    radius *= unitScale();
    const int index = circleIndex(m_state, circleId);
    if (index < 0 || !validNumber(radius) || radius <= kMinimumLength) {
        setError(QStringLiteral("Invalid CAD circle radius")); return false;
    }
    if (m_state.circles.at(index).radius == radius) return true;
    State candidate = m_state;
    candidate.circles[index].radius = radius;
    QString diagnostic;
    if (!solve(&candidate, {}, &diagnostic)) { setError(diagnostic); return false; }
    rememberEdit(); m_state = std::move(candidate);
    setError({}); emit changed();
    return true;
}

bool CadDocument::setArc(const QString &arcId, double radius,
                          double startDegrees, double sweepDegrees)
{
    radius *= unitScale();
    const int index = arcIndex(m_state, arcId);
    if (index < 0 || !validNumber(radius) || radius <= kMinimumLength
        || !validNumber(startDegrees) || !std::isfinite(sweepDegrees)
        || sweepDegrees <= 0 || sweepDegrees >= 360) {
        setError(QStringLiteral("Invalid CAD arc geometry")); return false;
    }
    const double start = std::fmod(std::fmod(startDegrees, 360.0) + 360.0, 360.0);
    const Arc &current = m_state.arcs.at(index);
    if (current.radius == radius && current.start == start && current.sweep == sweepDegrees) return true;
    rememberEdit();
    Arc &arc = m_state.arcs[index];
    arc.radius = radius; arc.start = start; arc.sweep = sweepDegrees;
    setError({}); emit changed();
    return true;
}

bool CadDocument::setDimension(const QString &constraintId, double value)
{
    const int index = constraintIndex(m_state, constraintId);
    if (index < 0) { setError(QStringLiteral("Unknown CAD dimension")); return false; }
    const Constraint &constraint = m_state.constraints.at(index);
    if (constraint.kind != QLatin1String("distance") && constraint.kind != QLatin1String("angle")) {
        setError(QStringLiteral("Constraint is not an editable dimension")); return false;
    }
    const double canonical = constraint.kind == QLatin1String("distance") ? value * unitScale()
        : std::remainder(value, 360.0);
    if (!validNumber(value) || !validNumber(canonical)
        || (constraint.kind == QLatin1String("distance") && canonical < 0)) {
        setError(QStringLiteral("Invalid CAD dimension value")); return false;
    }
    if (constraint.value == canonical) return true;
    State candidate = m_state;
    candidate.constraints[index].value = canonical;
    QString diagnostic;
    if (!solve(&candidate, {}, &diagnostic)) { setError(diagnostic); return false; }
    rememberEdit(); m_state = std::move(candidate);
    setError({}); emit changed();
    return true;
}

bool CadDocument::removeEntity(const QString &entityId)
{
    if (constraintIndex(m_state, entityId) >= 0) return removeConstraint(entityId);
    const int p = pointIndex(m_state, entityId);
    const int s = segmentIndex(m_state, entityId);
    const int c = circleIndex(m_state, entityId);
    const int arcIndexValue = arcIndex(m_state, entityId);
    if (p < 0 && s < 0 && c < 0 && arcIndexValue < 0) {
        setError(QStringLiteral("Unknown CAD entity")); return false;
    }
    State candidate = m_state;
    QSet<QString> removed{entityId};
    if (p >= 0) {
        candidate.points.removeAt(p);
        candidate.segments.removeIf([&](const Segment &segment) {
            if (segment.start != entityId && segment.end != entityId) return false;
            removed.insert(segment.id); return true;
        });
        candidate.circles.removeIf([&](const Circle &circle) {
            if (circle.center != entityId) return false;
            removed.insert(circle.id); return true;
        });
        candidate.arcs.removeIf([&](const Arc &arc) { return arc.center == entityId; });
    } else if (s >= 0) candidate.segments.removeAt(s);
    else if (c >= 0) candidate.circles.removeAt(c);
    else candidate.arcs.removeAt(arcIndexValue);
    candidate.constraints.removeIf([&](const Constraint &constraint) {
        return removed.contains(constraint.first) || removed.contains(constraint.second);
    });
    rememberEdit(); m_state = std::move(candidate);
    setError({}); emit changed();
    return true;
}

bool CadDocument::changeUnits(const QString &units)
{
    if (units != QLatin1String("mm") && units != QLatin1String("in")) {
        setError(QStringLiteral("Units must be mm or in")); return false;
    }
    if (m_state.units == units) return true;
    rememberEdit(); m_state.units = units;
    setError({}); emit changed();
    return true;
}

QVariantMap CadDocument::measure(const QString &entityId) const
{
    const double scale = unitScale();
    const int p = pointIndex(m_state, entityId);
    if (p >= 0) return {{QStringLiteral("kind"), QStringLiteral("point")},
        {QStringLiteral("x"), m_state.points.at(p).x / scale},
        {QStringLiteral("y"), m_state.points.at(p).y / scale},
        {QStringLiteral("units"), m_state.units}};
    const int s = segmentIndex(m_state, entityId);
    if (s >= 0) {
        const Segment &segment = m_state.segments.at(s);
        const Point &a = m_state.points.at(pointIndex(m_state, segment.start));
        const Point &b = m_state.points.at(pointIndex(m_state, segment.end));
        return {{QStringLiteral("kind"), QStringLiteral("segment")},
            {QStringLiteral("length"), length(b.x - a.x, b.y - a.y) / scale},
            {QStringLiteral("angleDegrees"), std::atan2(b.y - a.y, b.x - a.x) * 180.0 / kPi},
            {QStringLiteral("units"), m_state.units}};
    }
    const int c = circleIndex(m_state, entityId);
    if (c >= 0) {
        const double radius = m_state.circles.at(c).radius / scale;
        return {{QStringLiteral("kind"), QStringLiteral("circle")},
            {QStringLiteral("radius"), radius}, {QStringLiteral("diameter"), radius * 2},
            {QStringLiteral("area"), kPi * radius * radius},
            {QStringLiteral("units"), m_state.units}};
    }
    const int arcIndexValue = arcIndex(m_state, entityId);
    if (arcIndexValue >= 0) {
        const Arc &arc = m_state.arcs.at(arcIndexValue);
        return {{QStringLiteral("kind"), QStringLiteral("arc")},
            {QStringLiteral("radius"), arc.radius / scale},
            {QStringLiteral("sweepDegrees"), arc.sweep},
            {QStringLiteral("length"), arc.radius * arc.sweep * kPi / 180.0 / scale},
            {QStringLiteral("units"), m_state.units}};
    }
    return {};
}

QVariantMap CadDocument::snap(double x, double y, double tolerance,
                              double gridStep, const QString &excludePointId) const
{
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(tolerance)
        || tolerance < 0 || !std::isfinite(gridStep) || gridStep < 0) return {};
    double best = tolerance;
    QVariantMap result{{QStringLiteral("x"), x}, {QStringLiteral("y"), y},
                       {QStringLiteral("kind"), QStringLiteral("free")}};
    const double scale = unitScale();
    for (const Point &point : m_state.points) {
        if (point.id == excludePointId) continue;
        const double px = point.x / scale, py = point.y / scale;
        const double distance = length(px - x, py - y);
        if (distance > best) continue;
        best = distance;
        result = {{QStringLiteral("x"), px}, {QStringLiteral("y"), py},
                  {QStringLiteral("kind"), QStringLiteral("point")},
                  {QStringLiteral("pointId"), point.id}};
    }
    if (result.value(QStringLiteral("kind")) == QLatin1String("free") && gridStep > 0) {
        const double gx = std::round(x / gridStep) * gridStep;
        const double gy = std::round(y / gridStep) * gridStep;
        if (length(gx - x, gy - y) <= tolerance)
            result = {{QStringLiteral("x"), gx}, {QStringLiteral("y"), gy},
                      {QStringLiteral("kind"), QStringLiteral("grid")}};
    }
    return result;
}

bool CadDocument::undo()
{
    if (m_transactionDepth > 0) { setError(QStringLiteral("Finish the open CAD edit first")); return false; }
    if (m_undo.isEmpty()) return false;
    m_redo.append(m_state); m_state = m_undo.takeLast(); ++m_revision;
    setError({}); emit changed(); return true;
}

bool CadDocument::redo()
{
    if (m_transactionDepth > 0) { setError(QStringLiteral("Finish the open CAD edit first")); return false; }
    if (m_redo.isEmpty()) return false;
    m_undo.append(m_state); m_state = m_redo.takeLast(); ++m_revision;
    setError({}); emit changed(); return true;
}

} // namespace QindaTK
