// SPDX-License-Identifier: LGPL-3.0-or-later
#include "color_grade_processor.h"

#include <QColorSpace>
#include <algorithm>
#include <array>
#include <cmath>

namespace QindaTK {
namespace {
constexpr qint64 kMaximumPixels = 16 * 1024 * 1024;

QPointF readPoint(const QVariantMap &map, const QString &key, bool *ok)
{
    const QVariant value = map.value(key);
    if (!value.isValid()) { *ok = true; return {}; }
    const QVariantMap point = value.toMap();
    bool xOk = false, yOk = false;
    const double x = point.value(QStringLiteral("x")).toDouble(&xOk);
    const double y = point.value(QStringLiteral("y")).toDouble(&yOk);
    *ok = xOk && yOk;
    return {x, y};
}

// Opponent basis: x moves red against green; y moves blue against yellow.
// Each axis sums to zero, so wheel movement does not inject neutral luma.
std::array<double, 3> wheelRgb(const QPointF &point)
{
    return {0.5 * point.x() - point.y() / 3.0,
            -0.5 * point.x() - point.y() / 3.0,
            2.0 * point.y() / 3.0};
}

int byte(double value)
{
    return qBound(0, qRound(qBound(0.0, value, 1.0) * 255.0), 255);
}
}

bool ColorGradeParameters::isValid() const
{
    const auto finitePoint = [](const QPointF &p) {
        return std::isfinite(p.x()) && std::isfinite(p.y())
            && std::abs(p.x()) <= 1.0 && std::abs(p.y()) <= 1.0;
    };
    return std::isfinite(exposureEv) && exposureEv >= -8.0 && exposureEv <= 8.0
        && finitePoint(lift) && finitePoint(gamma) && finitePoint(gain);
}

std::optional<ColorGradeParameters> ColorGradeParameters::fromVariantMap(const QVariantMap &map,
                                                                          OperationError *error)
{
    if (error) *error = {};
    ColorGradeParameters result;
    bool exposureOk = true;
    if (map.contains(QStringLiteral("exposureEV")))
        result.exposureEv = map.value(QStringLiteral("exposureEV")).toDouble(&exposureOk);
    bool liftOk = false, gammaOk = false, gainOk = false;
    result.lift = readPoint(map, QStringLiteral("lift"), &liftOk);
    result.gamma = readPoint(map, QStringLiteral("gamma"), &gammaOk);
    result.gain = readPoint(map, QStringLiteral("gain"), &gainOk);
    if (!exposureOk || !liftOk || !gammaOk || !gainOk || !result.isValid()) {
        if (error) *error = {ErrorCode::Validation,
            QStringLiteral("Grade values must be finite; exposure is [-8,8] EV and wheel x/y are [-1,1]"),
            QStringLiteral("parameters"), QStringLiteral("qindatk.color-grade")};
        return std::nullopt;
    }
    return result;
}

ColorGradeResult ColorGradeProcessor::process(const QImage &source,
                                              const ColorGradeParameters &parameters,
                                              const ColorContextSnapshot &context) const
{
    ColorGradeResult result;
    result.generation = context.generation;
    auto fail = [&result](ErrorCode code, const QString &field, const QString &message) {
        result.error = {code, message, field, QStringLiteral("qindatk.color-grade")};
        return result;
    };
    if (source.isNull())
        return fail(ErrorCode::InvalidArgument, QStringLiteral("source"), QStringLiteral("Image is empty"));
    if (qint64(source.width()) * source.height() > kMaximumPixels)
        return fail(ErrorCode::Validation, QStringLiteral("source.size"), QStringLiteral("Image exceeds the 16-megapixel processing bound"));
    if (!parameters.isValid())
        return fail(ErrorCode::Validation, QStringLiteral("parameters"), QStringLiteral("Grade parameters are outside their finite supported range"));
    if (!source.colorSpace().isValid())
        return fail(ErrorCode::Unavailable, QStringLiteral("source.colorSpace"), QStringLiteral("Input image needs a valid ICC color-space tag"));
    if (!context.working.isValid() || !context.output.isValid())
        return fail(ErrorCode::Unavailable, QStringLiteral("context.profile"), QStringLiteral("Working and output profiles must be resolved"));
    if (context.working.id() != ColorProfile::linearSRgb().id())
        return fail(ErrorCode::Unsupported, QStringLiteral("context.working"), QStringLiteral("This grade space is explicitly linear sRGB"));
    if (context.intent != ColorIntent::RelativeColorimetric || context.blackPointCompensation)
        return fail(ErrorCode::Unsupported, QStringLiteral("context.intent"), QStringLiteral("Only relative-colorimetric conversion without BPC is supported"));

    const QByteArray workingBytes = context.working.iccBytes();
    const QByteArray outputBytes = context.output.iccBytes();
    const QColorSpace workingSpace = QColorSpace::fromIccProfile(workingBytes);
    const QColorSpace outputSpace = QColorSpace::fromIccProfile(outputBytes);
    if (!workingSpace.isValid() || !outputSpace.isValid())
        return fail(ErrorCode::BackendFailure, QStringLiteral("context.profile"), QStringLiteral("Qt could not reopen a validated RGB profile"));
    QImage linear = source.convertedToColorSpace(workingSpace, QImage::Format_RGBA8888);
    if (linear.isNull())
        return fail(ErrorCode::BackendFailure, QStringLiteral("source.colorSpace"), QStringLiteral("Input profile could not be converted to linear sRGB"));

    const auto lift = wheelRgb(parameters.lift);
    const auto gamma = wheelRgb(parameters.gamma);
    const auto gain = wheelRgb(parameters.gain);
    const double exponent[3] = {std::exp2(-gamma[0]), std::exp2(-gamma[1]), std::exp2(-gamma[2])};
    const double gainScale[3] = {std::max(0.0, 1.0 + gain[0]),
                                 std::max(0.0, 1.0 + gain[1]),
                                 std::max(0.0, 1.0 + gain[2])};
    const double exposure = std::exp2(parameters.exposureEv);
    for (int y = 0; y < linear.height(); ++y) {
        uchar *row = linear.scanLine(y);
        for (int x = 0; x < linear.width(); ++x) {
            uchar *p = row + x * 4;
            for (int channel = 0; channel < 3; ++channel) {
                const double lifted = p[channel] / 255.0 + 0.25 * lift[channel];
                const double signedGamma = std::copysign(std::pow(std::abs(lifted), exponent[channel]), lifted);
                p[channel] = uchar(byte(signedGamma * gainScale[channel] * exposure));
            }
        }
    }
    linear.setColorSpace(workingSpace);
    result.image = linear.convertedToColorSpace(outputSpace, QImage::Format_RGBA8888);
    if (result.image.isNull())
        return fail(ErrorCode::BackendFailure, QStringLiteral("context.output"), QStringLiteral("Graded image could not be converted to the declared output profile"));
    result.image.setColorSpace(outputSpace);
    return result;
}

} // namespace QindaTK
