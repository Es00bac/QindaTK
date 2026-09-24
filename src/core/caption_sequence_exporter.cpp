// SPDX-License-Identifier: LGPL-3.0-or-later
#include "caption_sequence_exporter.h"

#include "caption_frame_renderer.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QSaveFile>
#include <QUuid>

namespace {
constexpr qsizetype MaxSequenceFrames = 10000;

QImage decodePngDataUrl(const QString &url)
{
    constexpr qsizetype prefixSize = sizeof("data:image/png;base64,") - 1;
    if (!url.startsWith(QLatin1String("data:image/png;base64,"))) return {};
    return QImage::fromData(QByteArray::fromBase64(url.mid(prefixSize).toLatin1()), "PNG");
}

QImage fixtureFrame(qsizetype index)
{
    QImage image(960, 540, QImage::Format_ARGB32_Premultiplied);
    image.fill(QColor(24 + int(index % 3) * 7, 54, 79));
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(58, 133, 118));
    painter.drawEllipse(QRectF(90 + index * 6, 120, 340, 330));
    painter.setBrush(QColor(190, 135, 82));
    painter.drawRoundedRect(QRectF(550 - index * 6, 150, 260, 230), 24, 24);
    painter.end();
    return image;
}
}

CaptionSequenceExporter::CaptionSequenceExporter(QObject *parent) : QObject(parent) {}

QVariantMap CaptionSequenceExporter::exportFrames(const QVariantList &sourceFrames,
                                                   CaptionTrackModel *track,
                                                   const QStringList &times,
                                                   const QString &targetDirectory)
{
    QList<QImage> frames;
    frames.reserve(sourceFrames.size());
    for (const QVariant &frame : sourceFrames) {
        if (frame.metaType().id() != QMetaType::QImage) {
            m_error = QStringLiteral("Every sequence frame must be a host-supplied QImage.");
            emit changed();
            return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), m_error}};
        }
        frames.append(frame.value<QImage>());
    }
    return publish(frames, track, times, targetDirectory);
}

QVariantMap CaptionSequenceExporter::exportFixtureSequence(CaptionTrackModel *track,
                                                            const QStringList &times,
                                                            const QString &targetDirectory)
{
    QList<QImage> frames;
    frames.reserve(times.size());
    for (qsizetype i = 0; i < times.size(); ++i) frames.append(fixtureFrame(i));
    return publish(frames, track, times, targetDirectory);
}

QVariantMap CaptionSequenceExporter::publish(const QList<QImage> &frames, CaptionTrackModel *track,
                                              const QStringList &times, const QString &targetDirectory)
{
    m_error.clear();
    m_framePaths.clear();
    m_manifestPath.clear();
    m_outputDirectory.clear();
    const QString target = QDir::cleanPath(targetDirectory.trimmed());
    if (!track || frames.isEmpty() || frames.size() != times.size() || frames.size() > MaxSequenceFrames
        || target.isEmpty() || target == QLatin1String(".")) {
        m_error = QStringLiteral("Provide a caption track, a matching sequence of 1–10000 frames and times, and a target directory.");
        emit changed();
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), m_error}};
    }
    const QFileInfo targetInfo(target);
    QDir parent(targetInfo.absolutePath());
    if (QFileInfo::exists(target) || !parent.mkpath(QStringLiteral("."))) {
        m_error = QStringLiteral("Target directory already exists or its parent cannot be created.");
        emit changed();
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), m_error}};
    }

    const QString stageName = QStringLiteral(".%1.pending-%2")
        .arg(targetInfo.fileName(), QUuid::createUuid().toString(QUuid::WithoutBraces));
    if (!parent.mkdir(stageName)) {
        m_error = QStringLiteral("Could not create a staging directory beside the target.");
        emit changed();
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), m_error}};
    }
    const QString stagePath = parent.filePath(stageName);
    CaptionFrameRenderer renderer;
    QJsonArray manifestFrames;
    QStringList stagedFrames;
    for (qsizetype index = 0; index < frames.size(); ++index) {
        const QImage &source = frames.at(index);
        if (source.isNull()) {
            m_error = QStringLiteral("Source frame %1 is empty.").arg(index);
            break;
        }
        const QVariantMap result = renderer.renderFrame(source, track, times.at(index));
        if (!result.value(QStringLiteral("ok")).toBool()) {
            m_error = result.value(QStringLiteral("error")).toString();
            break;
        }
        const QImage burned = decodePngDataUrl(result.value(QStringLiteral("imageDataUrl")).toString());
        const QString fileName = QStringLiteral("frame-%1.png").arg(index, 6, 10, QLatin1Char('0'));
        QSaveFile file(QDir(stagePath).filePath(fileName));
        file.setDirectWriteFallback(false);
        if (burned.isNull() || !file.open(QIODevice::WriteOnly) || !burned.save(&file, "PNG") || !file.commit()) {
            m_error = QStringLiteral("Could not atomically save %1: %2").arg(fileName, file.errorString());
            break;
        }
        stagedFrames.append(fileName);
        QJsonArray cueIds;
        for (const QString &cueId : renderer.activeCueIds()) cueIds.append(cueId);
        manifestFrames.append(QJsonObject{{QStringLiteral("index"), int(index)},
                                         {QStringLiteral("file"), fileName},
                                         {QStringLiteral("timeMs"), times.at(index)},
                                         {QStringLiteral("activeCueIds"), cueIds}});
    }
    if (m_error.isEmpty()) {
        QJsonObject manifest{{QStringLiteral("version"), 1},
                             {QStringLiteral("format"), QStringLiteral("png-sequence")},
                             {QStringLiteral("frameCount"), frames.size()},
                             {QStringLiteral("frames"), manifestFrames}};
        QSaveFile file(QDir(stagePath).filePath(QStringLiteral("timing.json")));
        file.setDirectWriteFallback(false);
        if (!file.open(QIODevice::WriteOnly)
            || file.write(QJsonDocument(manifest).toJson(QJsonDocument::Indented)) < 0
            || !file.commit())
            m_error = QStringLiteral("Could not atomically save sequence timing manifest: %1").arg(file.errorString());
    }
    if (m_error.isEmpty() && !parent.rename(stageName, targetInfo.fileName()))
        m_error = QStringLiteral("Could not publish the completed sequence directory.");
    if (!m_error.isEmpty()) {
        QDir(stagePath).removeRecursively();
        emit changed();
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), m_error}};
    }
    m_outputDirectory = targetInfo.absoluteFilePath();
    m_manifestPath = QDir(m_outputDirectory).filePath(QStringLiteral("timing.json"));
    for (const QString &file : stagedFrames) m_framePaths.append(QDir(m_outputDirectory).filePath(file));
    emit changed();
    return {{QStringLiteral("ok"), true}, {QStringLiteral("outputDirectory"), m_outputDirectory},
            {QStringLiteral("manifestPath"), m_manifestPath}, {QStringLiteral("framePaths"), m_framePaths},
            {QStringLiteral("frameCount"), frameCount()}};
}
