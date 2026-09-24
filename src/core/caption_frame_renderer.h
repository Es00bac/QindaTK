// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "caption_track_model.h"

#include <QImage>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

// Burns committed CaptionTrackModel cues into one host-decoded video frame.
class CaptionFrameRenderer : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString imageDataUrl READ imageDataUrl NOTIFY changed)
    Q_PROPERTY(QString defaultOutputPath READ defaultOutputPath CONSTANT)
    Q_PROPERTY(QStringList activeCueIds READ activeCueIds NOTIFY changed)
    Q_PROPERTY(QString lastTimeMs READ lastTimeMs NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
public:
    explicit CaptionFrameRenderer(QObject *parent = nullptr);
    QString imageDataUrl() const { return m_imageDataUrl; }
    QString defaultOutputPath() const;
    QStringList activeCueIds() const { return m_activeCueIds; }
    QString lastTimeMs() const { return m_lastTimeMs; }
    QString error() const { return m_error; }

    QVariantMap renderFrame(const QImage &source, CaptionTrackModel *track,
                            const QString &timeMilliseconds);
    Q_INVOKABLE QVariantMap renderFixture(CaptionTrackModel *track,
                                         const QString &timeMilliseconds);
    Q_INVOKABLE bool saveRenderedFrame(const QString &path = QString());

signals:
    void changed();

private:
    static QImage fixtureFrame();
    static QString pngDataUrl(const QImage &image);
    QImage m_rendered;
    QString m_imageDataUrl;
    QStringList m_activeCueIds;
    QString m_lastTimeMs;
    QString m_error;
};
