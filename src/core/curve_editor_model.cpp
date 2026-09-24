// SPDX-License-Identifier: LGPL-3.0-or-later
#include "curve_editor_model.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonValue>
#include <cmath>
#include <limits>

namespace QindaTK {
namespace {
double number(const QVariant &value) { return value.isValid() && value.canConvert<double>() ? value.toDouble() : std::numeric_limits<double>::quiet_NaN(); }
QString interpolationName(CurveInterpolation i) { return i == CurveInterpolation::Step ? "step" : i == CurveInterpolation::CubicBezier ? "cubic" : "linear"; }
QString extrapolationName(CurveExtrapolation i) { return i == CurveExtrapolation::Linear ? "linear" : i == CurveExtrapolation::Repeat ? "repeat" : "hold"; }
QString dimensionName(UnitDimension d) {
    switch (d) { case UnitDimension::Length: return "length"; case UnitDimension::Angle: return "angle"; case UnitDimension::Duration: return "duration"; case UnitDimension::Frequency: return "frequency"; case UnitDimension::Gain: return "gain"; default: return "scalar"; }
}
QVariantMap axisMap(const CurveAxisDomain &a) { return {{"dimension", dimensionName(a.dimension)}, {"unitId", a.unitId}, {"minimum", a.minimum}, {"maximum", a.maximum}}; }
QVariantMap pointMap(const QPointF &p) { return {{"x", p.x()}, {"y", p.y()}}; }
QVariantMap knotMap(const CurveKnot &k) {
    QVariantMap m{{"id", k.id}, {"x", k.x}, {"y", k.y}, {"interpolation", interpolationName(k.interpolation)}};
    if (k.inHandle) m.insert("inHandle", pointMap(*k.inHandle));
    if (k.outHandle) m.insert("outHandle", pointMap(*k.outHandle));
    return m;
}
bool readAxis(const QVariantMap &m, CurveAxisDomain *a) {
    const QString d = m.value("dimension").toString();
    if (d == "scalar") a->dimension = UnitDimension::Scalar;
    else if (d == "length") a->dimension = UnitDimension::Length;
    else if (d == "angle") a->dimension = UnitDimension::Angle;
    else if (d == "duration") a->dimension = UnitDimension::Duration;
    else if (d == "frequency") a->dimension = UnitDimension::Frequency;
    else if (d == "gain") a->dimension = UnitDimension::Gain;
    else return false;
    a->unitId = m.value("unitId").toString(); a->minimum = number(m.value("minimum")); a->maximum = number(m.value("maximum"));
    return true;
}
}

CurveEditorModel::CurveEditorModel(QObject *parent) : QObject(parent)
{
    CurveDefinition initial; initial.id = QStringLiteral("transfer");
    initial.knots = {{QStringLiteral("low"), 0.0, 0.0, {}, QPointF(0.22, 0.0), CurveInterpolation::CubicBezier},
                     {QStringLiteral("mid"), 0.5, 0.42, QPointF(0.36, 0.42), QPointF(0.68, 0.42), CurveInterpolation::CubicBezier},
                     {QStringLiteral("high"), 1.0, 1.0, QPointF(0.82, 1.0), {}, CurveInterpolation::Linear}};
    m_document.defineProperty({QStringLiteral("curve"), QMetaType::fromType<QVariantMap>(), QVariant::fromValue(QVariantMap{}), {}, true, false});
    m_document.addEntity(m_entity, {{"curve", toMap(initial)}});
    m_undo.addDocument(&m_document);
    m_transaction.setDocument(&m_document); m_transaction.setUndoService(&m_undo);
    m_transaction.setTargetIds({m_entity.toString()}); m_transaction.setPropertyId("curve");
    m_transaction.setValidator([this](const QVector<PropertyChange> &changes) {
        if (changes.isEmpty()) return OperationError{};
        const auto parsed = fromMap(changes.last().after.toMap());
        if (!parsed) return OperationError{ErrorCode::Validation, "Malformed curve definition", "curve", "curve-cpu"};
        auto error = CurveEvaluator::validate(*parsed);
        return error ? error : (m_constraint ? m_constraint(*parsed) : OperationError{});
    });
    connect(&m_document, &PropertyDocument::changed, this, [this] { emit changed(); });
}

QVariantMap CurveEditorModel::currentMap() const { return m_document.value(m_entity, "curve").toMap(); }
QVariantMap CurveEditorModel::definition() const { return currentMap(); }
CurveDefinition CurveEditorModel::curve() const { return fromMap(currentMap()).value_or(CurveDefinition{}); }
QVariantList CurveEditorModel::knots() const { return currentMap().value("knots").toList(); }
QVariantList CurveEditorModel::samples() const {
    QVariantList out; const auto c = curve();
    if (CurveEvaluator::validate(c)) return out;
    constexpr int count = 129;
    for (int i = 0; i < count; ++i) { const double x = double(i) / (count - 1); const auto e = CurveEvaluator::evaluateNormalized(c, x); if (e.sample) out.append(QVariantMap{{"x", x}, {"y", e.sample->normalizedY}}); }
    return out;
}

QVariantMap CurveEditorModel::toMap(const CurveDefinition &c) {
    QVariantList ks; for (const auto &k : c.knots) ks.append(knotMap(k));
    return {{"schema", "qindatk.curve/1"}, {"id", c.id}, {"xDomain", axisMap(c.xDomain)}, {"yDomain", axisMap(c.yDomain)}, {"before", extrapolationName(c.before)}, {"after", extrapolationName(c.after)}, {"knots", ks}};
}
std::optional<CurveDefinition> CurveEditorModel::fromMap(const QVariantMap &m) {
    CurveDefinition c; c.id = m.value("id").toString();
    if (!readAxis(m.value("xDomain").toMap(), &c.xDomain) || !readAxis(m.value("yDomain").toMap(), &c.yDomain)) return {};
    auto extrap = [](const QString &s, CurveExtrapolation *v) { if (s == "hold") *v = CurveExtrapolation::Hold; else if (s == "linear") *v = CurveExtrapolation::Linear; else if (s == "repeat") *v = CurveExtrapolation::Repeat; else return false; return true; };
    if (!extrap(m.value("before").toString(), &c.before) || !extrap(m.value("after").toString(), &c.after)) return {};
    for (const auto &v : m.value("knots").toList()) {
        const auto kmap = v.toMap(); CurveKnot k; k.id = kmap.value("id").toString(); k.x = number(kmap.value("x")); k.y = number(kmap.value("y"));
        const QString i = kmap.value("interpolation").toString();
        if (i == "step") k.interpolation = CurveInterpolation::Step; else if (i == "linear") k.interpolation = CurveInterpolation::Linear; else if (i == "cubic") k.interpolation = CurveInterpolation::CubicBezier; else return {};
        if (kmap.contains("inHandle")) { auto p = kmap.value("inHandle").toMap(); k.inHandle = QPointF(number(p.value("x")), number(p.value("y"))); }
        if (kmap.contains("outHandle")) { auto p = kmap.value("outHandle").toMap(); k.outHandle = QPointF(number(p.value("x")), number(p.value("y"))); }
        c.knots.append(k);
    }
    return c;
}

bool CurveEditorModel::setCurve(const CurveDefinition &c) {
    auto error = CurveEvaluator::validate(c); if (error || (m_constraint && (error = m_constraint(c)))) { m_error = error.message; emit changed(); return false; }
    m_transaction.cancel(); m_document.resetModel({qMakePair(m_entity, QVariantMap{{"curve", toMap(c)}})}); m_error.clear(); return true;
}
void CurveEditorModel::setDomainConstraint(std::function<OperationError(const CurveDefinition &)> constraint) { m_constraint = std::move(constraint); }
bool CurveEditorModel::begin(const QString &id, const QString &label) {
    if (m_transaction.active()) return true;
    m_error.clear(); m_transaction.setLabel(label); m_transaction.setMergeKey({});
    const bool ok = m_transaction.begin({m_entity}, "curve", label); if (!ok) m_error = m_transaction.errorMessage();
    Q_UNUSED(id); return ok;
}
bool CurveEditorModel::beginKnotEdit(const QString &id) { return begin(id, "Move curve knot"); }
bool CurveEditorModel::beginTangentEdit(const QString &id, const QString &side) {
    if (side != "in" && side != "out") { m_error = "Tangent side must be in or out"; emit changed(); return false; }
    return begin(id, "Move curve tangent");
}
bool CurveEditorModel::applyPreview(const QVariantMap &m) {
    const auto c = fromMap(m); if (!c) return false;
    auto error = CurveEvaluator::validate(*c); if (!error && m_constraint) error = m_constraint(*c);
    if (error) { m_error = error.message; emit changed(); return false; }
    const auto result = m_transaction.updateValue(m); m_error = result.message; emit changed(); return !result;
}
bool CurveEditorModel::previewKnot(const QString &id, double x, double y) {
    if (!m_transaction.active() || !std::isfinite(x) || !std::isfinite(y)) return false;
    auto m = currentMap(); auto list = m.value("knots").toList();
    for (int i = 0; i < list.size(); ++i) { auto k = list[i].toMap(); if (k.value("id").toString() == id) { k["x"] = x; k["y"] = y; list[i] = k; m["knots"] = list; return applyPreview(m); } }
    return false;
}
bool CurveEditorModel::previewTangent(const QString &id, const QString &side, double x, double y) {
    if (!m_transaction.active() || (side != "in" && side != "out") || !std::isfinite(x) || !std::isfinite(y)) return false;
    auto m = currentMap(); auto list = m.value("knots").toList();
    for (int i = 0; i < list.size(); ++i) { auto k = list[i].toMap(); if (k.value("id").toString() == id) { k[side + "Handle"] = QVariantMap{{"x", x}, {"y", y}}; list[i] = k; m["knots"] = list; return applyPreview(m); } }
    return false;
}
bool CurveEditorModel::commitEdit() { const auto e = m_transaction.commitEdit(); m_error = e.message; emit changed(); return !e; }
void CurveEditorModel::cancelEdit() { m_transaction.cancel(); m_error.clear(); emit changed(); }
QByteArray CurveEditorModel::saveJson() const { return QJsonDocument::fromVariant(currentMap()).toJson(QJsonDocument::Compact); }
bool CurveEditorModel::loadJson(const QByteArray &json) {
    QJsonParseError parse; const auto doc = QJsonDocument::fromJson(json, &parse);
    if (parse.error != QJsonParseError::NoError || !doc.isObject() || doc.object().value("schema").toString() != "qindatk.curve/1") { m_error = "Invalid curve JSON"; emit changed(); return false; }
    const auto c = fromMap(doc.object().toVariantMap()); if (!c) { m_error = "Malformed curve JSON"; emit changed(); return false; }
    return setCurve(*c);
}
} // namespace QindaTK
