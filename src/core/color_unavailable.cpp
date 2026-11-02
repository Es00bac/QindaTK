// SPDX-License-Identifier: LGPL-3.0-or-later
// AGENT-NOTE (D-354, docs/android.md): ColorProfile and ColorEngine for builds
// without lcms2 (QINDATK_ENABLE_LCMS2=OFF, the Android default). The API of
// color_profile.cpp / color_engine.cpp is kept so QML and C++ callers link and
// get an explicit ErrorCode::Unavailable instead of a wrong conversion.
// AGENT-GUARD: ColorProfile::Data is never created here, so every profile is
// invalid; do not "succeed" with a pass-through transform, which would show
// unmanaged colour as if it were managed.
#include "color_engine.h"
#include "color_profile.h"

namespace QindaTK {

namespace {
OperationError unavailable(const QString &field)
{
    return {ErrorCode::Unavailable,
            QStringLiteral("ICC colour management is not built into this QindaTK (no lcms2)"),
            field, QStringLiteral("lcms2")};
}
} // namespace

struct ColorProfile::Data {};

ColorProfile ColorProfile::fromIcc(const QByteArray &, OperationError *error)
{
    if (error) *error = unavailable(QStringLiteral("profile"));
    return {};
}
ColorProfile ColorProfile::sRgb() { return {}; }
ColorProfile ColorProfile::linearSRgb() { return {}; }
QByteArray ColorProfile::iccBytes() const { return {}; }
QString ColorProfile::id() const { return {}; }
QString ColorProfile::transferId() const { return {}; }
QString ColorProfile::description() const { return {}; }

struct ColorEngine::Private {};

ColorEngine::ColorEngine(int) : d(std::make_unique<Private>()) {}
ColorEngine::~ColorEngine() = default;
int ColorEngine::cachedTransformCount() const { return 0; }

ColorTransformResult ColorEngine::convert(const ColorTransformRequest &request)
{
    ColorTransformResult result;
    result.operation = request.operation;
    result.generation = request.context.generation;
    result.error = unavailable(QStringLiteral("backend"));
    return result;
}

} // namespace QindaTK
