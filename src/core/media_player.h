// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "media_catalog.h"
#include "media_playback_engine.h"
#include "resource_resolver.h"
#include "time_map.h"
#include "timeline_model.h"
#include "transport_model.h"
#include "video_finishing.h"

#include <QImage>
#include <QPointer>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// AGENT-CONTRACT (docs/media-playback.md "MediaPlayer"): the QML face of one
// MediaPlaybackEngine. Source mode (resourceId, no timeline) shows one media
// file frame by frame; program mode (timeline set) composites the committed
// TimelineModel revision. Every playhead change is a TransportModel command
// acknowledged by the engine; presented* describes the frame actually on
// screen, which may trail targetFrame while `pending` is true. Resource IDs
// resolve through `resolver` (without one, a resource ID is a local path or
// file URI); a catalog's active proxy replaces pictures, never frame identity.
class MediaPlayer : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QindaTK::ResourceResolver *resolver READ resolver WRITE setResolver NOTIFY sourceChanged)
    Q_PROPERTY(QindaTK::MediaCatalog *catalog READ catalog WRITE setCatalog NOTIFY sourceChanged)
    Q_PROPERTY(QString resourceId READ resourceId WRITE setResourceId NOTIFY sourceChanged)
    Q_PROPERTY(QindaTK::TimelineModel *timeline READ timeline WRITE setTimeline NOTIFY sourceChanged)
    Q_PROPERTY(int frameWidth READ frameWidth WRITE setFrameWidth NOTIFY sourceChanged)
    Q_PROPERTY(int frameHeight READ frameHeight WRITE setFrameHeight NOTIFY sourceChanged)
    Q_PROPERTY(int audioChannels READ audioChannels WRITE setAudioChannels NOTIFY sourceChanged)
    Q_PROPERTY(bool useProxies READ useProxies WRITE setUseProxies NOTIFY sourceChanged)
    Q_PROPERTY(int maxPreviewWidth READ maxPreviewWidth WRITE setMaxPreviewWidth NOTIFY sourceChanged)
    Q_PROPERTY(int maxPreviewHeight READ maxPreviewHeight WRITE setMaxPreviewHeight NOTIFY sourceChanged)
    // Grade and captions applied to every presented and exported frame
    // (D-126); the monitor shows open-gesture previews, exports the committed grade.
    Q_PROPERTY(QindaTK::VideoFinishing *finishing READ finishing WRITE setFinishing NOTIFY sourceChanged)
    Q_PROPERTY(QindaTK::TransportModel *transport READ transport CONSTANT)
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap mediaInfo READ mediaInfo NOTIFY programChanged)
    Q_PROPERTY(QVariantMap programInfo READ programInfo NOTIFY programChanged)
    Q_PROPERTY(QString frameCount READ frameCount NOTIFY programChanged)
    Q_PROPERTY(QString frameRateNumerator READ frameRateNumerator NOTIFY programChanged)
    Q_PROPERTY(QString frameRateDenominator READ frameRateDenominator NOTIFY programChanged)
    Q_PROPERTY(QString timebaseNumerator READ timebaseNumerator NOTIFY programChanged)
    Q_PROPERTY(QString timebaseDenominator READ timebaseDenominator NOTIFY programChanged)
    Q_PROPERTY(double pixelAspect READ pixelAspect NOTIFY programChanged)
    Q_PROPERTY(bool proxyActive READ proxyActive NOTIFY programChanged)
    Q_PROPERTY(QString presentedFrame READ presentedFrame NOTIFY frameChanged)
    Q_PROPERTY(QString presentedTicks READ presentedTicks NOTIFY frameChanged)
    Q_PROPERTY(QString presentedFrameKey READ presentedFrameKey NOTIFY frameChanged)
    Q_PROPERTY(QString presentedTimecode READ presentedTimecode NOTIFY frameChanged)
    Q_PROPERTY(QVariantList presentedSources READ presentedSources NOTIFY frameChanged)
    Q_PROPERTY(QStringList missingResourceIds READ missingResourceIds NOTIFY frameChanged)
    Q_PROPERTY(QString frameError READ frameError NOTIFY frameChanged)
    Q_PROPERTY(bool presentedCorrupt READ presentedCorrupt NOTIFY frameChanged)
    Q_PROPERTY(QString targetFrame READ targetFrame NOTIFY frameChanged)
    Q_PROPERTY(bool pending READ pending NOTIFY frameChanged)
    Q_PROPERTY(QVariantMap statistics READ statistics NOTIFY statisticsChanged)
public:
    explicit MediaPlayer(QObject *parent = nullptr);
    ~MediaPlayer() override;

    ResourceResolver *resolver() const { return m_resolver; }
    void setResolver(ResourceResolver *resolver);
    MediaCatalog *catalog() const { return m_catalog; }
    void setCatalog(MediaCatalog *catalog);
    QString resourceId() const { return m_resourceId; }
    void setResourceId(const QString &resourceId);
    TimelineModel *timeline() const { return m_timeline; }
    void setTimeline(TimelineModel *timeline);
    int frameWidth() const { return m_frameSize.width(); }
    void setFrameWidth(int width);
    int frameHeight() const { return m_frameSize.height(); }
    void setFrameHeight(int height);
    int audioChannels() const { return m_audioChannels; }
    void setAudioChannels(int channels);
    bool useProxies() const { return m_useProxies; }
    void setUseProxies(bool enabled);
    int maxPreviewWidth() const { return m_previewSize.width(); }
    void setMaxPreviewWidth(int width);
    int maxPreviewHeight() const { return m_previewSize.height(); }
    void setMaxPreviewHeight(int height);
    VideoFinishing *finishing() const { return m_finishing; }
    void setFinishing(VideoFinishing *finishing);
    TransportModel *transport() const { return m_transport; }
    QString state() const { return m_state; }
    QString lastError() const { return m_lastError; }
    QVariantMap mediaInfo() const;
    QVariantMap programInfo() const;
    QString frameCount() const;
    QString frameRateNumerator() const;
    QString frameRateDenominator() const;
    QString timebaseNumerator() const;
    QString timebaseDenominator() const;
    double pixelAspect() const;
    bool proxyActive() const { return m_proxyCount > 0; }
    QString presentedFrame() const;
    QString presentedTicks() const;
    QString presentedFrameKey() const;
    QString presentedTimecode() const;
    QVariantList presentedSources() const;
    QStringList missingResourceIds() const;
    QString frameError() const;
    bool presentedCorrupt() const;
    QString targetFrame() const;
    bool pending() const;
    QVariantMap statistics() const;

    MediaPlaybackEngine *engine() const { return m_engine; }
    MediaProgramPtr program() const { return m_program; }
    QImage presentedImage() const;
    quint64 presentedRevision() const;

    // Each returns the TransportModel request ID, or "" when rejected
    // (transport.lastError explains). Frames and ticks are exact decimals.
    Q_INVOKABLE QString seekFrame(const QString &frame);
    Q_INVOKABLE QString stepFrame(int delta);
    // Seeks to the start of the frame showing at `ticks` (program timebase).
    Q_INVOKABLE QString seekTicks(const QString &ticks);
    Q_INVOKABLE QString play();
    Q_INVOKABLE QString stop();
    Q_INVOKABLE QString togglePlayback();
    Q_INVOKABLE QString frameStartTicks(const QString &frame) const;
    Q_INVOKABLE QString frameAtTicks(const QString &ticks) const;
    // A RenderQueueModel snapshot exporting the whole timeline program from
    // original media (never proxies); {} with lastError when not possible.
    Q_INVOKABLE QVariantMap exportJob(const QString &outputPath, const QString &label = {});
    Q_INVOKABLE void reload();

signals:
    void sourceChanged();
    void stateChanged();
    void programChanged();
    void frameChanged();
    void statisticsChanged();

private:
    struct Resolved {
        QString uri;
        QString revision;
        bool online = false;
    };
    Resolved resolve(const QString &resourceId) const;
    QString proxyFor(const QString &resourceId) const;
    void rebuild();
    void buildTimelineProgram();
    void finishProbe(const Resolved &original, const QString &playbackUri, bool proxy,
                     const MediaSourceInfoPtr &info, const OperationError &error);
    void applyProgram(MediaProgramPtr program, const QString &state, const QString &error);
    void refinish();
    void setStateText(const QString &state, const QString &error);
    void watchTimeMap();
    qint64 presentedIndex() const;
    QString requestFrame(qint64 frame);

    QPointer<ResourceResolver> m_resolver;
    QPointer<MediaCatalog> m_catalog;
    QPointer<TimelineModel> m_timeline;
    QPointer<TimeMap> m_watchedTimeMap;
    QString m_resourceId;
    QSize m_frameSize{1280, 720};
    QSize m_previewSize;
    int m_audioChannels = 2;
    bool m_useProxies = true;
    int m_proxyCount = 0;
    QString m_state = QStringLiteral("empty");
    QString m_lastError;
    MediaProgramPtr m_program;
    MediaProgramPtr m_unfinished; // m_program before finishing
    QPointer<VideoFinishing> m_finishing;
    QMetaObject::Connection m_finishingLink;
    quint64 m_generation = 0;
    QList<QMetaObject::Connection> m_sourceLinks;
    QMetaObject::Connection m_timeMapLink;
    TimeMap *m_timeMap = nullptr;
    MediaPlaybackEngine *m_engine = nullptr;
    TransportModel *m_transport = nullptr;
};

} // namespace QindaTK
