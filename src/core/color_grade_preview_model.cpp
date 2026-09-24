// SPDX-License-Identifier: LGPL-3.0-or-later
#include "color_grade_preview_model.h"

#include <QBuffer>
#include <QColorSpace>

namespace QindaTK {

ColorGradePreviewModel::ColorGradePreviewModel(QObject *parent)
    : QObject(parent), m_beforeImage(fixtureFrame())
{
    m_context.document = ColorProfile::sRgb();
    m_context.working = ColorProfile::linearSRgb();
    m_context.display = ColorProfile::sRgb();
    m_context.output = ColorProfile::sRgb();
    m_beforeImageUrl = pngDataUrl(m_beforeImage);
    m_beforeScopes = m_scopeModel.analyzeFrame(m_beforeImage, QStringLiteral("grade-before-0"));
    render({});
}

QImage ColorGradePreviewModel::fixtureFrame()
{
    const QRgb bars[] = {qRgb(235, 235, 235), qRgb(235, 235, 16), qRgb(16, 235, 235),
                         qRgb(16, 235, 16), qRgb(235, 16, 235), qRgb(235, 16, 16),
                         qRgb(16, 16, 235), qRgb(16, 16, 16)};
    QImage image(512, 192, QImage::Format_RGB32);
    for (int y = 0; y < image.height(); ++y) {
        auto *row = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            const QRgb base = bars[x * 8 / image.width()];
            const int amount = y < image.height() / 2 ? y / 3 : 0;
            row[x] = qRgb(qMin(255, qRed(base) + amount), qMin(255, qGreen(base) + amount),
                          qMin(255, qBlue(base) + amount));
        }
    }
    image.setColorSpace(QColorSpace(QColorSpace::SRgb));
    return image;
}

QString ColorGradePreviewModel::pngDataUrl(const QImage &image)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG")) return {};
    return QStringLiteral("data:image/png;base64,") + QString::fromLatin1(bytes.toBase64());
}

QVariantMap ColorGradePreviewModel::render(const QVariantMap &parameters)
{
    OperationError error;
    const auto parsed = ColorGradeParameters::fromVariantMap(parameters, &error);
    if (!parsed) {
        m_errorMessage = error.message;
        emit changed();
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), m_errorMessage}};
    }
    m_context.generation = ++m_revision;
    const auto result = m_processor.process(m_beforeImage, *parsed, m_context);
    if (!result) {
        m_errorMessage = result.error.message;
        emit changed();
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), m_errorMessage}};
    }
    m_afterImageUrl = pngDataUrl(result.image);
    m_afterScopes = m_scopeModel.analyzeFrame(result.image,
        QStringLiteral("grade-after-%1").arg(m_revision));
    m_errorMessage.clear();
    emit changed();
    return {{QStringLiteral("ok"), !m_afterImageUrl.isEmpty()},
            {QStringLiteral("generation"), QVariant::fromValue<qulonglong>(m_revision)},
            {QStringLiteral("beforeScopes"), m_beforeScopes},
            {QStringLiteral("afterScopes"), m_afterScopes},
            {QStringLiteral("error"), m_errorMessage}};
}

} // namespace QindaTK
