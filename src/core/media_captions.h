// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "application_contracts.h"
#include "caption_track_codec.h"

#include <QString>
#include <QVector>

namespace QindaTK {

struct MediaCaptionTrack {
    QVector<CaptionCueRecord> cues; // whole milliseconds of media time, in stream order
    QString language = QStringLiteral("und"); // the stream's language tag as written
    QString codec;                  // subrip, text or webvtt
    OperationError error;
};

// AGENT-CONTRACT (docs/caption-rendering.md "Embedded captions"): the reading side of
// MediaEncodeSettings::subtitles. MediaEncoder writes a Matroska SubRip
// (S_TEXT/UTF8) stream whose packets are the cue text, timed in
// milliseconds, with the track's BCP 47 tag as the stream language;
// readEmbedded() returns exactly those cues and that tag. Bitmap and ASS
// streams are reported Unsupported rather than approximated.
namespace MediaCaptions {
// The `ordinal`-th text subtitle stream of a local media file.
MediaCaptionTrack readEmbedded(const QString &path, int ordinal = 0);
} // namespace MediaCaptions

} // namespace QindaTK
