// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QString>
#include <QStringList>

class QQmlEngine;

namespace QindaTK::PreviewQst {

// Optional QindaQt desktop integration for qtk-preview: publishes a
// desktop theme (an id from /usr/share/qindaqt/themes or a theme .json
// path) into the QindaQt.Tokens facade of `engine`, exactly as a desktop
// application does, so the QindaTK.QindaQt bridge can be exercised for
// real. Compiled in only when the desktop libraries are installed
// (QINDATK_HAVE_QINDAQT); otherwise available() is false and publish()
// explains why.
[[nodiscard]] bool available();
[[nodiscard]] QStringList themeIds();
[[nodiscard]] bool publish(QQmlEngine &engine, const QString &themeIdOrPath, double textScale,
                           QString *error);

} // namespace QindaTK::PreviewQst
