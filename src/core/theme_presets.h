// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace QindaTK {

class Theme;

// The authoring side of Theme: built-in presets, the derivation of every
// non-base colour role, QST-1 token adoption and JSON theme files. Split
// from theme.cpp so the mix ratios (the part measured from Sloom Studio)
// live in one place.
struct ThemePresets final {
    [[nodiscard]] static QStringList ids();
    static bool apply(Theme &theme, const QString &id);
    static void installBaseMetrics(Theme &theme);
    static void applyRoles(Theme &theme, const QVariantMap &roles);
    static bool applyQst(Theme &theme, const QVariantMap &tokens);
    static bool applyJson(Theme &theme, const QVariantMap &json);
    static void rescaleFonts(Theme &theme, qreal factor);
    static void applyMotion(Theme &theme, bool reduced);

    // Shims for the file-local helpers of theme_presets.cpp, which are not
    // friends of Theme/ThemeFont themselves.
    static void setDark(Theme &theme, bool dark);
    static void publishFontSizes(Theme &theme, qreal micro, qreal caption, qreal small, qreal body,
                                 qreal medium, qreal large, qreal title, qreal display);
    static void setFamilies(Theme &theme, const QString &family, const QString &mono);
};

} // namespace QindaTK
