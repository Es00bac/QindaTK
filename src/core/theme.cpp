// SPDX-License-Identifier: LGPL-3.0-or-later
#include "theme.h"

#include "density.h"
#include "theme_presets.h"

#include <QFile>
#include <QFontDatabase>
#include <QJSEngine>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlEngine>

#include <algorithm>
#include <cmath>

namespace QindaTK {

// AGENT-GUARD: every Q_PROPERTY of ThemeColors must appear here, or
// setRole()/toMap()/JSON themes silently skip it. tests/cpp/tst_theme.cpp
// checks the table against the meta-object.
const std::vector<ThemeColors::Role> &ThemeColors::roleTable()
{
    static const std::vector<Role> table = {
        {"bg", &ThemeColors::m_bg},
        {"surface", &ThemeColors::m_surface},
        {"panel", &ThemeColors::m_panel},
        {"panelAlt", &ThemeColors::m_panelAlt},
        {"canvas", &ThemeColors::m_canvas},
        {"border", &ThemeColors::m_border},
        {"borderStrong", &ThemeColors::m_borderStrong},
        {"divider", &ThemeColors::m_divider},
        {"text", &ThemeColors::m_text},
        {"textMuted", &ThemeColors::m_textMuted},
        {"textDisabled", &ThemeColors::m_textDisabled},
        {"accent", &ThemeColors::m_accent},
        {"accentContrast", &ThemeColors::m_accentContrast},
        {"accentSubtle", &ThemeColors::m_accentSubtle},
        {"accentText", &ThemeColors::m_accentText},
        {"hover", &ThemeColors::m_hover},
        {"pressed", &ThemeColors::m_pressed},
        {"selection", &ThemeColors::m_selection},
        {"focus", &ThemeColors::m_focus},
        {"danger", &ThemeColors::m_danger},
        {"dangerContrast", &ThemeColors::m_dangerContrast},
        {"dangerSubtle", &ThemeColors::m_dangerSubtle},
        {"warning", &ThemeColors::m_warning},
        {"success", &ThemeColors::m_success},
        {"info", &ThemeColors::m_info},
        {"controlBg", &ThemeColors::m_controlBg},
        {"controlBorder", &ThemeColors::m_controlBorder},
        {"controlHoverBg", &ThemeColors::m_controlHoverBg},
        {"controlHoverBorder", &ThemeColors::m_controlHoverBorder},
        {"controlActiveBg", &ThemeColors::m_controlActiveBg},
        {"controlActiveBorder", &ThemeColors::m_controlActiveBorder},
        {"chipBg", &ThemeColors::m_chipBg},
        {"chipBorder", &ThemeColors::m_chipBorder},
        {"chipHoverBg", &ThemeColors::m_chipHoverBg},
        {"chipHoverBorder", &ThemeColors::m_chipHoverBorder},
        {"inputBg", &ThemeColors::m_inputBg},
        {"inputBorder", &ThemeColors::m_inputBorder},
        {"inputFocusBorder", &ThemeColors::m_inputFocusBorder},
        {"headerBg", &ThemeColors::m_headerBg},
        {"headerText", &ThemeColors::m_headerText},
        {"popoverBg", &ThemeColors::m_popoverBg},
        {"popoverBorder", &ThemeColors::m_popoverBorder},
        {"tooltipBg", &ThemeColors::m_tooltipBg},
        {"tooltipText", &ThemeColors::m_tooltipText},
        {"islandBg", &ThemeColors::m_islandBg},
        {"islandBorder", &ThemeColors::m_islandBorder},
        {"scrollTrack", &ThemeColors::m_scrollTrack},
        {"scrollThumb", &ThemeColors::m_scrollThumb},
        {"seam", &ThemeColors::m_seam},
        {"seamHover", &ThemeColors::m_seamHover},
        {"dropZone", &ThemeColors::m_dropZone},
        {"shadow", &ThemeColors::m_shadow},
        {"overlay", &ThemeColors::m_overlay},
    };
    return table;
}

namespace {

const ThemeColors::Role *findRole(const QString &name)
{
    const QByteArray latin = name.toLatin1();
    for (const ThemeColors::Role &entry : ThemeColors::roleTable()) {
        if (latin == entry.name) {
            return &entry;
        }
    }
    return nullptr;
}

QString defaultFamily(bool mono)
{
    return QFontDatabase::systemFont(mono ? QFontDatabase::FixedFont
                                          : QFontDatabase::GeneralFont).family();
}

} // namespace

// ---- ThemeColors ---------------------------------------------------------

ThemeColors::ThemeColors(QObject *parent)
    : QObject(parent)
{
}

QStringList ThemeColors::roleNames()
{
    QStringList names;
    for (const Role &entry : roleTable()) {
        names.append(QString::fromLatin1(entry.name));
    }
    return names;
}

QColor ThemeColors::role(const QString &name) const
{
    const Role *entry = findRole(name);
    return entry == nullptr ? QColor() : this->*(entry->member);
}

bool ThemeColors::setRole(const QString &name, const QColor &color)
{
    const Role *entry = findRole(name);
    if (entry == nullptr) {
        return false;
    }
    this->*(entry->member) = color;
    return true;
}

QVariantMap ThemeColors::toMap() const
{
    QVariantMap map;
    for (const Role &entry : roleTable()) {
        map.insert(QString::fromLatin1(entry.name), (this->*(entry.member)).name(QColor::HexArgb));
    }
    return map;
}

// ---- ThemeFont -----------------------------------------------------------

ThemeFont::ThemeFont(QObject *parent)
    : QObject(parent)
{
}

QVariantMap ThemeFont::toMap() const
{
    return {
        {QStringLiteral("family"), m_family},
        {QStringLiteral("monoFamily"), m_monoFamily},
        {QStringLiteral("micro"), m_micro},
        {QStringLiteral("caption"), m_caption},
        {QStringLiteral("small"), m_small},
        {QStringLiteral("body"), m_body},
        {QStringLiteral("medium"), m_medium},
        {QStringLiteral("large"), m_large},
        {QStringLiteral("title"), m_title},
        {QStringLiteral("display"), m_display},
        {QStringLiteral("trackingWide"), m_trackingWide},
        {QStringLiteral("trackingWider"), m_trackingWider},
    };
}

// ---- ThemeMetrics --------------------------------------------------------

ThemeMetrics::ThemeMetrics(bool scalable, QObject *parent)
    : QQmlPropertyMap(this, parent)
    , m_scalable(scalable)
{
}

qreal ThemeMetrics::metric(const QString &name) const
{
    return value(name).toReal();
}

void ThemeMetrics::setBase(const QString &name, qreal value)
{
    if (!m_base.contains(name)) {
        m_order.append(name);
    }
    m_base.insert(name, value);
    insert(name, m_scalable ? std::round(value * m_factor) : value);
}

qreal ThemeMetrics::base(const QString &name) const
{
    return m_base.value(name, 0.0);
}

void ThemeMetrics::rescale(qreal factor)
{
    m_factor = factor;
    for (const QString &name : std::as_const(m_order)) {
        const qreal value = m_base.value(name);
        insert(name, m_scalable ? std::round(value * m_factor) : value);
    }
}

QStringList ThemeMetrics::names() const
{
    return m_order;
}

QVariantMap ThemeMetrics::toMap() const
{
    QVariantMap map;
    for (const QString &name : m_order) {
        map.insert(name, value(name));
    }
    return map;
}

// ---- Theme ---------------------------------------------------------------

Theme::Theme(QObject *parent)
    : QObject(parent)
    , m_color(new ThemeColors(this))
    , m_font(new ThemeFont(this))
    , m_space(new ThemeMetrics(true, this))
    , m_radius(new ThemeMetrics(false, this))
    , m_size(new ThemeMetrics(true, this))
    , m_motion(new ThemeMetrics(false, this))
    , m_opacity(new ThemeMetrics(false, this))
    , m_ramp(new ThemeRamps(this))
{
    m_font->m_family = defaultFamily(false);
    m_font->m_monoFamily = defaultFamily(true);
    ThemePresets::installBaseMetrics(*this);
    ThemePresets::apply(*this, QStringLiteral("sloom-dark"));
    m_ramp->rebuild(*m_color);
    connect(Density::instance(), &Density::changed, this, &Theme::rescale);
    rescale();
}

Theme *Theme::instance()
{
    static Theme *theme = new Theme();
    return theme;
}

Theme *Theme::create(QQmlEngine *, QJSEngine *)
{
    Theme *theme = instance();
    QJSEngine::setObjectOwnership(theme, QJSEngine::CppOwnership);
    return theme;
}

void Theme::setPreset(const QString &id)
{
    applyPreset(id);
}

void Theme::setReducedMotion(bool reduced)
{
    if (reduced == m_reducedMotion) {
        return;
    }
    m_reducedMotion = reduced;
    rescale();
}

QStringList Theme::presets() const
{
    return ThemePresets::ids();
}

bool Theme::applyPreset(const QString &id)
{
    if (!ThemePresets::apply(*this, id)) {
        return false;
    }
    finishUpdate();
    return true;
}

void Theme::applyRoles(const QVariantMap &roles)
{
    ThemePresets::applyRoles(*this, roles);
    m_preset = QStringLiteral("custom");
    finishUpdate();
}

void Theme::setColor(const QString &role, const QColor &color)
{
    if (m_color->setRole(role, color)) {
        finishUpdate();
    }
}

void Theme::setMetric(const QString &group, const QString &name, qreal value)
{
    ThemeMetrics *metrics = group == QLatin1String("space") ? m_space
                          : group == QLatin1String("radius") ? m_radius
                          : group == QLatin1String("size") ? m_size
                          : group == QLatin1String("motion") ? m_motion
                          : group == QLatin1String("opacity") ? m_opacity
                          : nullptr;
    if (metrics == nullptr) {
        return;
    }
    metrics->setBase(name, value);
    finishUpdate();
}

void Theme::setFontFamilies(const QString &family, const QString &monoFamily)
{
    if (!family.isEmpty()) {
        m_font->m_family = family;
    }
    if (!monoFamily.isEmpty()) {
        m_font->m_monoFamily = monoFamily;
    }
    finishUpdate();
}

bool Theme::applyQst(const QVariantMap &tokens)
{
    if (!ThemePresets::applyQst(*this, tokens)) {
        return false;
    }
    m_preset = QStringLiteral("qindaqt");
    finishUpdate();
    return true;
}

bool Theme::loadFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    return loadJson(QString::fromUtf8(file.readAll()));
}

bool Theme::loadJson(const QString &json)
{
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(json.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return false;
    }
    if (!ThemePresets::applyJson(*this, document.object().toVariantMap())) {
        return false;
    }
    m_preset = QStringLiteral("file");
    finishUpdate();
    return true;
}

namespace {

QVariantMap rampStops(const ThemeRamp *ramp)
{
    QVariantMap map;
    const QVariantList stops = ramp->stops();
    for (const QVariant &entry : stops) {
        const QVariantMap stop = entry.toMap();
        map.insert(QString::number(stop.value(QStringLiteral("position")).toDouble()),
                   stop.value(QStringLiteral("color")).value<QColor>().name(QColor::HexArgb));
    }
    return map;
}

} // namespace

QVariantMap Theme::rampMap() const
{
    QVariantMap map;
    for (const QString &name : ThemeRamps::names()) {
        map.insert(name, rampStops(m_ramp->byName(name)));
    }
    return map;
}

QVariantMap Theme::toMap() const
{
    return {
        {QStringLiteral("preset"), m_preset},
        {QStringLiteral("name"), m_name},
        {QStringLiteral("dark"), m_dark},
        {QStringLiteral("density"), Density::instance()->modeName()},
        {QStringLiteral("colors"), m_color->toMap()},
        {QStringLiteral("font"), m_font->toMap()},
        {QStringLiteral("space"), m_space->toMap()},
        {QStringLiteral("radius"), m_radius->toMap()},
        {QStringLiteral("size"), m_size->toMap()},
        {QStringLiteral("motion"), m_motion->toMap()},
        {QStringLiteral("opacity"), m_opacity->toMap()},
        {QStringLiteral("ramp"), rampMap()},
    };
}

QColor Theme::mix(const QColor &a, const QColor &b, qreal t) const
{
    const qreal w = std::clamp(t, 0.0, 1.0);
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * w,
                            a.greenF() + (b.greenF() - a.greenF()) * w,
                            a.blueF() + (b.blueF() - a.blueF()) * w,
                            a.alphaF() + (b.alphaF() - a.alphaF()) * w);
}

QColor Theme::alpha(const QColor &c, qreal a) const
{
    QColor result = c;
    result.setAlphaF(std::clamp(a, 0.0, 1.0));
    return result;
}

QColor Theme::lighten(const QColor &c, qreal amount) const
{
    return mix(c, QColor(Qt::white), amount);
}

QColor Theme::darken(const QColor &c, qreal amount) const
{
    return mix(c, QColor(Qt::black), amount);
}

qreal Theme::luminance(const QColor &c) const
{
    const auto channel = [](qreal v) {
        return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * channel(c.redF()) + 0.7152 * channel(c.greenF()) + 0.0722 * channel(c.blueF());
}

QColor Theme::onColor(const QColor &bg) const
{
    return luminance(bg) > 0.35 ? QColor(QStringLiteral("#0b0c10")) : QColor(QStringLiteral("#f3f7fb"));
}

void Theme::rescale()
{
    const qreal scale = Density::instance()->scale();
    m_space->rescale(scale);
    m_size->rescale(scale);
    ThemePresets::rescaleFonts(*this, Density::instance()->scaleFonts() ? scale : 1.0);
    ThemePresets::applyMotion(*this, m_reducedMotion);
    finishUpdate();
}

void Theme::finishUpdate()
{
    ++m_generation;
    // AGENT-GUARD: ramps are derived from the colour roles, so they must be
    // rebuilt before changed() lets any binding read Theme.ramp again.
    m_ramp->rebuild(*m_color);
    emit m_color->changed();
    emit m_font->changed();
    emit changed();
}

} // namespace QindaTK
