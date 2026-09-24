// SPDX-License-Identifier: LGPL-3.0-or-later
#include <QCoreApplication>
#include <QDebug>
#include <cmath>

#include <qindatk/color_engine.h>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const auto source = QindaTK::ColorProfile::sRgb();
    const auto target = QindaTK::ColorProfile::linearSRgb();
    if (!source.isValid() || !target.isValid()) {
        qCritical() << "Installed color profiles are unavailable";
        return 1;
    }
    QindaTK::ColorTransformRequest request;
    request.source = source;
    request.context.document = source;
    request.context.working = target;
    request.context.generation = 7;
    request.value = {.red = 0.5, .green = 0.5, .blue = 0.5, .alpha = 0.25,
                     .profileId = source.id(), .transferId = source.transferId()};
    QindaTK::ColorEngine engine;
    const auto result = engine.convert(request);
    if (!result.value || result.error || result.generation != 7
        || result.value->profileId != target.id()
        || result.value->transferId != target.transferId()
        || result.value->alpha != 0.25
        || std::abs(result.value->red - 0.2140411405) > 1e-5) {
        qCritical() << "Installed color conversion failed:" << result.error.message;
        return 2;
    }
    return 0;
}
