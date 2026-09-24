// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "caption_track_codec.h"
#include "color_grade_processor.h"
#include "media_encoder.h"

#include <QImage>
#include <QJsonObject>
#include <QVector>
#include <optional>

namespace QindaTK {

// AGENT-CONTRACT (docs/color.md "Output grade", docs/caption-rendering.md "Burn-in",
// D-126): the last stage of a MediaProgram. MediaProgram::render applies it
// to every frame it returns, so the program monitor, the render queue and
// MediaExport all show the same graded, captioned pixels:
//   1. grade: ColorGradeProcessor with gradeContext() (the frame's own tag
//      into linear sRGB, output encoded sRGB, D-096's SDR program policy);
//   2. burn-in: CaptionBurnIn::burn at the frame's program time in whole
//      milliseconds, floor((startTicks - originTicks) * 1000 / timebase),
//      which decides [in, out) activity exactly for millisecond cues.
// Embedded captions never touch pixels; MediaExport writes them as a SubRip
// stream (subtitlesFrom). It is plain data, copied into each program.
struct MediaFinishing {
    std::optional<ColorGradeParameters> grade;
    QVector<CaptionCueRecord> burnedCaptions;
    QVector<CaptionCueRecord> embeddedCaptions;
    QString embeddedLanguage; // BCP 47 tag of the embedded stream

    bool isNull() const { return !grade && burnedCaptions.isEmpty() && embeddedCaptions.isEmpty(); }
    bool changesPictures() const;
    static ColorContextSnapshot gradeContext();
    // Grades then burns captions into `frame` in place; the frame keeps its
    // size and alpha format and is tagged sRGB when graded.
    OperationError apply(QImage *frame, qint64 programMs) const;
    // SHA-1 of toJson(); part of MediaProgram::cacheKey().
    QString digest() const;
    QJsonObject toJson() const;
    static std::optional<MediaFinishing> fromJson(const QJsonObject &json, QString *error = nullptr);
    // Embedded cues on an export whose first frame starts at program
    // millisecond `offsetMs`: shifted, cut at zero, sorted by start.
    QVector<MediaSubtitleCue> subtitlesFrom(qint64 offsetMs) const;
};

} // namespace QindaTK
