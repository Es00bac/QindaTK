// SPDX-License-Identifier: LGPL-3.0-or-later
#include "motion_effect_registry.h"

#include <QColorSpace>
#include <cmath>

namespace QindaTK {
namespace {
constexpr qsizetype kMaxEffects = 64;
constexpr qsizetype kMaxParameterCount = 32;
constexpr int kMaxEffectIdLength = 128;

bool numeric(const QVariant &value)
{
    const int id = value.metaType().id();
    return id == QMetaType::Int || id == QMetaType::UInt || id == QMetaType::LongLong
        || id == QMetaType::ULongLong || id == QMetaType::Double || id == QMetaType::Float;
}

bool brightness(QImage &image, const QVariantMap &parameters, QString *error)
{
    if (parameters.size() > 1 || (parameters.contains(QStringLiteral("amount")) == false && !parameters.isEmpty())) {
        *error = QStringLiteral("brightness accepts only the 'amount' parameter");
        return false;
    }
    bool ok = true;
    double amount = 0.0;
    if (parameters.contains(QStringLiteral("amount"))) {
        const QVariant amountValue = parameters.value(QStringLiteral("amount"));
        if (!numeric(amountValue)) ok = false;
        else amount = amountValue.toDouble(&ok);
    }
    if (!ok || !std::isfinite(amount) || amount < -1.0 || amount > 1.0) {
        *error = QStringLiteral("brightness amount must be finite and within [-1,1]");
        return false;
    }
    const int offset = qRound(amount * 255.0);
    for (int y = 0; y < image.height(); ++y) {
        uchar *row = image.scanLine(y);
        for (int x = 0; x < image.width(); ++x) {
            uchar *p = row + x * 4;
            for (int c = 0; c < 3; ++c) p[c] = uchar(qBound(0, int(p[c]) + offset, 255));
        }
    }
    return true;
}

bool grayscale(QImage &image, const QVariantMap &parameters, QString *error)
{
    if (!parameters.isEmpty()) {
        *error = QStringLiteral("grayscale takes no parameters");
        return false;
    }
    for (int y = 0; y < image.height(); ++y) {
        uchar *row = image.scanLine(y);
        for (int x = 0; x < image.width(); ++x) {
            uchar *p = row + x * 4;
            const int luma = qBound(0, qRound(0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2]), 255);
            p[0] = p[1] = p[2] = uchar(luma);
        }
    }
    return true;
}

bool enabled(const QVariantMap &effect, bool *isEnabled, QString *error)
{
    const QVariant value = effect.value(QStringLiteral("enabled"));
    if (!value.isValid()) { *isEnabled = true; return true; }
    if (value.metaType().id() != QMetaType::Bool) {
        *error = QStringLiteral("effect enabled must be a boolean");
        return false;
    }
    *isEnabled = value.toBool();
    return true;
}
}

MotionEffectRegistry::MotionEffectRegistry()
{
    m_processors.insert(QStringLiteral("brightness"), brightness);
    m_processors.insert(QStringLiteral("grayscale"), grayscale);
}

bool MotionEffectRegistry::registerProcessor(const QString &effectId, Processor processor)
{
    if (effectId.isEmpty() || effectId.size() > kMaxEffectIdLength || !processor
        || m_processors.contains(effectId)) return false;
    m_processors.insert(effectId, std::move(processor));
    return true;
}

bool MotionEffectRegistry::validateEffect(const QVariantMap &effect, QString *error) const
{
    const QVariant idValue = effect.value(QStringLiteral("effectId"));
    if (idValue.metaType().id() != QMetaType::QString || idValue.toString().isEmpty()
        || idValue.toString().size() > kMaxEffectIdLength) {
        *error = QStringLiteral("Effect needs a non-empty bounded effectId string");
        return false;
    }
    bool active = true;
    if (!enabled(effect, &active, error)) return false;
    if (!active) return true;
    const QString id = idValue.toString();
    const auto it = m_processors.constFind(id);
    if (it == m_processors.cend()) {
        *error = QStringLiteral("Unsupported enabled motion effect: %1").arg(id);
        return false;
    }
    const QVariant parameters = effect.value(QStringLiteral("parameters"));
    if (parameters.isValid() && parameters.metaType().id() != QMetaType::QVariantMap) {
        *error = QStringLiteral("Effect parameters must be an object");
        return false;
    }
    if (parameters.toMap().size() > kMaxParameterCount) {
        *error = QStringLiteral("Effect parameter object exceeds the 32-entry bound");
        return false;
    }
    // Built-ins validate parameters before the renderer allocates any output.
    if (id == QLatin1String("brightness")) {
        const auto map = parameters.toMap();
        if (map.size() > 1 || (map.contains(QStringLiteral("amount")) == false && !map.isEmpty())) {
            *error = QStringLiteral("brightness accepts only the 'amount' parameter");
            return false;
        }
        bool ok = true;
        double amount = 0.0;
        if (map.contains(QStringLiteral("amount"))) {
            const QVariant amountValue = map.value(QStringLiteral("amount"));
            if (!numeric(amountValue)) ok = false;
            else amount = amountValue.toDouble(&ok);
        }
        if (!ok || !std::isfinite(amount) || amount < -1.0 || amount > 1.0) {
            *error = QStringLiteral("brightness amount must be finite and within [-1,1]");
            return false;
        }
    } else if (id == QLatin1String("grayscale") && !parameters.toMap().isEmpty()) {
        *error = QStringLiteral("grayscale takes no parameters");
        return false;
    }
    return true;
}

bool MotionEffectRegistry::validate(const QVariantList &effects, QString *error) const
{
    if (effects.size() > kMaxEffects) {
        if (error) *error = QStringLiteral("A rendered layer may contain at most 64 effects");
        return false;
    }
    for (const QVariant &value : effects) {
        if (value.metaType().id() != QMetaType::QVariantMap) {
            if (error) *error = QStringLiteral("Layer effect records must be objects");
            return false;
        }
        QString message;
        if (!validateEffect(value.toMap(), &message)) {
            if (error) *error = message;
            return false;
        }
    }
    return true;
}

MotionEffectResult MotionEffectRegistry::apply(const QImage &source, const QVariantList &effects) const
{
    MotionEffectResult result;
    QString message;
    if (source.isNull() || !source.colorSpace().isValid()
        || source.colorSpace().primaries() != QColorSpace::Primaries::SRgb
        || source.colorSpace().transferFunction() != QColorSpace::TransferFunction::SRgb) {
        result.error = QStringLiteral("Motion effects require an explicitly tagged sRGB source image");
        return result;
    }
    if (!validate(effects, &message)) { result.error = message; return result; }
    result.image = source.convertToFormat(QImage::Format_RGBA8888);
    result.image.setColorSpace(QColorSpace(QColorSpace::SRgb));
    for (const QVariant &value : effects) {
        const QVariantMap effect = value.toMap();
        bool active = true;
        enabled(effect, &active, &message);
        if (!active) continue;
        const auto processor = m_processors.constFind(effect.value(QStringLiteral("effectId")).toString());
        if (!processor.value()(result.image, effect.value(QStringLiteral("parameters")).toMap(), &message)) {
            result.image = {};
            result.error = QStringLiteral("Effect %1 failed: %2").arg(effect.value(QStringLiteral("effectId")).toString(), message);
            return result;
        }
        if (result.image.isNull() || result.image.size() != source.size()) {
            result.image = {};
            result.error = QStringLiteral("Effect %1 changed the bounded source dimensions").arg(effect.value(QStringLiteral("effectId")).toString());
            return result;
        }
        result.image = result.image.convertToFormat(QImage::Format_RGBA8888);
        result.image.setColorSpace(QColorSpace(QColorSpace::SRgb));
    }
    return result;
}

} // namespace QindaTK
