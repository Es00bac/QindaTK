// SPDX-License-Identifier: LGPL-3.0-or-later
#include "color_profile.h"
#include "color_lcms_p.h"

#include <QColorSpace>
#include <QCryptographicHash>
#include <QtEndian>

namespace QindaTK {

struct ColorProfile::Data {
    QByteArray bytes;
    QString id;
    QString description;
};

ColorProfile ColorProfile::fromIcc(const QByteArray &bytes, OperationError *error)
{
    if (error) *error = {};
    auto fail = [error](ErrorCode code, const QString &message) {
        if (error) *error = {code, message, QStringLiteral("profile"), QStringLiteral("lcms2")};
        return ColorProfile{};
    };
    // Resource limit is part of the import contract, independent of ICC's
    // theoretical 32-bit size. Reject trailing data as well as truncation.
    constexpr qsizetype maxBytes = 16 * 1024 * 1024;
    if (bytes.size() < 132 || bytes.size() > maxBytes)
        return fail(ErrorCode::Validation, QStringLiteral("ICC size must be 132 bytes to 16 MiB"));
    auto word = [&bytes](qsizetype offset) {
        return qFromBigEndian<quint32>(bytes.constData() + offset);
    };
    if (word(0) != quint32(bytes.size()) || bytes.mid(36, 4) != QByteArrayLiteral("acsp"))
        return fail(ErrorCode::Validation, QStringLiteral("Invalid ICC header or declared size"));
    const quint32 count = word(128);
    if (count > quint32((bytes.size() - 132) / 12))
        return fail(ErrorCode::Validation, QStringLiteral("ICC tag table is truncated"));
    const quint32 dataStart = 132 + count * 12;
    for (quint32 i = 0; i < count; ++i) {
        const quint32 offset = word(132 + i * 12 + 4);
        const quint32 size = word(132 + i * 12 + 8);
        if (offset < dataStart || offset % 4 || size < 8
            || quint64(offset) + size > quint64(bytes.size()))
            return fail(ErrorCode::Validation, QStringLiteral("ICC tag lies outside its data section"));
    }
    ColorLcms::Context context;
    if (!context.handle)
        return fail(ErrorCode::BackendFailure, QStringLiteral("Cannot create ICC parser"));
    ColorLcms::Profile profile(cmsOpenProfileFromMemTHR(context.handle, bytes.constData(),
                                                     cmsUInt32Number(bytes.size())));
    if (!profile.handle)
        return fail(ErrorCode::Validation, context.diagnostic);
    const auto profileClass = cmsGetDeviceClass(profile.handle);
    if (cmsGetColorSpace(profile.handle) != cmsSigRgbData
        || (cmsGetPCS(profile.handle) != cmsSigXYZData && cmsGetPCS(profile.handle) != cmsSigLabData)
        || (profileClass != cmsSigInputClass && profileClass != cmsSigDisplayClass
            && profileClass != cmsSigOutputClass && profileClass != cmsSigColorSpaceClass))
        return fail(ErrorCode::Unsupported, QStringLiteral("Only ordinary RGB ICC profiles are supported"));
    if (!cmsIsIntentSupported(profile.handle, INTENT_RELATIVE_COLORIMETRIC, LCMS_USED_AS_INPUT)
        && !cmsIsIntentSupported(profile.handle, INTENT_RELATIVE_COLORIMETRIC, LCMS_USED_AS_OUTPUT))
        return fail(ErrorCode::Unsupported, QStringLiteral("ICC profile has no usable RGB direction"));
    auto data = std::make_shared<Data>();
    // QByteArray::fromRawData callers must not retain ownership of our bytes.
    data->bytes = QByteArray(bytes.constData(), bytes.size());
    data->id = QStringLiteral("icc-sha256:") + QString::fromLatin1(
        QCryptographicHash::hash(data->bytes, QCryptographicHash::Sha256).toHex());
    char description[512] = {};
    cmsGetProfileInfoASCII(profile.handle, cmsInfoDescription, "en", "US", description,
                           sizeof(description));
    data->description = QString::fromUtf8(description);
    ColorProfile result;
    result.m_data = std::move(data);
    return result;
}

ColorProfile ColorProfile::sRgb()
{
    static const auto profile = fromIcc(QColorSpace(QColorSpace::SRgb).iccProfile());
    return profile;
}

ColorProfile ColorProfile::linearSRgb()
{
    static const auto profile = fromIcc(QColorSpace(QColorSpace::SRgbLinear).iccProfile());
    return profile;
}

QByteArray ColorProfile::iccBytes() const { return m_data ? m_data->bytes : QByteArray{}; }
QString ColorProfile::id() const { return m_data ? m_data->id : QString{}; }
QString ColorProfile::transferId() const { return m_data ? QStringLiteral("icc") : QString{}; }
QString ColorProfile::description() const { return m_data ? m_data->description : QString{}; }

} // namespace QindaTK
