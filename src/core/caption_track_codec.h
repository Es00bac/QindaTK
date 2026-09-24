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
bool parseWebVtt(const QString &text, const QString &language, QVector<CaptionCueRecord> *cues, QString *error);
bool parseSrt(const QString &text, const QString &language, QVector<CaptionCueRecord> *cues, QString *error);
QString toWebVtt(const QVector<CaptionCueRecord> &cues);
QString toSrt(const QVector<CaptionCueRecord> &cues);
QJsonObject toJson(const QVector<CaptionCueRecord> &cues, const QString &trackId, const QString &language);
bool fromJson(const QJsonObject &json, QVector<CaptionCueRecord> *cues, QString *trackId,
              QString *language, QString *error);
}
