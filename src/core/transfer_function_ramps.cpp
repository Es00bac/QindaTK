// SPDX-License-Identifier: LGPL-3.0-or-later
// TransferFunctionModel settings, explicit out-of-range/missing colours,
// built-in presets and the exported ramp (EXT-09, D-262, D-263). Stop editing
// lives in transfer_function_model.cpp, history in transfer_function_history.cpp.
#include "transfer_function_model.h"
#include <algorithm>
#include <cmath>

namespace QindaTK {
namespace {
QColor opaqueColor(const QVariant &value)
{
    QColor color = value.metaType().id() == QMetaType::QString ? QColor::fromString(value.toString())
                                                               : value.value<QColor>();
    if (!color.isValid()) return {};
    color = color.toRgb();
    color.setAlpha(255);
    return color;
}
struct PresetStop { double position; const char *color; double opacity; };
struct Preset { const char *id; const char *name; int count; PresetStop stops[5]; };
// AGENT-NOTE: known ramps as data, not theme colours: exported settings must
// mean the same colours in every theme. "hot" and "viridis" follow their
// published anchor colours; tests compare exported samples at the anchors.
const Preset Presets[] = {
    {"grayscale", QT_TRANSLATE_NOOP("TransferFunctionModel", "Grayscale"), 2,
     {{0, "#000000", 0}, {1, "#ffffff", 1}}},
    {"hot", QT_TRANSLATE_NOOP("TransferFunctionModel", "Hot"), 4,
     {{0, "#000000", 0}, {0.375, "#ff0000", 0.375}, {0.75, "#ffff00", 0.75}, {1, "#ffffff", 1}}},
    {"viridis", QT_TRANSLATE_NOOP("TransferFunctionModel", "Viridis"), 5,
     {{0, "#440154", 0}, {0.25, "#3b528b", 0.25}, {0.5, "#21918c", 0.5}, {0.75, "#5ec962", 0.75},
      {1, "#fde725", 1}}},
    {"cool-warm", QT_TRANSLATE_NOOP("TransferFunctionModel", "Cool to warm"), 3,
     {{0, "#3b4cc0", 1}, {0.5, "#dddddd", 0.1}, {1, "#b40426", 1}}},
};
} // namespace

QVariantMap TransferFunctionModel::specialMap(const Special &special, const Point &end) const
{
    const bool clamp = !special.set;
    return {{QStringLiteral("mode"), clamp ? QStringLiteral("clamp") : QStringLiteral("color")},
            {QStringLiteral("color"), (clamp ? end.color : special.color).name(QColor::HexRgb)},
            {QStringLiteral("opacity"), clamp ? end.opacity : special.opacity}};
}
QVariantMap TransferFunctionModel::belowRange() const { return specialMap(m_below, m_points.first()); }
QVariantMap TransferFunctionModel::aboveRange() const { return specialMap(m_above, m_points.last()); }
QVariantMap TransferFunctionModel::missingValue() const { return specialMap(m_missing, m_points.first()); }

TransferFunctionModel::Special *TransferFunctionModel::specialNamed(State *state, const QString &which) const
{
    if (which == QLatin1String("below")) return &state->below;
    if (which == QLatin1String("above")) return &state->above;
    if (which == QLatin1String("missing")) return &state->missing;
    return nullptr;
}
bool TransferFunctionModel::setRangeColor(const QString &which, const QColor &color, double opacity)
{
    State next = state();
    Special *special = specialNamed(&next, which);
    const QColor rgb = opaqueColor(QVariant::fromValue(color));
    if (!special || !rgb.isValid() || !std::isfinite(opacity) || opacity < 0 || opacity > 1) {
        fail(QStringLiteral("Range must be below, above or missing with a valid color and 0–1 opacity"));
        return false;
    }
    *special = {true, rgb, opacity};
    return apply(std::move(next), which == QLatin1String("missing") ? tr("Change missing-value color")
                                                                     : tr("Change out-of-range color"));
}
bool TransferFunctionModel::clampRange(const QString &which)
{
    State next = state();
    Special *special = which == QLatin1String("missing") ? nullptr : specialNamed(&next, which);
    if (!special) { fail(QStringLiteral("Only below or above can clamp to the end stop")); return false; }
    *special = {};
    return apply(std::move(next), tr("Clamp out-of-range values"));
}

bool TransferFunctionModel::parseState(const QVariantMap &settings, State *out)
{
    bool minOk = false, maxOk = false;
    State parsed;
    parsed.min = settings.value(QStringLiteral("domainMin")).toDouble(&minOk);
    parsed.max = settings.value(QStringLiteral("domainMax")).toDouble(&maxOk);
    if (!minOk || !maxOk || !std::isfinite(parsed.min) || !std::isfinite(parsed.max) || parsed.min >= parsed.max) {
        fail(QStringLiteral("Transfer domain must be finite and increasing")); return false;
    }
    // Validated against the domain being restored; nothing changes on failure.
    if (!parsePoints(settings.value(QStringLiteral("points")).toList(), parsed.min, parsed.max, &parsed.points))
        return false;
    parsed.unit = settings.value(QStringLiteral("unit")).toString();
    parsed.preset = settings.value(QStringLiteral("presetId")).toString();
    // Settings saved before D-263 have no range entries: clamp, transparent missing.
    parsed.missing = {true, QColor(Qt::black), 0};
    for (const QString &which : {QStringLiteral("below"), QStringLiteral("above"), QStringLiteral("missing")}) {
        if (!settings.contains(which)) continue;
        const QVariantMap map = settings.value(which).toMap();
        const QString mode = map.value(QStringLiteral("mode")).toString();
        Special *special = specialNamed(&parsed, which);
        if (mode == QLatin1String("clamp") && which != QLatin1String("missing")) { *special = {}; continue; }
        bool ok = false;
        const double opacity = map.value(QStringLiteral("opacity")).toDouble(&ok);
        const QColor color = opaqueColor(map.value(QStringLiteral("color")));
        if (mode != QLatin1String("color") || !ok || !std::isfinite(opacity) || opacity < 0 || opacity > 1
            || !color.isValid()) {
            fail(QStringLiteral("Invalid %1 transfer color").arg(which)); return false;
        }
        *special = {true, color, opacity};
    }
    *out = std::move(parsed);
    return true;
}
QVariantMap TransferFunctionModel::saveSettings() const
{
    QVariantList saved;
    for (const auto &point : m_points)
        saved.append(QVariantMap{{QStringLiteral("id"), point.id},
                                 {QStringLiteral("value"), point.value},
                                 {QStringLiteral("color"), point.color.name(QColor::HexRgb)},
                                 {QStringLiteral("opacity"), point.opacity}});
    return {{QStringLiteral("domainMin"), m_min}, {QStringLiteral("domainMax"), m_max},
            {QStringLiteral("unit"), m_unit}, {QStringLiteral("points"), saved},
            {QStringLiteral("below"), belowRange()}, {QStringLiteral("above"), aboveRange()},
            {QStringLiteral("missing"), missingValue()}, {QStringLiteral("presetId"), m_preset}};
}
bool TransferFunctionModel::restoreSettings(const QVariantMap &settings)
{
    State parsed;
    if (!parseState(settings, &parsed)) return false;
    setState(parsed);
    resetHistory(); publish(); emit committed();
    return true;
}
bool TransferFunctionModel::applySettings(const QVariantMap &settings)
{
    // AGENT-GUARD (D-262): a host "restore view" must use this, not the load
    // restoreSettings(): the user's transfer edits stay one undo away.
    if (m_editing) { fail(QStringLiteral("Finish the transfer edit first")); return false; }
    State parsed;
    if (!parseState(settings, &parsed)) return false;
    return apply(std::move(parsed), tr("Restore transfer settings"));
}

QVariantList TransferFunctionModel::presets() const
{
    QVariantList result;
    for (const Preset &preset : Presets)
        result.append(QVariantMap{{QStringLiteral("id"), QString::fromLatin1(preset.id)},
                                  {QStringLiteral("name"), tr(preset.name)}});
    return result;
}
bool TransferFunctionModel::applyPreset(const QString &id)
{
    if (m_editing) { fail(QStringLiteral("Finish the transfer edit first")); return false; }
    for (const Preset &preset : Presets) {
        if (id != QLatin1String(preset.id)) continue;
        State next = state();
        next.points.clear();
        for (int index = 0; index < preset.count; ++index) {
            const PresetStop &stop = preset.stops[index];
            next.points.append({QStringLiteral("%1-%2").arg(id).arg(index),
                                m_min + stop.position * (m_max - m_min),
                                QColor::fromString(QLatin1String(stop.color)), stop.opacity});
        }
        next.points.first().value = m_min; // exact endpoints, whatever the rounding
        next.points.last().value = m_max;
        next.preset = id;
        return apply(std::move(next), tr("Apply %1 preset").arg(tr(preset.name)));
    }
    fail(QStringLiteral("Unknown transfer preset %1").arg(id));
    return false;
}

QVariantMap TransferFunctionModel::exportRamp(int count) const
{
    count = std::clamp(count, 2, 4096);
    QVariantList samples;
    for (int i = 0; i < count; ++i) {
        const double value = m_min + (m_max - m_min) * i / (count - 1);
        samples.append(QVariantMap{{QStringLiteral("value"), value},
                                   {QStringLiteral("color"), colorAt(value).name(QColor::HexRgb)},
                                   {QStringLiteral("opacity"), opacityAt(value)}});
    }
    return {{QStringLiteral("domainMin"), m_min}, {QStringLiteral("domainMax"), m_max},
            {QStringLiteral("unit"), m_unit}, {QStringLiteral("presetId"), m_preset},
            {QStringLiteral("samples"), samples}, {QStringLiteral("below"), belowRange()},
            {QStringLiteral("above"), aboveRange()}, {QStringLiteral("missing"), missingValue()}};
}
} // namespace QindaTK
