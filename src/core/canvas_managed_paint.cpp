// SPDX-License-Identifier: LGPL-3.0-or-later
#include "canvas_managed_paint.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QLinearGradient>
#include <algorithm>
#include <cmath>

namespace QindaTK::CanvasManagedPaint {
namespace {
constexpr qsizetype maximumPayload = 24 * 1024 * 1024;
OperationError problem(const QString &message)
{ return {ErrorCode::Validation, message, QStringLiteral("objects.managedFill"), QStringLiteral("canvas.color")}; }
QJsonObject parse(const QByteArray &payload, OperationError *error)
{
    if (payload.isEmpty() || payload.size() > maximumPayload) {
        if (error) *error = problem(QStringLiteral("Managed fill payload exceeds its limit"));
        return {};
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) *error = problem(QStringLiteral("Invalid managed fill JSON"));
        return {};
    }
    return document.object();
}
ColorValue readColor(const QJsonObject &o, const ColorProfile &profile)
{
    return {o.value("r").toDouble(qQNaN()), o.value("g").toDouble(qQNaN()),
            o.value("b").toDouble(qQNaN()), o.value("a").toDouble(qQNaN()),
            profile.id(), profile.transferId()};
}
QColor toQColor(const ColorValue &value)
{
    return QColor::fromRgbF(std::clamp(value.red, 0.0, 1.0),
                            std::clamp(value.green, 0.0, 1.0),
                            std::clamp(value.blue, 0.0, 1.0), value.alpha);
}
}

QByteArray solid(const ColorValue &value, const ColorProfile &profile, OperationError *error)
{
    if (error) *error = {};
    if (!profile.isValid() || !value.isValid() || value.profileId != profile.id() ||
        value.transferId != profile.transferId()) {
        if (error) *error = problem(QStringLiteral("Typed fill and ICC profile do not match"));
        return {};
    }
    return QJsonDocument(QJsonObject{{"kind", "solid"},
        {"profile", QString::fromLatin1(profile.iccBytes().toBase64())},
        {"r", value.red}, {"g", value.green}, {"b", value.blue}, {"a", value.alpha}})
        .toJson(QJsonDocument::Compact);
}

QByteArray gradient(const GradientDefinition &value, OperationError *error)
{
    const QByteArray definition = GradientDefinitionCodec::encode(value, error);
    if (definition.isEmpty()) return {};
    const QByteArray payload = QJsonDocument(QJsonObject{{"kind", "gradient"},
        {"definition", QString::fromLatin1(definition.toBase64())}}).toJson(QJsonDocument::Compact);
    if (payload.size() > maximumPayload) {
        if (error) *error = problem(QStringLiteral("Managed gradient exceeds its limit"));
        return {};
    }
    return payload;
}

QBrush brush(const QByteArray &payload, const QRectF &bounds, ColorEngine &engine, OperationError *error)
{
    if (error) *error = {};
    OperationError failure;
    const QJsonObject object = parse(payload, &failure);
    if (failure) { if (error) *error = failure; return {}; }
    const ColorProfile target = ColorProfile::sRgb();
    if (object.value("kind") == QLatin1String("solid")) {
        const QByteArray icc = QByteArray::fromBase64(object.value("profile").toString().toLatin1());
        const ColorProfile profile = ColorProfile::fromIcc(icc, &failure);
        const ColorValue value = readColor(object, profile);
        if (!profile.isValid() || !value.isValid()) {
            if (error) *error = problem(QStringLiteral("Invalid typed solid fill"));
            return {};
        }
        ColorContextSnapshot context;
        context.document = profile; context.working = target;
        const auto result = engine.convert({value, profile, context, ColorOperation::Working});
        if (!result.value) { if (error) *error = result.error; return {}; }
        return QBrush(toQColor(*result.value));
    }
    if (object.value("kind") == QLatin1String("gradient")) {
        const auto definition = GradientDefinitionCodec::decode(
            QByteArray::fromBase64(object.value("definition").toString().toLatin1()), &failure);
        if (!definition) { if (error) *error = failure; return {}; }
        QLinearGradient ramp(bounds.topLeft(), bounds.topRight());
        constexpr int samples = 65;
        for (int i = 0; i < samples; ++i) {
            const double position = double(i) / (samples - 1);
            const auto result = GradientEvaluator::evaluate(*definition, position, target, engine);
            if (!result.color) { if (error) *error = result.error; return {}; }
            ramp.setColorAt(position, toQColor(*result.color));
        }
        return QBrush(ramp);
    }
    if (error) *error = problem(QStringLiteral("Unsupported managed fill kind"));
    return {};
}

OperationError validate(const QByteArray &payload)
{
    if (payload.isEmpty()) return {};
    ColorEngine engine;
    OperationError error;
    brush(payload, QRectF(0,0,1,1), engine, &error);
    return error;
}
}
