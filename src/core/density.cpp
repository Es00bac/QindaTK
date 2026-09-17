// SPDX-License-Identifier: LGPL-3.0-or-later
#include "density.h"

#include <QJSEngine>
#include <QQmlEngine>

#include <cmath>

namespace QindaTK {

Density::Density(QObject *parent)
    : QObject(parent)
{
}

Density *Density::instance()
{
    static Density *density = new Density();
    return density;
}

Density *Density::create(QQmlEngine *, QJSEngine *)
{
    Density *density = instance();
    QJSEngine::setObjectOwnership(density, QJSEngine::CppOwnership);
    return density;
}

void Density::setMode(Mode mode)
{
    if (mode == m_mode) {
        return;
    }
    m_mode = mode;
    emit changed();
}

QString Density::modeName() const
{
    switch (m_mode) {
    case Compact:
        return QStringLiteral("compact");
    case Comfortable:
        return QStringLiteral("comfortable");
    case Touch:
        return QStringLiteral("touch");
    }
    return QStringLiteral("compact");
}

void Density::setModeName(const QString &name)
{
    const QString lowered = name.trimmed().toLower();
    if (lowered == QLatin1String("comfortable")) {
        setMode(Comfortable);
    } else if (lowered == QLatin1String("touch")) {
        setMode(Touch);
    } else if (lowered == QLatin1String("compact") || lowered == QLatin1String("dense")) {
        setMode(Compact);
    }
}

qreal Density::scale() const
{
    switch (m_mode) {
    case Compact:
        return 1.0;
    case Comfortable:
        return 1.2;
    case Touch:
        return 1.5;
    }
    return 1.0;
}

void Density::setScaleFonts(bool scaleFonts)
{
    if (scaleFonts == m_scaleFonts) {
        return;
    }
    m_scaleFonts = scaleFonts;
    emit changed();
}

qreal Density::px(qreal value) const
{
    return std::round(value * scale());
}

qreal Density::scaled(qreal value) const
{
    return value * scale();
}

} // namespace QindaTK
