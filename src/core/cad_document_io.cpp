// SPDX-License-Identifier: LGPL-3.0-or-later
#include "cad_document.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <cmath>
#include <utility>

namespace QindaTK {

bool CadDocument::saveToFile(const QString &path)
{
    if (path.isEmpty()) { setError(QStringLiteral("Missing CAD path")); return false; }
    QJsonArray points, segments, circles, arcs, constraints;
    for (const Point &p : m_state.points)
        points.append(QJsonObject{{QStringLiteral("id"), p.id},
            {QStringLiteral("xMm"), p.x}, {QStringLiteral("yMm"), p.y}});
    for (const Segment &s : m_state.segments)
        segments.append(QJsonObject{{QStringLiteral("id"), s.id},
            {QStringLiteral("start"), s.start}, {QStringLiteral("end"), s.end}});
    for (const Circle &c : m_state.circles)
        circles.append(QJsonObject{{QStringLiteral("id"), c.id},
            {QStringLiteral("center"), c.center}, {QStringLiteral("radiusMm"), c.radius}});
    for (const Arc &arc : m_state.arcs)
        arcs.append(QJsonObject{{QStringLiteral("id"), arc.id},
            {QStringLiteral("center"), arc.center}, {QStringLiteral("radiusMm"), arc.radius},
            {QStringLiteral("startDegrees"), arc.start},
            {QStringLiteral("sweepDegrees"), arc.sweep}});
    for (const Constraint &c : m_state.constraints)
        constraints.append(QJsonObject{{QStringLiteral("id"), c.id},
            {QStringLiteral("kind"), c.kind}, {QStringLiteral("first"), c.first},
            {QStringLiteral("second"), c.second}, {QStringLiteral("valueMm"), c.value}});
    const QByteArray bytes = QJsonDocument(QJsonObject{
        {QStringLiteral("format"), QStringLiteral("qindatk.cad/1")},
        {QStringLiteral("version"), 1}, {QStringLiteral("units"), m_state.units},
        {QStringLiteral("points"), points}, {QStringLiteral("segments"), segments},
        {QStringLiteral("circles"), circles}, {QStringLiteral("arcs"), arcs},
        {QStringLiteral("constraints"), constraints}
    }).toJson();
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        setError(QStringLiteral("Could not save CAD sketch")); return false;
    }
    setError({}); return true;
}

bool CadDocument::decode(const QByteArray &bytes, State *result, QString *diagnostic) const
{
    QJsonParseError parseError;
    const QJsonDocument parsed = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !parsed.isObject()) {
        *diagnostic = QStringLiteral("Invalid CAD JSON"); return false;
    }
    const QJsonObject root = parsed.object();
    const QString units = root.value(QStringLiteral("units")).toString();
    if (root.value(QStringLiteral("format")).toString() != QLatin1String("qindatk.cad/1")
        || root.value(QStringLiteral("version")).toInt() != 1
        || (units != QLatin1String("mm") && units != QLatin1String("in"))
        || !root.value(QStringLiteral("points")).isArray()
        || !root.value(QStringLiteral("segments")).isArray()
        || !root.value(QStringLiteral("circles")).isArray()
        || (!root.value(QStringLiteral("arcs")).isUndefined()
            && !root.value(QStringLiteral("arcs")).isArray())
        || !root.value(QStringLiteral("constraints")).isArray()) {
        *diagnostic = QStringLiteral("Unsupported CAD sketch format"); return false;
    }
    const auto points = root.value(QStringLiteral("points")).toArray();
    const auto segments = root.value(QStringLiteral("segments")).toArray();
    const auto circles = root.value(QStringLiteral("circles")).toArray();
    const auto arcs = root.value(QStringLiteral("arcs")).toArray();
    const auto constraints = root.value(QStringLiteral("constraints")).toArray();
    if (points.size() > 1000 || segments.size() > 1500 || circles.size() > 1000
        || arcs.size() > 1000
        || constraints.size() > 1000) {
        *diagnostic = QStringLiteral("CAD sketch exceeds entity limits"); return false;
    }
    result->units = units;
    QSet<QString> identities;
    auto uniqueId = [&](const QJsonObject &record) {
        const QString id = record.value(QStringLiteral("id")).toString();
        if (id.isEmpty() || identities.contains(id)) return QString();
        identities.insert(id); return id;
    };
    for (const QJsonValue &value : points) {
        if (!value.isObject()) return false;
        const QJsonObject p = value.toObject();
        const QString id = uniqueId(p);
        if (id.isEmpty() || !p.value(QStringLiteral("xMm")).isDouble()
            || !p.value(QStringLiteral("yMm")).isDouble()) return false;
        result->points.append({id, p.value(QStringLiteral("xMm")).toDouble(),
                              p.value(QStringLiteral("yMm")).toDouble()});
    }
    for (const QJsonValue &value : segments) {
        if (!value.isObject()) return false;
        const QJsonObject s = value.toObject();
        const QString id = uniqueId(s);
        const QString start = s.value(QStringLiteral("start")).toString();
        const QString end = s.value(QStringLiteral("end")).toString();
        if (id.isEmpty() || start == end || pointIndex(*result, start) < 0
            || pointIndex(*result, end) < 0) return false;
        result->segments.append({id, start, end});
    }
    for (const QJsonValue &value : circles) {
        if (!value.isObject()) return false;
        const QJsonObject c = value.toObject();
        const QString id = uniqueId(c);
        const QString center = c.value(QStringLiteral("center")).toString();
        if (id.isEmpty() || pointIndex(*result, center) < 0
            || !c.value(QStringLiteral("radiusMm")).isDouble()) return false;
        result->circles.append({id, center, c.value(QStringLiteral("radiusMm")).toDouble()});
    }
    for (const QJsonValue &value : arcs) {
        if (!value.isObject()) return false;
        const QJsonObject a = value.toObject();
        const QString id = uniqueId(a);
        const QString center = a.value(QStringLiteral("center")).toString();
        if (id.isEmpty() || pointIndex(*result, center) < 0
            || !a.value(QStringLiteral("radiusMm")).isDouble()
            || !a.value(QStringLiteral("startDegrees")).isDouble()
            || !a.value(QStringLiteral("sweepDegrees")).isDouble()) return false;
        result->arcs.append({id, center, a.value(QStringLiteral("radiusMm")).toDouble(),
            a.value(QStringLiteral("startDegrees")).toDouble(),
            a.value(QStringLiteral("sweepDegrees")).toDouble()});
    }
    for (const QJsonValue &value : constraints) {
        if (!value.isObject()) return false;
        const QJsonObject c = value.toObject();
        const QString id = uniqueId(c);
        const QString kind = c.value(QStringLiteral("kind")).toString();
        const QString first = c.value(QStringLiteral("first")).toString();
        const QString second = c.value(QStringLiteral("second")).toString();
        const bool lineKind = kind == QLatin1String("horizontal") || kind == QLatin1String("vertical")
            || kind == QLatin1String("angle");
        const bool pointKind = kind == QLatin1String("coincident") || kind == QLatin1String("distance");
        const bool tangentKind = kind == QLatin1String("tangent");
        const QJsonValue amount = c.value(QStringLiteral("valueMm"));
        if (id.isEmpty() || (!lineKind && !pointKind && !tangentKind) || !amount.isDouble()
            || !std::isfinite(amount.toDouble())
            || (kind == QLatin1String("distance") && amount.toDouble() < 0)
            || (kind == QLatin1String("angle") && std::abs(amount.toDouble()) > 180)
            || (lineKind && (segmentIndex(*result, first) < 0 || !second.isEmpty()))
            || (pointKind && (pointIndex(*result, first) < 0
                              || pointIndex(*result, second) < 0 || first == second))
            || (tangentKind && (segmentIndex(*result, first) < 0
                                || circleIndex(*result, second) < 0))) return false;
        result->constraints.append({id, kind, first, second, amount.toDouble()});
    }
    if (!validGeometry(*result, diagnostic)) return false;
    State projected = *result;
    if (!solve(&projected, {}, diagnostic)) return false;
    for (int i = 0; i < result->points.size(); ++i) {
        const Point &original = result->points.at(i), &candidate = projected.points.at(i);
        if (std::hypot(original.x - candidate.x, original.y - candidate.y) > 1e-6) {
            *diagnostic = QStringLiteral("CAD file violates saved constraints"); return false;
        }
    }
    return true;
}

bool CadDocument::loadFromFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 4 * 1024 * 1024) {
        setError(QStringLiteral("Could not open CAD sketch")); return false;
    }
    State decoded;
    QString diagnostic;
    if (!decode(file.readAll(), &decoded, &diagnostic)) {
        setError(diagnostic.isEmpty() ? QStringLiteral("Invalid CAD sketch") : diagnostic);
        return false;
    }
    m_state = std::move(decoded);
    m_undo.clear(); m_redo.clear(); ++m_revision;
    setError({}); emit changed();
    return true;
}

} // namespace QindaTK
