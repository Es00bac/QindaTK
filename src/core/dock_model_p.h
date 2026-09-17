// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "dock_model.h"

namespace QindaTK::DockModelDetail {

[[nodiscard]] inline QString modeToName(DockModel::Mode mode)
{
    switch (mode) {
    case DockModel::Docked:
        return QStringLiteral("docked");
    case DockModel::Floating:
        return QStringLiteral("floating");
    case DockModel::Collapsed:
        return QStringLiteral("collapsed");
    case DockModel::Hidden:
        return QStringLiteral("hidden");
    }
    return QStringLiteral("docked");
}

[[nodiscard]] inline DockModel::Mode modeFromName(const QString &name, DockModel::Mode fallback)
{
    if (name == QLatin1String("docked")) return DockModel::Docked;
    if (name == QLatin1String("floating")) return DockModel::Floating;
    if (name == QLatin1String("collapsed")) return DockModel::Collapsed;
    if (name == QLatin1String("hidden")) return DockModel::Hidden;
    return fallback;
}

[[nodiscard]] inline bool isSideZone(const QString &zone)
{
    return zone == QLatin1String("left") || zone == QLatin1String("right");
}

} // namespace QindaTK::DockModelDetail
