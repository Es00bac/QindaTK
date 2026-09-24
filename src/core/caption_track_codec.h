// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVector>

struct CaptionCueRecord {
    QString id;
    qint64 inMs = 0;
    qint64 outMs = 0;
    QString text;
    QString language = QStringLiteral("und");
    QString styleId = QStringLiteral("default");
    int lane = 0;
};

namespace CaptionTrackCodec {
bool parseTime(const QString &text, qint64 *milliseconds);
QString formatTime(qint64 milliseconds, QChar decimal = QLatin1Char('.'));
// WebVTT (docs/caption-rendering.md "Formats"): the header's `Language:` line is the
// track language and a cue whose whole text is one `<lang tag>` span carries
// its own language. Cue text is plain text: &amp; &lt; &gt; &nbsp; &lrm; and
// &rlm; are decoded on import and & < > are escaped on export; other markup
// stays literal text.
bool parseWebVtt(const QString &text, const QString &language, QVector<CaptionCueRecord> *cues, QString *error,
                 QString *headerLanguage = nullptr);
bool parseSrt(const QString &text, const QString &language, QVector<CaptionCueRecord> *cues, QString *error);
// With a trackLanguage, writes `Language:` and wraps each cue in another
// language in a `<lang>` span, so a WebVTT round trip keeps every language.
QString toWebVtt(const QVector<CaptionCueRecord> &cues, const QString &trackLanguage = QString());
QString toSrt(const QVector<CaptionCueRecord> &cues);
QJsonObject toJson(const QVector<CaptionCueRecord> &cues, const QString &trackId, const QString &language);
bool fromJson(const QJsonObject &json, QVector<CaptionCueRecord> *cues, QString *trackId,
              QString *language, QString *error);
}
