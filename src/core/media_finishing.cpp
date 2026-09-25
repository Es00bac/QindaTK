// SPDX-License-Identifier: LGPL-3.0-or-later
#include "media_finishing.h"
#include "caption_frame_renderer.h"

#include <QColorSpace>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <algorithm>

namespace QindaTK {
namespace {
bool neutral(const ColorGradeParameters &grade)
{
    return grade.exposureEv == 0.0 && grade.lift.isNull() && grade.gamma.isNull() && grade.gain.isNull();
}

QJsonObject pointJson(const QPointF &point)
{
    return {{QStringLiteral("x"), point.x()}, {QStringLiteral("y"), point.y()}};
}

std::optional<QVector<CaptionCueRecord>> cuesFrom(const QJsonValue &value, QString *language, QString *error)
{
    if (value.isUndefined() || value.isNull()) return QVector<CaptionCueRecord>();
    QVector<CaptionCueRecord> cues;
    QString trackId, parsedLanguage;
    if (!CaptionTrackCodec::fromJson(value.toObject(), &cues, &trackId, &parsedLanguage, error))
        return std::nullopt;
    if (language) *language = parsedLanguage;
    return cues;
}
} // namespace

bool MediaFinishing::changesPictures() const
{
    return (grade && !neutral(*grade)) || !burnedCaptions.isEmpty();
}

ColorContextSnapshot MediaFinishing::gradeContext()
{
    ColorContextSnapshot context;
    context.document = ColorProfile::sRgb();
    context.working = ColorProfile::linearSRgb();
    context.display = ColorProfile::sRgb();
    context.output = ColorProfile::sRgb();
    return context;
}

OperationError MediaFinishing::apply(QImage *frame, qint64 programMs) const
{
    if (!frame || frame->isNull())
        return {ErrorCode::InvalidArgument, QStringLiteral("No frame to finish"), {}, QStringLiteral("media-finishing")};
    if (grade && !neutral(*grade)) {
        QImage input = *frame;
        if (!input.colorSpace().isValid()) input.setColorSpace(QColorSpace(QColorSpace::SRgb));
        const ColorGradeResult graded = ColorGradeProcessor().process(input, *grade, gradeContext());
        if (!graded) return graded.error;
        const QImage::Format format = frame->hasAlphaChannel() ? QImage::Format_ARGB32 : QImage::Format_RGB32;
        *frame = graded.image.convertToFormat(format);
        frame->setColorSpace(QColorSpace(QColorSpace::SRgb));
    }
    if (!burnedCaptions.isEmpty()) CaptionBurnIn::burn(frame, burnedCaptions, programMs);
    return {};
}

QString MediaFinishing::digest() const
{
    if (isNull()) return {};
    return QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(toJson()).toJson(QJsonDocument::Compact),
                                                        QCryptographicHash::Sha1).toHex());
}

QJsonObject MediaFinishing::toJson() const
{
    QJsonObject json;
    if (grade) {
        json.insert(QStringLiteral("grade"), QJsonObject{{QStringLiteral("exposureEV"), grade->exposureEv},
            {QStringLiteral("lift"), pointJson(grade->lift)}, {QStringLiteral("gamma"), pointJson(grade->gamma)},
            {QStringLiteral("gain"), pointJson(grade->gain)}});
    }
    if (!burnedCaptions.isEmpty())
        json.insert(QStringLiteral("burnedCaptions"),
                    CaptionTrackCodec::toJson(burnedCaptions, QStringLiteral("burned"), QStringLiteral("und")));
    if (!embeddedCaptions.isEmpty())
        json.insert(QStringLiteral("embeddedCaptions"),
                    CaptionTrackCodec::toJson(embeddedCaptions, QStringLiteral("embedded"),
                                              embeddedLanguage.isEmpty() ? QStringLiteral("und") : embeddedLanguage));
    return json;
}

std::optional<MediaFinishing> MediaFinishing::fromJson(const QJsonObject &json, QString *error)
{
    MediaFinishing finishing;
    QString why;
    if (json.contains(QStringLiteral("grade"))) {
        OperationError gradeError;
        finishing.grade = ColorGradeParameters::fromVariantMap(
            json.value(QStringLiteral("grade")).toObject().toVariantMap(), &gradeError);
        if (!finishing.grade) {
            if (error) *error = gradeError.message;
            return std::nullopt;
        }
    }
    const auto burned = cuesFrom(json.value(QStringLiteral("burnedCaptions")), nullptr, &why);
    QString language;
    const auto embedded = burned ? cuesFrom(json.value(QStringLiteral("embeddedCaptions")), &language, &why)
                                 : std::nullopt;
    if (!burned || !embedded) {
        if (error) *error = why;
        return std::nullopt;
    }
    finishing.burnedCaptions = *burned;
    finishing.embeddedCaptions = *embedded;
    finishing.embeddedLanguage = embedded->isEmpty() ? QString() : language;
    return finishing;
}

QVector<MediaSubtitleCue> MediaFinishing::subtitlesFrom(qint64 offsetMs) const
{
    QVector<MediaSubtitleCue> result;
    for (const CaptionCueRecord &cue : embeddedCaptions) {
        const qint64 end = cue.outMs - offsetMs;
        if (end <= 0 || cue.text.isEmpty()) continue;
        result.append({std::max<qint64>(0, cue.inMs - offsetMs), end, cue.text});
    }
    std::stable_sort(result.begin(), result.end(), [](const MediaSubtitleCue &a, const MediaSubtitleCue &b) {
        return a.startMs < b.startMs;
    });
    return result;
}

} // namespace QindaTK
