// SPDX-License-Identifier: LGPL-3.0-or-later
#include "color_engine.h"
#include "color_lcms_p.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <list>
#include <mutex>

namespace QindaTK {

struct ColorEngine::Private {
    struct Transform {
        QString key;
        ColorProfile source;
        ColorProfile destination;
        ColorProfile proof;
        cmsHTRANSFORM handle = nullptr;
        ~Transform() { if (handle) cmsDeleteTransform(handle); }
    };
    mutable std::mutex mutex;
    // AGENT-GUARD: context precedes cache, so transforms die before their
    // context. Error callbacks and all LCMS calls run under mutex.
    ColorLcms::Context context;
    std::list<std::unique_ptr<Transform>> cache;
    int limit = 32;
};

ColorEngine::ColorEngine(int maxCachedTransforms) : d(std::make_unique<Private>())
{
    d->limit = std::clamp(maxCachedTransforms, 1, 256);
}
ColorEngine::~ColorEngine() = default;

int ColorEngine::cachedTransformCount() const
{
    std::lock_guard lock(d->mutex);
    return int(d->cache.size());
}

ColorTransformResult ColorEngine::convert(const ColorTransformRequest &request)
{
    ColorTransformResult result;
    result.operation = request.operation;
    result.generation = request.context.generation;
    auto fail = [&result](ErrorCode code, const QString &field, const QString &message) {
        result.error = {code, message, field, QStringLiteral("lcms2")};
        return result;
    };
    ColorProfile destination;
    ColorProfile proof;
    switch (request.operation) {
    case ColorOperation::Working: destination = request.context.working; break;
    case ColorOperation::Display: destination = request.context.display; break;
    case ColorOperation::Output: destination = request.context.output; break;
    case ColorOperation::ProofPreview:
        destination = request.context.display;
        proof = request.context.proof;
        break;
    default:
        return fail(ErrorCode::InvalidArgument, QStringLiteral("operation"),
                    QStringLiteral("Unknown color operation"));
    }
    if (!request.source.isValid() || !destination.isValid())
        return fail(ErrorCode::Unavailable, QStringLiteral("profile"),
                    QStringLiteral("Source and destination profiles must be resolved"));
    if (request.operation == ColorOperation::ProofPreview && !proof.isValid())
        return fail(ErrorCode::Unavailable, QStringLiteral("context.proof"),
                    QStringLiteral("Soft proofing requires a resolved proof profile"));
    if (!request.value.isValid())
        return fail(ErrorCode::InvalidArgument, QStringLiteral("value"),
                    QStringLiteral("Color must have finite RGB, alpha in [0,1], and encoding IDs"));
    if (request.value.profileId != request.source.id()
        || request.value.transferId != request.source.transferId())
        return fail(ErrorCode::Validation, QStringLiteral("value.profileId/transferId"),
                    QStringLiteral("Color encoding does not match the source ICC profile"));
    cmsUInt32Number intent;
    switch (request.context.intent) {
    case ColorIntent::Perceptual: intent = INTENT_PERCEPTUAL; break;
    case ColorIntent::RelativeColorimetric: intent = INTENT_RELATIVE_COLORIMETRIC; break;
    case ColorIntent::Saturation: intent = INTENT_SATURATION; break;
    case ColorIntent::AbsoluteColorimetric: intent = INTENT_ABSOLUTE_COLORIMETRIC; break;
    default:
        return fail(ErrorCode::InvalidArgument, QStringLiteral("intent"),
                    QStringLiteral("Unknown rendering intent"));
    }
    if (intent != INTENT_RELATIVE_COLORIMETRIC || request.context.blackPointCompensation)
        return fail(ErrorCode::Unsupported, QStringLiteral("intent/blackPointCompensation"),
                    QStringLiteral("This backend slice supports relative colorimetric intent without BPC"));
    cmsUInt32Number proofIntent = INTENT_RELATIVE_COLORIMETRIC;
    if (request.operation == ColorOperation::ProofPreview) {
        switch (request.context.proofIntent) {
        case ColorIntent::RelativeColorimetric: break;
        case ColorIntent::Perceptual:
        case ColorIntent::Saturation:
        case ColorIntent::AbsoluteColorimetric:
            return fail(ErrorCode::Unsupported, QStringLiteral("context.proofIntent"),
                        QStringLiteral("This backend supports relative-colorimetric proofing"));
        default:
            return fail(ErrorCode::InvalidArgument, QStringLiteral("context.proofIntent"),
                        QStringLiteral("Unknown proofing intent"));
        }
    }
    const double input[3] = {request.value.red, request.value.green, request.value.blue};
    for (double channel : input) {
        if (std::abs(channel) > std::numeric_limits<float>::max())
            return fail(ErrorCode::InvalidArgument, QStringLiteral("value.rgb"),
                        QStringLiteral("RGB component exceeds Float32 range"));
    }
    const float rgb[3] = {float(input[0]), float(input[1]), float(input[2])};
    float output[3] = {};
    std::lock_guard lock(d->mutex);
    if (!d->context.handle)
        return fail(ErrorCode::BackendFailure, QStringLiteral("backend"),
                    QStringLiteral("Cannot create color engine"));
    d->context.diagnostic.clear();
    QString key = request.source.id() + QLatin1Char('/') + destination.id()
        + QLatin1Char('/') + QString::number(intent) + QLatin1Char('/')
        + QString::number(request.context.blackPointCompensation);
    if (request.operation == ColorOperation::ProofPreview)
        key += QStringLiteral("/proof/") + proof.id() + QLatin1Char('/')
            + QString::number(proofIntent);
    auto found = std::find_if(d->cache.begin(), d->cache.end(), [&key](const auto &entry) {
        return entry->key == key;
    });
    if (found != d->cache.end()) {
        d->cache.splice(d->cache.begin(), d->cache, found);
    } else {
        const auto sourceBytes = request.source.iccBytes();
        const auto targetBytes = destination.iccBytes();
        ColorLcms::Profile source(cmsOpenProfileFromMemTHR(d->context.handle,
            sourceBytes.constData(), cmsUInt32Number(sourceBytes.size())));
        ColorLcms::Profile target(cmsOpenProfileFromMemTHR(d->context.handle,
            targetBytes.constData(), cmsUInt32Number(targetBytes.size())));
        const QByteArray proofBytes = proof.isValid() ? proof.iccBytes() : QByteArray{};
        ColorLcms::Profile proofProfile(proof.isValid()
            ? cmsOpenProfileFromMemTHR(d->context.handle, proofBytes.constData(),
                                       cmsUInt32Number(proofBytes.size())) : nullptr);
        if (!source.handle || !target.handle)
            return fail(ErrorCode::BackendFailure, QStringLiteral("profile"),
                        QStringLiteral("Cannot reopen validated ICC container: ") + d->context.diagnostic);
        if (proof.isValid() && !proofProfile.handle)
            return fail(ErrorCode::BackendFailure, QStringLiteral("context.proof"),
                        QStringLiteral("Cannot reopen proof ICC profile: ") + d->context.diagnostic);
        if (!cmsIsIntentSupported(source.handle, intent, LCMS_USED_AS_INPUT)
            || !cmsIsIntentSupported(target.handle, intent, LCMS_USED_AS_OUTPUT))
            return fail(ErrorCode::Unsupported, QStringLiteral("intent"),
                        QStringLiteral("ICC profile does not support the requested direction/intent"));
        if (proof.isValid()
            && !cmsIsIntentSupported(proofProfile.handle, proofIntent, LCMS_USED_AS_PROOF))
            return fail(ErrorCode::Unsupported, QStringLiteral("context.proofIntent"),
                        QStringLiteral("Proof ICC profile does not support the requested intent"));
        constexpr cmsUInt32Number flags = cmsFLAGS_NOCACHE | cmsFLAGS_NOOPTIMIZE;
        auto transform = std::make_unique<Private::Transform>();
        transform->key = key;
        transform->source = request.source;
        transform->destination = destination;
        transform->proof = proof;
        if (proof.isValid()) {
            transform->handle = cmsCreateProofingTransformTHR(
                d->context.handle, source.handle, TYPE_RGB_FLT,
                target.handle, TYPE_RGB_FLT, proofProfile.handle, intent, proofIntent,
                flags | cmsFLAGS_SOFTPROOFING);
        } else {
            transform->handle = cmsCreateTransformTHR(d->context.handle, source.handle,
                TYPE_RGB_FLT, target.handle, TYPE_RGB_FLT, intent, flags);
        }
        if (!transform->handle || !d->context.diagnostic.isEmpty())
            return fail(ErrorCode::BackendFailure, QStringLiteral("transform"),
                        QStringLiteral("Cannot construct ICC transform: ") + d->context.diagnostic);
        if (int(d->cache.size()) == d->limit) d->cache.pop_back();
        d->cache.push_front(std::move(transform));
    }
    cmsDoTransform(d->cache.front()->handle, rgb, output, 1);
    for (float channel : output) {
        if (!std::isfinite(channel))
            return fail(ErrorCode::BackendFailure, QStringLiteral("result.rgb"),
                        QStringLiteral("ICC transform produced nonfinite RGB"));
        result.outsideNominalRange |= channel < 0.0f || channel > 1.0f;
    }
    if (!d->context.diagnostic.isEmpty())
        return fail(ErrorCode::BackendFailure, QStringLiteral("transform"), d->context.diagnostic);
    // AGENT-CONTRACT: ColorValue alpha is straight, linear coverage. Never
    // send it through ICC TRCs or discard hidden RGB at alpha zero.
    result.value = ColorValue{output[0], output[1], output[2], request.value.alpha,
                              destination.id(), destination.transferId()};
    return result;
}

} // namespace QindaTK
