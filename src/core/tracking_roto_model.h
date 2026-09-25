// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "application_contracts.h"

#include <QFuture>
#include <QHash>
#include <QImage>
#include <QMap>
#include <QObject>
#include <QPointF>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// AGENT-CONTRACT: decoded, tagged source frames remain host-owned inputs;
// only tracks, accepted points, masks and keyframes are serialized. Generated
// tracking points stay in previewPoints until acceptAnalysis is called.
// AGENT-CONTRACT (docs/tracking-roto.md "Tracking footage"): frame IDs are
// source frame indices, the same indices loadMediaFrames decodes and
// MediaFrameFeed reports per fed layer; MotionFrameRenderer samples masks
// at the frame the layer's pixels came from.
class TrackingRotoModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantList tracks READ tracks NOTIFY changed)
    Q_PROPERTY(QVariantList masks READ masks NOTIFY changed)
    Q_PROPERTY(bool analysisRunning READ analysisRunning NOTIFY analysisChanged)
    Q_PROPERTY(double analysisProgress READ analysisProgress NOTIFY analysisChanged)
    Q_PROPERTY(bool loadingFrames READ loadingFrames NOTIFY loadChanged)
    Q_PROPERTY(double frameLoadProgress READ frameLoadProgress NOTIFY loadChanged)
    Q_PROPERTY(QVariantList loadedFrames READ loadedFrames NOTIFY framesChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY changed)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY changed)
public:
    explicit TrackingRotoModel(QObject *parent = nullptr);
    ~TrackingRotoModel() override;
    QVariantList tracks() const;
    QVariantList masks() const;
    bool analysisRunning() const { return m_analysis.running; }
    double analysisProgress() const;
    bool loadingFrames() const { return m_load.running; }
    double frameLoadProgress() const;
    QVariantList loadedFrames() const;
    QString lastError() const { return m_error; }
    bool canUndo() const { return !m_undo.isEmpty(); }
    bool canRedo() const { return !m_redo.isEmpty(); }

    bool setFrame(int frame, const QImage &image);
    Q_INVOKABLE bool loadFrame(int frame, const QUrl &url);
    // Decodes source frames [firstFrame, firstFrame + count) of a local video
    // (path or file URI) on a worker; frameLoadFinished reports the outcome.
    Q_INVOKABLE bool loadMediaFrames(const QString &uri, int firstFrame, int count);
    Q_INVOKABLE void cancelFrameLoad();
    Q_INVOKABLE QString addTrack(const QString &label, int referenceFrame,
                                 double x, double y, int radius = 5, int searchRadius = 16);
    Q_INVOKABLE bool removeTrack(const QString &trackId);
    // endFrame < startFrame tracks backwards. occlusionFrames is how many
    // consecutive lost frames the worker searches through before stopping.
    Q_INVOKABLE bool startAnalysis(const QString &trackId, int startFrame, int endFrame,
                                   int occlusionFrames = 0);
    Q_INVOKABLE void cancelAnalysis();
    Q_INVOKABLE bool acceptAnalysis(const QString &trackId);
    Q_INVOKABLE bool correctPoint(const QString &trackId, int frame, double x, double y);
    Q_INVOKABLE bool offsetTrack(const QString &trackId, double dx, double dy,
                                 int fromFrame, int toFrame);
    Q_INVOKABLE bool rebaseTrack(const QString &trackId, int frame);

    Q_INVOKABLE QString addMask(const QString &label);
    Q_INVOKABLE bool removeMask(const QString &maskId);
    Q_INVOKABLE bool setMaskKeyframe(const QString &maskId, int frame,
                                     const QVariantList &points);
    Q_INVOKABLE bool removeMaskKeyframe(const QString &maskId, int frame);
    Q_INVOKABLE bool setMaskFeather(const QString &maskId, double pixels);
    Q_INVOKABLE QVariantMap evaluateMask(const QString &maskId, int frame) const;
    Q_INVOKABLE QVariantMap previewTrackedMask(const QString &maskId, int sourceFrame,
                                                int targetFrame, const QString &trackId) const;
    Q_INVOKABLE QVariantMap applyTrackToMask(const QString &maskId, int sourceFrame,
                                             const QString &trackId, int fromFrame, int toFrame);
    QImage applyMask(const QString &maskId, int frame, const QImage &source);
    Q_INVOKABLE bool exportMaskedPng(const QString &maskId, int frame, const QUrl &url);
    Q_INVOKABLE bool undo();
    Q_INVOKABLE bool redo();
    Q_INVOKABLE bool saveToFile(const QUrl &url);
    Q_INVOKABLE bool loadFromFile(const QUrl &url);

signals:
    void changed();
    void analysisChanged();
    void loadChanged();
    void framesChanged();
    void frameLoadFinished(bool ok, const QString &error);
    void errorChanged();

private:
    struct Point {
        int frame = 0;
        double x = 0;
        double y = 0;
        double confidence = 0;
        QString state;
        QString source;
    };
    struct Track {
        QString id;
        QString label;
        int referenceFrame = 0;
        double seedX = 0;
        double seedY = 0;
        int radius = 5;
        int searchRadius = 16;
        QString state = QStringLiteral("idle");
        QMap<int, Point> points;
        QMap<int, Point> preview;
    };
    struct PathPoint { QString id; double x = 0; double y = 0; };
    struct Mask {
        QString id;
        QString label;
        double feather = 0;
        QMap<int, QVector<PathPoint>> keys;
    };
    struct State { QVector<Track> tracks; QVector<Mask> masks; };
    struct Analysis {
        bool running = false;
        QString trackId;
        int total = 0;
        int done = 0;
        int lastFrame = 0;
        double lastConfidence = 0;
        quint64 job = 0;
        CancellationSource cancel;
    };
    struct Load {
        bool running = false;
        int total = 0;
        int done = 0;
        quint64 job = 0;
        CancellationSource cancel;
    };
    int trackIndex(const QString &id) const;
    int maskIndex(const QString &id) const;
    void remember();
    void setError(const QString &error);
    void onAnalysisSample(quint64 job, int frame, double x, double y, double confidence, bool lost);
    void onAnalysisFinished(quint64 job, bool lost);
    void onFrameLoaded(quint64 job, int frame, const QImage &proxy);
    void onLoadFinished(quint64 job, const QString &error);
    bool storeProxy(int frame, const QImage &proxy);
    bool translatedPath(const QString &maskId, int sourceFrame, const Track &track,
                        int targetFrame, QVector<PathPoint> *path, QString *error) const;
    static QVariantMap pointMap(const Point &point);
    static QVariantList pathList(const QVector<PathPoint> &points);
    static bool parsePath(const QVariantList &value, QVector<PathPoint> *points);
    QVariantMap contentMap() const;
    bool loadContent(const QVariantMap &content);

    QVector<Track> m_tracks;
    QVector<Mask> m_masks;
    QVector<State> m_undo;
    QVector<State> m_redo;
    QHash<int, QImage> m_frames;
    qsizetype m_frameBytes = 0;
    quint64 m_jobSerial = 0;
    Analysis m_analysis;
    Load m_load;
    QString m_error;
    // AGENT-GUARD: the destructor cancels and waits for both futures before
    // members go; worker lambdas only post to this object's thread.
    QFuture<void> m_analysisFuture;
    QFuture<void> m_loadFuture;
};

} // namespace QindaTK
