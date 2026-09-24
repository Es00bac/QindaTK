// SPDX-License-Identifier: LGPL-3.0-or-later
#include "audio_parameter_map.h"
#include <algorithm>
#include <cmath>

namespace QindaTK {
namespace {
constexpr double finiteGainStart = 0.001; // zero alone means silence
struct Mapping {
    QString taper;
    double minimum = 0;
    double maximum = 1;
    double unityPosition = 0.75;
};

Mapping mapping(const QVariantMap &descriptor)
{
    return {descriptor.value(QStringLiteral("taper")).toString(),
            descriptor.value(QStringLiteral("minimum")).toDouble(),
            descriptor.value(QStringLiteral("maximum")).toDouble(),
            descriptor.value(QStringLiteral("unityPosition")).toDouble()};
}

QString validate(const Mapping &map)
{
    if (!std::isfinite(map.minimum) || !std::isfinite(map.maximum)
        || map.minimum >= map.maximum)
        return QStringLiteral("Parameter bounds must be finite and ordered");
    if (map.taper == QStringLiteral("linear")) return {};
    if (map.taper == QStringLiteral("logarithmic"))
        return map.minimum > 0 ? QString{} : QStringLiteral("Logarithmic bounds must be positive");
    if (map.taper == QStringLiteral("gainDb"))
        return map.minimum < 0 && map.maximum > 0 && map.unityPosition > 0
            && map.unityPosition < 1 && std::isfinite(map.unityPosition)
            ? QString{} : QStringLiteral("Gain bounds must surround 0 dB and unity must lie within travel");
    return QStringLiteral("Unsupported parameter taper");
}
}

void AudioParameterMap::setDescriptor(const QVariantMap &descriptor)
{
    const Mapping next = mapping(descriptor);
    const QString error = validate(next);
    if (!error.isEmpty()) {
        if (m_lastError != error) { m_lastError = error; emit errorChanged(); }
        return;
    }
    if (!m_lastError.isEmpty()) { m_lastError.clear(); emit errorChanged(); }
    if (m_descriptor == descriptor) return;
    m_descriptor = descriptor;
    emit changed();
}

double AudioParameterMap::positionFor(double value, bool silent) const
{
    const Mapping map = mapping(m_descriptor);
    if (silent && map.taper == QStringLiteral("gainDb")) return 0;
    if (!std::isfinite(value)) return 0;
    value = std::clamp(value, map.minimum, map.maximum);
    if (map.taper == QStringLiteral("logarithmic"))
        return std::log(value / map.minimum) / std::log(map.maximum / map.minimum);
    if (map.taper == QStringLiteral("gainDb"))
        return value <= 0
            ? finiteGainStart + (map.unityPosition - finiteGainStart)
                  * (value - map.minimum) / (0 - map.minimum)
            : map.unityPosition + (1 - map.unityPosition) * value / map.maximum;
    return (value - map.minimum) / (map.maximum - map.minimum);
}

QVariantMap AudioParameterMap::valueAt(double position) const
{
    const Mapping map = mapping(m_descriptor);
    position = std::isfinite(position) ? std::clamp(position, 0.0, 1.0) : 0.0;
    if (map.taper == QStringLiteral("gainDb")) {
        if (position == 0) return {{QStringLiteral("value"), map.minimum},
                                   {QStringLiteral("silent"), true}};
        const double value = position <= finiteGainStart ? map.minimum
            : position <= map.unityPosition
            ? map.minimum + (0 - map.minimum)
                  * (position - finiteGainStart) / (map.unityPosition - finiteGainStart)
            : map.maximum * (position - map.unityPosition) / (1 - map.unityPosition);
        return {{QStringLiteral("value"), value}, {QStringLiteral("silent"), false}};
    }
    const double value = map.taper == QStringLiteral("logarithmic")
        ? map.minimum * std::pow(map.maximum / map.minimum, position)
        : map.minimum + (map.maximum - map.minimum) * position;
    return {{QStringLiteral("value"), value}, {QStringLiteral("silent"), false}};
}

} // namespace QindaTK
