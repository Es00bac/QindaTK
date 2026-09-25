// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "caption_track_model.h"

#include <QGlyphRun>
#include <QImage>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

// AGENT-CONTRACT (docs/caption-rendering.md "Burn-in"): the one caption painter.
// CaptionFrameRenderer, CaptionSequenceExporter and MediaFinishing (program
// playback and export) all draw through burn(), so a cue looks the same in a
// PNG, the program monitor and an exported file. Cues active at `timeMs`
// ([in, out)) stack from the upper band down in lane, in-time, ID order;
// styleId "emphasis" draws the emphasis style and every other ID the default.
// Returns the drawn cue IDs in that order.
// Text is laid out with QTextLayout (font fallback, shaping, bidi) and the
// layout is drawn as is; glyphRuns() returns the runs of that same layout so
// tests and warnings inspect exactly what burn() paints.
namespace CaptionBurnIn {
QStringList burn(QImage *image, const QVector<CaptionCueRecord> &cues, qint64 timeMs);
QList<QGlyphRun> glyphRuns(const QString &text, int pixelSize, bool emphasis, qreal width);
// Characters of `text` no installed font draws: they would burn in as
// empty .notdef boxes (CaptionTrackModel warns with kind "glyphs").
QString missingCharacters(const QString &text, bool emphasis = false);
}

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
