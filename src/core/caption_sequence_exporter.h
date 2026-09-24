// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "caption_track_model.h"

#include <QImage>
#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class CaptionFrameRenderer;

// Publishes a bounded, timestamped sequence of caption-burned PNG frames.
class CaptionSequenceExporter : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString outputDirectory READ outputDirectory NOTIFY changed)
    Q_PROPERTY(QString manifestPath READ manifestPath NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QStringList framePaths READ framePaths NOTIFY changed)
    Q_PROPERTY(int frameCount READ frameCount NOTIFY changed)
public:
    explicit CaptionSequenceExporter(QObject *parent = nullptr);
    QString outputDirectory() const { return m_outputDirectory; }
    QString manifestPath() const { return m_manifestPath; }
    QString error() const { return m_error; }
    QStringList framePaths() const { return m_framePaths; }
    int frameCount() const { return m_framePaths.size(); }

    Q_INVOKABLE QVariantMap exportFrames(const QVariantList &sourceFrames,
                                         CaptionTrackModel *track,
                                         const QStringList &timeMilliseconds,
                                         const QString &targetDirectory);
    Q_INVOKABLE QVariantMap exportFixtureSequence(CaptionTrackModel *track,
                                                  const QStringList &timeMilliseconds,
                                                  const QString &targetDirectory);

signals:
    void changed();

private:
    QVariantMap publish(const QList<QImage> &frames, CaptionTrackModel *track,
                        const QStringList &times, const QString &targetDirectory);
    QString m_outputDirectory;
    QString m_manifestPath;
    QString m_error;
    QStringList m_framePaths;
};
