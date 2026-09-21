// SPDX-License-Identifier: LGPL-3.0-or-later
#include "theme_presets.h"

#include "theme.h"

#include <QFontDatabase>

#include <array>

namespace QindaTK {

namespace {

struct BaseRoles final {
    QColor bg, surface, panel, border, text, muted, accent, accentContrast;
    QColor danger, warning, success, info;
    bool dark = true;
};

struct PresetDef final {
    const char *id;
    const char *name;
    BaseRoles roles;
};

// AGENT-NOTE: sloom-dark is Sloom Studio's shipped palette (src/index.css
// of the original); the status colours are the literal amber/emerald the
// original paints because its nine roles carry none. sloom-light keeps the
// splash-screen paper tone as its base.
const std::array<PresetDef, 3> &presetTable()
{
    static const std::array<PresetDef, 3> table = {{
        {"sloom-dark", "Sloom Dark",
         {QColor("#0b0c10"), QColor("#11141d"), QColor("#1a1b23"), QColor("#263244"),
          QColor("#f3f7fb"), QColor("#92a3b8"), QColor("#22d3ee"), QColor("#061018"),
          QColor("#fb7185"), QColor("#ecc52f"), QColor("#6ee7b7"), QColor("#22d3ee"), true}},
        {"sloom-light", "Sloom Light",
         {QColor("#f3eee4"), QColor("#faf7f0"), QColor("#ffffff"), QColor("#d9d2c3"),
          QColor("#1b1f27"), QColor("#5f6878"), QColor("#0e9fb8"), QColor("#ffffff"),
          QColor("#d9345d"), QColor("#a16207"), QColor("#0f8f68"), QColor("#0e9fb8"), false}},
        {"graphite", "Graphite",
         {QColor("#121212"), QColor("#181818"), QColor("#1f1f1f"), QColor("#2c2c2c"),
          QColor("#ececec"), QColor("#9a9a9a"), QColor("#7aa2f7"), QColor("#0b0f1a"),
          QColor("#f7768e"), QColor("#e0af68"), QColor("#9ece6a"), QColor("#7dcfff"), true}},
    }};
    return table;
}

// Base type ramp in pixels; rescaleFonts republishes it at a factor.
struct FontBase final {
    qreal micro = 9, caption = 10, small = 11, body = 12, medium = 13, large = 14, title = 16,
          display = 20;
};
FontBase g_fontBase;

QColor colorAt(const QVariantMap &map, std::initializer_list<const char *> keys)
{
    for (const char *key : keys) {
        const QVariant value = map.value(QString::fromLatin1(key));
        if (!value.isValid()) {
            continue;
        }
        const QColor color = value.value<QColor>();
        if (color.isValid()) {
            return color;
        }
    }
    return {};
}

qreal numberAt(const QVariantMap &map, std::initializer_list<const char *> keys, qreal fallback)
{
    for (const char *key : keys) {
        const QVariant value = map.value(QString::fromLatin1(key));
        if (value.isValid() && value.canConvert<double>()) {
            return value.toDouble();
        }
    }
    return fallback;
}

QString firstInstalledFamily(std::initializer_list<const char *> candidates, const QString &fallback)
{
    for (const char *candidate : candidates) {
        if (QFontDatabase::hasFamily(QString::fromLatin1(candidate))) {
            return QString::fromLatin1(candidate);
        }
    }
    return fallback;
}

BaseRoles currentBase(const Theme &theme)
{
    const ThemeColors &c = *theme.color();
    return {c.bg(), c.surface(), c.panel(), c.border(), c.text(), c.textMuted(), c.accent(),
            c.accentContrast(), c.danger(), c.warning(), c.success(), c.info(), theme.dark()};
}

// AGENT-CONTRACT: the mix ratios. Recovered from Sloom Studio's stylesheet
// (`color-mix` rules of src/index.css) and, for the chip/island roles, by
// sampling rendered pixels of the running original (QindaStudio's
// StudioTheme.qml). Where a stylesheet and the pixels disagreed, the pixels
// won. Change a ratio here and every control follows.
void derive(Theme &theme, const BaseRoles &r)
{
    ThemeColors &c = *theme.color();
    const QColor black(Qt::black);
    const bool dark = r.dark;
    c.setRole("bg", r.bg);
    c.setRole("surface", r.surface);
    c.setRole("panel", r.panel);
    c.setRole("border", r.border);
    c.setRole("text", r.text);
    c.setRole("textMuted", r.muted);
    c.setRole("accent", r.accent);
    c.setRole("accentContrast", r.accentContrast);
    c.setRole("danger", r.danger);
    c.setRole("warning", r.warning);
    c.setRole("success", r.success);
    c.setRole("info", r.info);

    c.setRole("panelAlt", theme.mix(r.panel, black, dark ? 0.12 : 0.04));
    c.setRole("canvas", theme.mix(r.bg, black, dark ? 0.30 : 0.06));
    c.setRole("borderStrong", theme.mix(r.border, r.text, 0.25));
    c.setRole("divider", theme.alpha(r.border, 0.60));
    c.setRole("textDisabled", theme.alpha(r.muted, 0.55));
    c.setRole("accentSubtle", theme.mix(r.panel, r.accent, 0.12));
    c.setRole("accentText", dark ? r.accent : theme.mix(r.accent, black, 0.15));
    c.setRole("hover", theme.alpha(r.accent, 0.10));
    c.setRole("pressed", theme.alpha(r.accent, 0.18));
    c.setRole("selection", theme.alpha(r.accent, 0.28));
    c.setRole("focus", theme.alpha(r.accent, 0.65));
    c.setRole("dangerContrast", theme.onColor(r.danger));
    c.setRole("dangerSubtle", theme.alpha(r.danger, 0.15));
    c.setRole("controlBg", theme.alpha(r.panel, 0.72));
    c.setRole("controlBorder", theme.alpha(r.accent, 0.18));
    c.setRole("controlHoverBg", theme.mix(r.panel, r.accent, 0.12));
    c.setRole("controlHoverBorder", theme.alpha(r.accent, 0.42));
    c.setRole("controlActiveBg", theme.mix(r.panel, r.accent, 0.18));
    c.setRole("controlActiveBorder", theme.alpha(r.accent, 0.54));
    c.setRole("chipBg", theme.mix(r.panel, r.accent, 0.18));
    c.setRole("chipBorder", theme.mix(r.border, r.accent, 0.25));
    c.setRole("chipHoverBg", theme.mix(r.panel, r.accent, 0.26));
    c.setRole("chipHoverBorder", theme.mix(r.border, r.accent, 0.38));
    c.setRole("inputBg", theme.mix(r.panel, r.bg, 0.66));
    c.setRole("inputBorder", theme.alpha(r.border, 0.78));
    c.setRole("inputFocusBorder", theme.alpha(r.accent, 0.62));
    c.setRole("headerBg", theme.mix(r.surface, r.panel, 0.78));
    c.setRole("headerText", r.muted);
    c.setRole("popoverBg", dark ? theme.mix(r.surface, black, 0.04) : r.panel);
    c.setRole("popoverBorder", theme.mix(r.border, r.accent, 0.15));
    c.setRole("tooltipBg", dark ? theme.mix(r.panel, black, 0.10) : r.text);
    c.setRole("tooltipText", dark ? r.text : r.bg);
    c.setRole("islandBg", theme.alpha(r.bg, 0.72));
    c.setRole("islandBorder", theme.alpha(r.border, 0.55));
    c.setRole("scrollTrack", theme.mix(r.surface, black, dark ? 0.18 : 0.04));
    c.setRole("scrollThumb", theme.alpha(r.accent, 0.38));
    c.setRole("seam", theme.alpha(r.border, 0.40));
    c.setRole("seamHover", theme.alpha(r.accent, 0.80));
    c.setRole("dropZone", theme.alpha(r.accent, 0.22));
    c.setRole("shadow", theme.alpha(black, dark ? 0.55 : 0.25));
    c.setRole("overlay", theme.alpha(black, 0.65));
    ThemePresets::setDark(theme, dark);
}

void publishFonts(Theme &theme, qreal factor)
{
    ThemePresets::publishFontSizes(theme,
        std::round(g_fontBase.micro * factor), std::round(g_fontBase.caption * factor),
        std::round(g_fontBase.small * factor), std::round(g_fontBase.body * factor),
        std::round(g_fontBase.medium * factor), std::round(g_fontBase.large * factor),
        std::round(g_fontBase.title * factor), std::round(g_fontBase.display * factor));
}

void applyBaseFromMap(Theme &theme, const QVariantMap &roles)
{
    BaseRoles base = currentBase(theme);
    const auto pick = [&roles](QColor &target, const char *key) {
        const QVariant value = roles.value(QString::fromLatin1(key));
        if (value.isValid() && value.value<QColor>().isValid()) {
            target = value.value<QColor>();
        }
    };
    pick(base.bg, "bg");
    pick(base.surface, "surface");
    pick(base.panel, "panel");
    pick(base.border, "border");
    pick(base.text, "text");
    pick(base.muted, "textMuted");
    pick(base.accent, "accent");
    pick(base.accentContrast, "accentContrast");
    pick(base.danger, "danger");
    pick(base.warning, "warning");
    pick(base.success, "success");
    pick(base.info, "info");
    if (roles.contains(QStringLiteral("dark"))) {
        base.dark = roles.value(QStringLiteral("dark")).toBool();
    } else {
        base.dark = theme.luminance(base.bg) < 0.4;
    }
    derive(theme, base);
    // Any further key is an explicit override of a derived role.
    static const QStringList baseKeys = {
        "bg", "surface", "panel", "border", "text", "textMuted", "accent", "accentContrast",
        "danger", "warning", "success", "info", "dark"};
    for (auto it = roles.cbegin(); it != roles.cend(); ++it) {
        if (baseKeys.contains(it.key())) {
            continue;
        }
        const QColor color = it.value().value<QColor>();
        if (color.isValid()) {
            theme.color()->setRole(it.key(), color);
        }
    }
}

} // namespace

QStringList ThemePresets::ids()
{
    QStringList ids;
    for (const PresetDef &preset : presetTable()) {
        ids.append(QString::fromLatin1(preset.id));
    }
    return ids;
}

bool ThemePresets::apply(Theme &theme, const QString &id)
{
    for (const PresetDef &preset : presetTable()) {
        if (id != QLatin1String(preset.id)) {
            continue;
        }
        derive(theme, preset.roles);
        theme.m_preset = id;
        theme.m_name = QString::fromLatin1(preset.name);
        g_fontBase = FontBase{};
        theme.font()->m_family = firstInstalledFamily(
            {"Inter", "Fira Sans", "Noto Sans", "DejaVu Sans"}, theme.font()->m_family);
        theme.font()->m_monoFamily = firstInstalledFamily(
            {"IBM Plex Mono", "JetBrains Mono", "Fira Code", "DejaVu Sans Mono"},
            theme.font()->m_monoFamily);
        publishFonts(theme, 1.0);
        return true;
    }
    return false;
}

void ThemePresets::installBaseMetrics(Theme &theme)
{
    struct Entry { const char *name; qreal value; };
    static const Entry space[] = {{"unit", 4}, {"xs", 2}, {"sm", 4}, {"md", 8}, {"lg", 12},
                                  {"xl", 16}, {"xxl", 24}, {"xxxl", 32}};
    static const Entry radius[] = {{"none", 0}, {"xs", 2}, {"sm", 4}, {"md", 6}, {"lg", 10},
                                   {"xl", 14}, {"full", 999}};
    // AGENT-NOTE: measured from Sloom Studio at 100%: chips 32 (radius 6),
    // island actions 36 (fully round), control rows 24, list rows 22,
    // panel headers 24, dock tab strips 22, toolbars 36, status bars 22.
    static const Entry size[] = {
        {"controlSm", 20}, {"control", 24}, {"controlLg", 28}, {"chip", 32}, {"action", 36},
        {"row", 22}, {"rowLg", 28}, {"header", 24}, {"tab", 22}, {"toolbar", 36},
        {"statusBar", 22}, {"menuItem", 22}, {"icon", 14}, {"iconSm", 12}, {"iconLg", 16},
        {"iconXl", 20}, {"seam", 4}, {"scrollbar", 8}, {"labelWidth", 96}, {"fieldWidth", 120},
        {"panelMinWidth", 200}, {"panelMinHeight", 120}, {"border", 1}, {"focusRing", 2},
        {"grip", 10}, {"handle", 14}, {"dropEdge", 28},
        // Telemetry: a sparkline that fits a 22px list row beside its number,
        // a meter the weight of a rule rather than a bar, and the shortest
        // plot in which a 120-sample trace still has a readable shape.
        {"sparkline", 56}, {"sparklineHeight", 14}, {"meter", 6}, {"graphMinHeight", 40},
        // Media: a thumbnail wide enough to recognise a frame in a bin row at
        // 16:9, the waveform strip that fits under a short audio lane, and the
        // time ruler above a track area.
        {"thumbnail", 72}, {"thumbnailHeight", 40}, {"waveformHeight", 28},
        {"timeRuler", 22}};
    static const Entry motion[] = {{"instant", 0}, {"fast", 80}, {"base", 140}, {"slow", 220}};
    static const Entry opacity[] = {{"disabled", 0.45}, {"muted", 0.7}, {"island", 0.72},
                                    {"ghost", 0.55}, {"scrim", 0.65}};
    for (const Entry &e : space) theme.space()->setBase(QString::fromLatin1(e.name), e.value);
    for (const Entry &e : radius) theme.radius()->setBase(QString::fromLatin1(e.name), e.value);
    for (const Entry &e : size) theme.size()->setBase(QString::fromLatin1(e.name), e.value);
    for (const Entry &e : motion) theme.motion()->setBase(QString::fromLatin1(e.name), e.value);
    for (const Entry &e : opacity) theme.opacity()->setBase(QString::fromLatin1(e.name), e.value);
}

void ThemePresets::applyRoles(Theme &theme, const QVariantMap &roles)
{
    applyBaseFromMap(theme, roles);
    theme.m_name = QStringLiteral("Custom");
}

bool ThemePresets::applyQst(Theme &theme, const QVariantMap &tokens)
{
    const QVariantMap bg = tokens.value(QStringLiteral("bg")).toMap();
    const QVariantMap fg = tokens.value(QStringLiteral("fg")).toMap();
    const QVariantMap accent = tokens.value(QStringLiteral("accent")).toMap();
    if (bg.isEmpty() || fg.isEmpty() || accent.isEmpty()) {
        return false;
    }
    const QVariantMap state = tokens.value(QStringLiteral("state")).toMap();
    const QVariantMap focus = tokens.value(QStringLiteral("focus")).toMap();
    const QVariantMap outline = tokens.value(QStringLiteral("outline")).toMap();
    const QVariantMap status = tokens.value(QStringLiteral("status")).toMap();
    const QVariantMap danger = tokens.value(QStringLiteral("danger")).toMap();

    BaseRoles base = currentBase(theme);
    const auto take = [](QColor &target, const QColor &value) {
        if (value.isValid()) {
            target = value;
        }
    };
    take(base.bg, colorAt(bg, {"base"}));
    take(base.surface, colorAt(bg, {"raised"}));
    take(base.panel, colorAt(bg, {"highest"}));
    take(base.text, colorAt(fg, {"default", "defaultColor"}));
    take(base.muted, colorAt(fg, {"muted"}));
    take(base.accent, colorAt(accent, {"default", "defaultColor"}));
    take(base.accentContrast, colorAt(accent, {"fg", "foreground"}));
    take(base.border, colorAt(outline, {"divider"}));
    take(base.danger, colorAt(danger, {"default", "defaultColor"}));
    take(base.success, colorAt(status.value(QStringLiteral("success")).toMap(), {"fg", "foreground"}));
    take(base.warning, colorAt(status.value(QStringLiteral("warning")).toMap(), {"fg", "foreground"}));
    take(base.info, colorAt(status.value(QStringLiteral("info")).toMap(), {"fg", "foreground"}));
    base.dark = theme.luminance(base.bg) < 0.4;
    derive(theme, base);

    ThemeColors &c = *theme.color();
    const auto overrideRole = [&c](const char *role, const QColor &value) {
        if (value.isValid()) {
            c.setRole(QString::fromLatin1(role), value);
        }
    };
    overrideRole("textDisabled", colorAt(fg, {"disabled"}));
    overrideRole("accentSubtle", colorAt(accent, {"subtle"}));
    overrideRole("hover", colorAt(state, {"hover"}));
    overrideRole("pressed", colorAt(state, {"pressed"}));
    overrideRole("focus", colorAt(focus, {"ring"}));
    overrideRole("borderStrong", colorAt(outline, {"strong"}));
    overrideRole("dangerContrast", colorAt(danger, {"fg", "foreground"}));
    overrideRole("dangerSubtle", colorAt(danger, {"subtle"}));

    const QVariantMap type = tokens.value(QStringLiteral("type")).toMap();
    if (!type.isEmpty()) {
        const QString family = type.value(QStringLiteral("fontFamily")).toString();
        const QString mono = type.value(QStringLiteral("monoFontFamily")).toString();
        if (!family.isEmpty()) theme.font()->m_family = family;
        if (!mono.isEmpty()) theme.font()->m_monoFamily = mono;
        // QST publishes point sizes; the ramp here is pixels at 96 dpi.
        constexpr qreal pxPerPt = 96.0 / 72.0;
        const qreal caption = numberAt(type, {"caption"}, 0) * pxPerPt;
        const qreal body = numberAt(type, {"body"}, 0) * pxPerPt;
        const qreal subtitle = numberAt(type, {"subtitle"}, 0) * pxPerPt;
        const qreal title = numberAt(type, {"title"}, 0) * pxPerPt;
        const qreal display = numberAt(type, {"display"}, 0) * pxPerPt;
        if (caption > 0 && body > 0) {
            g_fontBase.caption = caption;
            g_fontBase.body = body;
            g_fontBase.micro = std::max(8.0, caption - 1);
            g_fontBase.small = (caption + body) / 2;
            g_fontBase.medium = subtitle > 0 ? subtitle : body + 1;
            g_fontBase.title = title > 0 ? title : body + 4;
            g_fontBase.large = (g_fontBase.medium + g_fontBase.title) / 2;
            g_fontBase.display = display > 0 ? display : g_fontBase.title + 4;
        }
    }
    publishFonts(theme, 1.0);

    const QVariantMap radius = tokens.value(QStringLiteral("radius")).toMap();
    if (!radius.isEmpty()) {
        theme.radius()->setBase(QStringLiteral("sm"), numberAt(radius, {"s", "small"}, 4));
        theme.radius()->setBase(QStringLiteral("md"), numberAt(radius, {"m", "medium"}, 6));
        theme.radius()->setBase(QStringLiteral("lg"), numberAt(radius, {"l", "large"}, 10));
    }
    const QVariantMap motion = tokens.value(QStringLiteral("motion")).toMap();
    if (!motion.isEmpty()) {
        theme.motion()->setBase(QStringLiteral("fast"), numberAt(motion, {"short", "shortDuration"}, 80));
        theme.motion()->setBase(QStringLiteral("base"), numberAt(motion, {"base"}, 140));
        theme.motion()->setBase(QStringLiteral("slow"), numberAt(motion, {"long", "longDuration"}, 220));
    }
    theme.m_name = tokens.value(QStringLiteral("sourceThemeId"), QStringLiteral("QindaQt")).toString();
    return true;
}

bool ThemePresets::applyJson(Theme &theme, const QVariantMap &json)
{
    const QVariantMap colors = json.value(QStringLiteral("colors")).toMap();
    if (colors.isEmpty()) {
        return false;
    }
    QVariantMap roles = colors;
    if (json.contains(QStringLiteral("dark"))) {
        roles.insert(QStringLiteral("dark"), json.value(QStringLiteral("dark")));
    }
    applyBaseFromMap(theme, roles);
    theme.m_name = json.value(QStringLiteral("name"), QStringLiteral("Theme file")).toString();

    const QVariantMap font = json.value(QStringLiteral("font")).toMap();
    if (!font.isEmpty()) {
        const QString family = font.value(QStringLiteral("family")).toString();
        const QString mono = font.value(QStringLiteral("monoFamily")).toString();
        if (!family.isEmpty()) theme.font()->m_family = family;
        if (!mono.isEmpty()) theme.font()->m_monoFamily = mono;
        g_fontBase.micro = numberAt(font, {"micro"}, g_fontBase.micro);
        g_fontBase.caption = numberAt(font, {"caption"}, g_fontBase.caption);
        g_fontBase.small = numberAt(font, {"small"}, g_fontBase.small);
        g_fontBase.body = numberAt(font, {"body"}, g_fontBase.body);
        g_fontBase.medium = numberAt(font, {"medium"}, g_fontBase.medium);
        g_fontBase.large = numberAt(font, {"large"}, g_fontBase.large);
        g_fontBase.title = numberAt(font, {"title"}, g_fontBase.title);
        g_fontBase.display = numberAt(font, {"display"}, g_fontBase.display);
    }
    publishFonts(theme, 1.0);

    const auto applyLadder = [&json](ThemeMetrics *ladder, const char *group) {
        const QVariantMap values = json.value(QString::fromLatin1(group)).toMap();
        for (auto it = values.cbegin(); it != values.cend(); ++it) {
            if (it.value().canConvert<double>()) {
                ladder->setBase(it.key(), it.value().toDouble());
            }
        }
    };
    applyLadder(theme.space(), "space");
    applyLadder(theme.radius(), "radius");
    applyLadder(theme.size(), "size");
    applyLadder(theme.motion(), "motion");
    applyLadder(theme.opacity(), "opacity");
    return true;
}

void ThemePresets::rescaleFonts(Theme &theme, qreal factor)
{
    publishFonts(theme, factor);
}

void ThemePresets::setDark(Theme &theme, bool dark)
{
    theme.m_dark = dark;
}

void ThemePresets::publishFontSizes(Theme &theme, qreal micro, qreal caption, qreal small, qreal body,
                                    qreal medium, qreal large, qreal title, qreal display)
{
    ThemeFont &f = *theme.font();
    f.m_micro = micro;
    f.m_caption = caption;
    f.m_small = small;
    f.m_body = body;
    f.m_medium = medium;
    f.m_large = large;
    f.m_title = title;
    f.m_display = display;
    // 0.14em / 0.18em at caption size: the uppercase header signature.
    f.m_trackingWide = caption * 0.14;
    f.m_trackingWider = caption * 0.18;
}

void ThemePresets::setFamilies(Theme &theme, const QString &family, const QString &mono)
{
    if (!family.isEmpty()) theme.font()->m_family = family;
    if (!mono.isEmpty()) theme.font()->m_monoFamily = mono;
}

void ThemePresets::applyMotion(Theme &theme, bool reduced)
{
    ThemeMetrics *motion = theme.motion();
    if (reduced) {
        for (const QString &name : motion->names()) {
            motion->insert(name, 0.0);
        }
    } else {
        motion->rescale(1.0);
    }
}

} // namespace QindaTK
