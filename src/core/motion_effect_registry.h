// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QHash>
#include <QImage>
#include <QVariantMap>
#include <functional>

namespace QindaTK {

struct MotionEffectResult {
    QImage image;
    QString error;
    explicit operator bool() const { return !image.isNull() && error.isEmpty(); }
};

// Small host registration seam for deterministic CPU layer effects. Callbacks
// receive straight RGBA8 encoded sRGB and must preserve image dimensions.
class MotionEffectRegistry {
public:
    using Processor = std::function<bool(QImage &, const QVariantMap &, QString *)>;

    MotionEffectRegistry();
    bool registerProcessor(const QString &effectId, Processor processor);
    bool validate(const QVariantList &effects, QString *error) const;
    MotionEffectResult apply(const QImage &source, const QVariantList &effects) const;

private:
    bool validateEffect(const QVariantMap &effect, QString *error) const;
    QHash<QString, Processor> m_processors;
};

} // namespace QindaTK
