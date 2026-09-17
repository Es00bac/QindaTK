// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QColor>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QQmlPropertyMap>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <vector>

class QQmlEngine;
class QJSEngine;

namespace QindaTK {

struct ThemePresets;

// Getter + private member for one theme role. Q_PROPERTY lines stay
// explicit so moc sees them; only the boilerplate is folded.
#define QTK_ROLE(type, name)                                                  \
public:                                                                       \
    [[nodiscard]] type name() const { return m_##name; }                      \
private:                                                                      \
    type m_##name{};

// AGENT-CONTRACT: colour roles. Nine base roles (bg, surface, panel, border,
// text, textMuted, accent, accentContrast, danger) plus the three status
// colours are authored by a preset, a theme file, or QindaQt's tokens; every
// other role is derived from them by theme_presets.cpp with the mix ratios
// measured from Sloom Studio. Controls read derived roles, never mix
// themselves, so a theme stays coherent when its accent changes.
class ThemeColors : public QObject {
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(QColor bg READ bg NOTIFY changed)
    Q_PROPERTY(QColor surface READ surface NOTIFY changed)
    Q_PROPERTY(QColor panel READ panel NOTIFY changed)
    Q_PROPERTY(QColor panelAlt READ panelAlt NOTIFY changed)
    Q_PROPERTY(QColor canvas READ canvas NOTIFY changed)
    Q_PROPERTY(QColor border READ border NOTIFY changed)
    Q_PROPERTY(QColor borderStrong READ borderStrong NOTIFY changed)
    Q_PROPERTY(QColor divider READ divider NOTIFY changed)
    Q_PROPERTY(QColor text READ text NOTIFY changed)
    Q_PROPERTY(QColor textMuted READ textMuted NOTIFY changed)
    Q_PROPERTY(QColor textDisabled READ textDisabled NOTIFY changed)
    Q_PROPERTY(QColor accent READ accent NOTIFY changed)
    Q_PROPERTY(QColor accentContrast READ accentContrast NOTIFY changed)
    Q_PROPERTY(QColor accentSubtle READ accentSubtle NOTIFY changed)
    Q_PROPERTY(QColor accentText READ accentText NOTIFY changed)
    Q_PROPERTY(QColor hover READ hover NOTIFY changed)
    Q_PROPERTY(QColor pressed READ pressed NOTIFY changed)
    Q_PROPERTY(QColor selection READ selection NOTIFY changed)
    Q_PROPERTY(QColor focus READ focus NOTIFY changed)
    Q_PROPERTY(QColor danger READ danger NOTIFY changed)
    Q_PROPERTY(QColor dangerContrast READ dangerContrast NOTIFY changed)
    Q_PROPERTY(QColor dangerSubtle READ dangerSubtle NOTIFY changed)
    Q_PROPERTY(QColor warning READ warning NOTIFY changed)
    Q_PROPERTY(QColor success READ success NOTIFY changed)
    Q_PROPERTY(QColor info READ info NOTIFY changed)
    Q_PROPERTY(QColor controlBg READ controlBg NOTIFY changed)
    Q_PROPERTY(QColor controlBorder READ controlBorder NOTIFY changed)
    Q_PROPERTY(QColor controlHoverBg READ controlHoverBg NOTIFY changed)
    Q_PROPERTY(QColor controlHoverBorder READ controlHoverBorder NOTIFY changed)
    Q_PROPERTY(QColor controlActiveBg READ controlActiveBg NOTIFY changed)
    Q_PROPERTY(QColor controlActiveBorder READ controlActiveBorder NOTIFY changed)
    Q_PROPERTY(QColor chipBg READ chipBg NOTIFY changed)
    Q_PROPERTY(QColor chipBorder READ chipBorder NOTIFY changed)
    Q_PROPERTY(QColor chipHoverBg READ chipHoverBg NOTIFY changed)
    Q_PROPERTY(QColor chipHoverBorder READ chipHoverBorder NOTIFY changed)
    Q_PROPERTY(QColor inputBg READ inputBg NOTIFY changed)
    Q_PROPERTY(QColor inputBorder READ inputBorder NOTIFY changed)
    Q_PROPERTY(QColor inputFocusBorder READ inputFocusBorder NOTIFY changed)
    Q_PROPERTY(QColor headerBg READ headerBg NOTIFY changed)
    Q_PROPERTY(QColor headerText READ headerText NOTIFY changed)
    Q_PROPERTY(QColor popoverBg READ popoverBg NOTIFY changed)
    Q_PROPERTY(QColor popoverBorder READ popoverBorder NOTIFY changed)
    Q_PROPERTY(QColor tooltipBg READ tooltipBg NOTIFY changed)
    Q_PROPERTY(QColor tooltipText READ tooltipText NOTIFY changed)
    Q_PROPERTY(QColor islandBg READ islandBg NOTIFY changed)
    Q_PROPERTY(QColor islandBorder READ islandBorder NOTIFY changed)
    Q_PROPERTY(QColor scrollTrack READ scrollTrack NOTIFY changed)
    Q_PROPERTY(QColor scrollThumb READ scrollThumb NOTIFY changed)
    Q_PROPERTY(QColor seam READ seam NOTIFY changed)
    Q_PROPERTY(QColor seamHover READ seamHover NOTIFY changed)
    Q_PROPERTY(QColor dropZone READ dropZone NOTIFY changed)
    Q_PROPERTY(QColor shadow READ shadow NOTIFY changed)
    Q_PROPERTY(QColor overlay READ overlay NOTIFY changed)

public:
    struct Role final {
        const char *name;
        QColor ThemeColors::*member;
    };

    explicit ThemeColors(QObject *parent = nullptr);

    // Every role with its member, in declaration order.
    [[nodiscard]] static const std::vector<Role> &roleTable();
    // Role names as QML sees them ("bg", "controlHoverBg", ...).
    [[nodiscard]] static QStringList roleNames();
    [[nodiscard]] QColor role(const QString &name) const;
    // Returns false for an unknown role. Emits nothing; the owner batches.
    bool setRole(const QString &name, const QColor &color);
    [[nodiscard]] QVariantMap toMap() const;

signals:
    void changed();

    QTK_ROLE(QColor, bg)
    QTK_ROLE(QColor, surface)
    QTK_ROLE(QColor, panel)
    QTK_ROLE(QColor, panelAlt)
    QTK_ROLE(QColor, canvas)
    QTK_ROLE(QColor, border)
    QTK_ROLE(QColor, borderStrong)
    QTK_ROLE(QColor, divider)
    QTK_ROLE(QColor, text)
    QTK_ROLE(QColor, textMuted)
    QTK_ROLE(QColor, textDisabled)
    QTK_ROLE(QColor, accent)
    QTK_ROLE(QColor, accentContrast)
    QTK_ROLE(QColor, accentSubtle)
    QTK_ROLE(QColor, accentText)
    QTK_ROLE(QColor, hover)
    QTK_ROLE(QColor, pressed)
    QTK_ROLE(QColor, selection)
    QTK_ROLE(QColor, focus)
    QTK_ROLE(QColor, danger)
    QTK_ROLE(QColor, dangerContrast)
    QTK_ROLE(QColor, dangerSubtle)
    QTK_ROLE(QColor, warning)
    QTK_ROLE(QColor, success)
    QTK_ROLE(QColor, info)
    QTK_ROLE(QColor, controlBg)
    QTK_ROLE(QColor, controlBorder)
    QTK_ROLE(QColor, controlHoverBg)
    QTK_ROLE(QColor, controlHoverBorder)
    QTK_ROLE(QColor, controlActiveBg)
    QTK_ROLE(QColor, controlActiveBorder)
    QTK_ROLE(QColor, chipBg)
    QTK_ROLE(QColor, chipBorder)
    QTK_ROLE(QColor, chipHoverBg)
    QTK_ROLE(QColor, chipHoverBorder)
    QTK_ROLE(QColor, inputBg)
    QTK_ROLE(QColor, inputBorder)
    QTK_ROLE(QColor, inputFocusBorder)
    QTK_ROLE(QColor, headerBg)
    QTK_ROLE(QColor, headerText)
    QTK_ROLE(QColor, popoverBg)
    QTK_ROLE(QColor, popoverBorder)
    QTK_ROLE(QColor, tooltipBg)
    QTK_ROLE(QColor, tooltipText)
    QTK_ROLE(QColor, islandBg)
    QTK_ROLE(QColor, islandBorder)
    QTK_ROLE(QColor, scrollTrack)
    QTK_ROLE(QColor, scrollThumb)
    QTK_ROLE(QColor, seam)
    QTK_ROLE(QColor, seamHover)
    QTK_ROLE(QColor, dropZone)
    QTK_ROLE(QColor, shadow)
    QTK_ROLE(QColor, overlay)

    friend class Theme;
    friend struct ThemePresets;
};

// Type ramp in pixels (Sloom's 9/10/11/12/13/14/16/20 ladder). `trackingWide`
// is the letter spacing of an uppercase section header at caption size.
class ThemeFont : public QObject {
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(QString family READ family NOTIFY changed)
    Q_PROPERTY(QString monoFamily READ monoFamily NOTIFY changed)
    Q_PROPERTY(qreal micro READ micro NOTIFY changed)
    Q_PROPERTY(qreal caption READ caption NOTIFY changed)
    Q_PROPERTY(qreal small READ small NOTIFY changed)
    Q_PROPERTY(qreal body READ body NOTIFY changed)
    Q_PROPERTY(qreal medium READ medium NOTIFY changed)
    Q_PROPERTY(qreal large READ large NOTIFY changed)
    Q_PROPERTY(qreal title READ title NOTIFY changed)
    Q_PROPERTY(qreal display READ display NOTIFY changed)
    Q_PROPERTY(qreal trackingWide READ trackingWide NOTIFY changed)
    Q_PROPERTY(qreal trackingWider READ trackingWider NOTIFY changed)

public:
    explicit ThemeFont(QObject *parent = nullptr);
    [[nodiscard]] QVariantMap toMap() const;

signals:
    void changed();

    QTK_ROLE(QString, family)
    QTK_ROLE(QString, monoFamily)
    QTK_ROLE(qreal, micro)
    QTK_ROLE(qreal, caption)
    QTK_ROLE(qreal, small)
    QTK_ROLE(qreal, body)
    QTK_ROLE(qreal, medium)
    QTK_ROLE(qreal, large)
    QTK_ROLE(qreal, title)
    QTK_ROLE(qreal, display)
    QTK_ROLE(qreal, trackingWide)
    QTK_ROLE(qreal, trackingWider)

    friend class Theme;
    friend struct ThemePresets;
};

// A named ladder of scalar metrics (space, radius, size, motion, opacity).
// Keys are dynamic QML properties (`Theme.space.md`, `Theme.size.control`)
// so JSON theme files and density scaling treat every ladder alike; the key
// lists live in docs/theming.md and Theme::toMap().
class ThemeMetrics : public QQmlPropertyMap {
    Q_OBJECT
    QML_ANONYMOUS

public:
    explicit ThemeMetrics(bool scalable, QObject *parent = nullptr);

    [[nodiscard]] qreal metric(const QString &name) const;
    // Base (unscaled) values. `rescale` republishes every key at `factor`;
    // a ladder created non-scalable ignores the factor.
    void setBase(const QString &name, qreal value);
    [[nodiscard]] qreal base(const QString &name) const;
    void rescale(qreal factor);
    [[nodiscard]] QStringList names() const;
    [[nodiscard]] QVariantMap toMap() const;

private:
    QHash<QString, qreal> m_base;
    QStringList m_order;
    qreal m_factor = 1.0;
    bool m_scalable = true;
};

// AGENT-CONTRACT: the process-wide theme singleton. Everything visual in
// QindaTK resolves through it: colour roles, the type ramp, spacing/radius/
// size/motion/opacity ladders. Density (see density.h) multiplies the
// scalable ladders. Presets are built in (`presets()`), a JSON file can
// replace a preset (`loadFile`), and on the QindaQt desktop the
// QindaTK.QindaQt bridge feeds QST-1 tokens through `applyQst`, so one
// control tree serves both the standalone and the desktop-themed case.
class Theme : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QindaTK::ThemeColors *color READ color CONSTANT)
    Q_PROPERTY(QindaTK::ThemeFont *font READ font CONSTANT)
    Q_PROPERTY(QindaTK::ThemeMetrics *space READ space CONSTANT)
    Q_PROPERTY(QindaTK::ThemeMetrics *radius READ radius CONSTANT)
    Q_PROPERTY(QindaTK::ThemeMetrics *size READ size CONSTANT)
    Q_PROPERTY(QindaTK::ThemeMetrics *motion READ motion CONSTANT)
    Q_PROPERTY(QindaTK::ThemeMetrics *opacity READ opacity CONSTANT)
    Q_PROPERTY(QString preset READ preset WRITE setPreset NOTIFY changed)
    Q_PROPERTY(QString name READ name NOTIFY changed)
    Q_PROPERTY(bool dark READ dark NOTIFY changed)
    Q_PROPERTY(bool reducedMotion READ reducedMotion WRITE setReducedMotion NOTIFY changed)
    Q_PROPERTY(qulonglong generation READ generation NOTIFY changed)

public:
    [[nodiscard]] static Theme *instance();
    [[nodiscard]] static Theme *create(QQmlEngine *engine, QJSEngine *jsEngine);

    [[nodiscard]] ThemeColors *color() const { return m_color; }
    [[nodiscard]] ThemeFont *font() const { return m_font; }
    [[nodiscard]] ThemeMetrics *space() const { return m_space; }
    [[nodiscard]] ThemeMetrics *radius() const { return m_radius; }
    [[nodiscard]] ThemeMetrics *size() const { return m_size; }
    [[nodiscard]] ThemeMetrics *motion() const { return m_motion; }
    [[nodiscard]] ThemeMetrics *opacity() const { return m_opacity; }

    [[nodiscard]] QString preset() const { return m_preset; }
    void setPreset(const QString &id);
    [[nodiscard]] QString name() const { return m_name; }
    [[nodiscard]] bool dark() const { return m_dark; }
    [[nodiscard]] bool reducedMotion() const { return m_reducedMotion; }
    void setReducedMotion(bool reduced);
    [[nodiscard]] qulonglong generation() const { return m_generation; }

    // Presets: "sloom-dark" (default), "sloom-light", "graphite".
    Q_INVOKABLE [[nodiscard]] QStringList presets() const;
    Q_INVOKABLE bool applyPreset(const QString &id);
    // Authors the base roles ({bg, surface, panel, border, text, textMuted,
    // accent, accentContrast, danger, warning, success, info, dark}) and
    // re-derives every other colour role. Missing keys keep their value.
    Q_INVOKABLE void applyRoles(const QVariantMap &roles);
    // Overrides one role after derivation (kept until the next preset).
    Q_INVOKABLE void setColor(const QString &role, const QColor &color);
    Q_INVOKABLE void setMetric(const QString &group, const QString &name, qreal value);
    Q_INVOKABLE void setFontFamilies(const QString &family, const QString &monoFamily);
    // QindaQt QST-1 tokens: {bg, fg, accent, state, focus, outline, status,
    // danger, radius, space, type, motion} as the Tokens singleton publishes
    // them. Unknown keys are ignored; returns false when nothing applied.
    Q_INVOKABLE bool applyQst(const QVariantMap &tokens);
    // JSON theme: {"name", "dark", "colors": {...}, "font": {...},
    // "space"/"radius"/"size"/"motion"/"opacity": {...}}. See docs/theming.md.
    Q_INVOKABLE bool loadFile(const QString &path);
    Q_INVOKABLE bool loadJson(const QString &json);
    Q_INVOKABLE [[nodiscard]] QVariantMap toMap() const;

    // Colour arithmetic in the sRGB space QML paints in. `mix` weights `b`
    // by `t` (CSS color-mix order); `alpha` replaces the alpha channel.
    Q_INVOKABLE [[nodiscard]] QColor mix(const QColor &a, const QColor &b, qreal t) const;
    Q_INVOKABLE [[nodiscard]] QColor alpha(const QColor &c, qreal a) const;
    Q_INVOKABLE [[nodiscard]] QColor lighten(const QColor &c, qreal amount) const;
    Q_INVOKABLE [[nodiscard]] QColor darken(const QColor &c, qreal amount) const;
    Q_INVOKABLE [[nodiscard]] QColor onColor(const QColor &bg) const;
    Q_INVOKABLE [[nodiscard]] qreal luminance(const QColor &c) const;

signals:
    void changed();

private:
    friend struct ThemePresets;
    explicit Theme(QObject *parent = nullptr);
    void rescale();
    void finishUpdate();

    ThemeColors *m_color = nullptr;
    ThemeFont *m_font = nullptr;
    ThemeMetrics *m_space = nullptr;
    ThemeMetrics *m_radius = nullptr;
    ThemeMetrics *m_size = nullptr;
    ThemeMetrics *m_motion = nullptr;
    ThemeMetrics *m_opacity = nullptr;
    QString m_preset;
    QString m_name;
    bool m_dark = true;
    bool m_reducedMotion = false;
    qulonglong m_generation = 0;
};

} // namespace QindaTK
