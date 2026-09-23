// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "timeline_model.h"
#include "timeline_visible_list.h"

#include <QObject>
#include <QPointer>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// AGENT-CONTRACT: one view of a TimelineModel. It owns only view state:
// the exact tick at x = 0 (originTicks), a display zoom, vertical scroll and
// density-driven default track height. It resolves the model's committed
// state plus the active preview into three bounded delegate lists and maps
// pixels to exact ticks with explicit rounding. It never edits the model.
// Canonical positions never pass through pixels: x = (ticks - origin) * zoom
// is computed from an exact integer difference, and a pixel maps back to a
// tick once, at the gesture boundary.
class TimelineViewport : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QindaTK::TimelineModel *model READ model WRITE setModel NOTIFY modelChanged)
    Q_PROPERTY(QString originTicks READ originTicksText WRITE setOriginTicksText NOTIFY viewChanged)
    Q_PROPERTY(double pixelsPerSecond READ pixelsPerSecond WRITE setPixelsPerSecond NOTIFY viewChanged)
    Q_PROPERTY(double minimumPixelsPerSecond READ minimumPixelsPerSecond WRITE setMinimumPixelsPerSecond NOTIFY viewChanged)
    Q_PROPERTY(double maximumPixelsPerSecond READ maximumPixelsPerSecond WRITE setMaximumPixelsPerSecond NOTIFY viewChanged)
    Q_PROPERTY(double viewportWidth READ viewportWidth WRITE setViewportWidth NOTIFY viewChanged)
    Q_PROPERTY(double viewportHeight READ viewportHeight WRITE setViewportHeight NOTIFY viewChanged)
    Q_PROPERTY(double scrollY READ scrollY WRITE setScrollY NOTIFY viewChanged)
    Q_PROPERTY(double defaultTrackHeight READ defaultTrackHeight WRITE setDefaultTrackHeight NOTIFY viewChanged)
    Q_PROPERTY(double minimumTrackHeight READ minimumTrackHeight WRITE setMinimumTrackHeight NOTIFY viewChanged)
    Q_PROPERTY(double overscanPixels READ overscanPixels WRITE setOverscanPixels NOTIFY viewChanged)
    Q_PROPERTY(double summaryPixels READ summaryPixels WRITE setSummaryPixels NOTIFY viewChanged)
    Q_PROPERTY(double handlePixels READ handlePixels WRITE setHandlePixels NOTIFY viewChanged)
    Q_PROPERTY(int maximumClipRows READ maximumClipRows WRITE setMaximumClipRows NOTIFY viewChanged)
    Q_PROPERTY(bool showGhosts READ showGhosts WRITE setShowGhosts NOTIFY viewChanged)
    Q_PROPERTY(double contentHeight READ contentHeight NOTIFY layoutChanged)
    Q_PROPERTY(double maximumScrollY READ maximumScrollY NOTIFY layoutChanged)
    Q_PROPERTY(QString visibleStartTicks READ visibleStartTicks NOTIFY layoutChanged)
    Q_PROPERTY(QString visibleEndTicks READ visibleEndTicks NOTIFY layoutChanged)
    Q_PROPERTY(int firstVisibleTrack READ firstVisibleTrack NOTIFY layoutChanged)
    Q_PROPERTY(int lastVisibleTrack READ lastVisibleTrack NOTIFY layoutChanged)
    Q_PROPERTY(int trackCount READ trackCount NOTIFY layoutChanged)
    Q_PROPERTY(int summarizedClips READ summarizedClips NOTIFY layoutChanged)
    Q_PROPERTY(bool truncated READ truncated NOTIFY layoutChanged)
    Q_PROPERTY(QindaTK::TimelineVisibleList *clips READ clips CONSTANT)
    Q_PROPERTY(QindaTK::TimelineVisibleList *tracks READ tracks CONSTANT)
    Q_PROPERTY(QindaTK::TimelineVisibleList *markers READ markers CONSTANT)
public:
    explicit TimelineViewport(QObject *parent = nullptr);

    TimelineModel *model() const { return m_model; }
    void setModel(TimelineModel *model);
    qint64 origin() const { return m_origin; }
    void setOrigin(qint64 ticks);
    QString originTicksText() const { return QString::number(m_origin); }
    void setOriginTicksText(const QString &ticks);
    double pixelsPerSecond() const { return m_pixelsPerSecond; }
    void setPixelsPerSecond(double value);
    double minimumPixelsPerSecond() const { return m_minimumZoom; }
    void setMinimumPixelsPerSecond(double value);
    double maximumPixelsPerSecond() const { return m_maximumZoom; }
    void setMaximumPixelsPerSecond(double value);
    double viewportWidth() const { return m_width; }
    void setViewportWidth(double value);
    double viewportHeight() const { return m_height; }
    void setViewportHeight(double value);
    double scrollY() const { return m_scrollY; }
    void setScrollY(double value);
    double defaultTrackHeight() const { return m_defaultTrackHeight; }
    void setDefaultTrackHeight(double value);
    double minimumTrackHeight() const { return m_minimumTrackHeight; }
    void setMinimumTrackHeight(double value);
    double overscanPixels() const { return m_overscan; }
    void setOverscanPixels(double value);
    double summaryPixels() const { return m_summaryPixels; }
    void setSummaryPixels(double value);
    double handlePixels() const { return m_handlePixels; }
    void setHandlePixels(double value);
    int maximumClipRows() const { return m_maximumClipRows; }
    void setMaximumClipRows(int value);
    bool showGhosts() const { return m_showGhosts; }
    void setShowGhosts(bool show);
    double contentHeight() const { return m_contentHeight; }
    double maximumScrollY() const { return std::max(0.0, m_contentHeight - m_height); }
    QString visibleStartTicks() const { return QString::number(m_visibleStart); }
    QString visibleEndTicks() const { return QString::number(m_visibleEnd); }
    int firstVisibleTrack() const { return m_firstTrack; }
    int lastVisibleTrack() const { return m_lastTrack; }
    int trackCount() const { return int(m_order.size()); }
    int summarizedClips() const { return m_summarized; }
    bool truncated() const { return m_truncated; }
    TimelineVisibleList *clips() const { return m_clips; }
    TimelineVisibleList *tracks() const { return m_tracks; }
    TimelineVisibleList *markers() const { return m_markers; }

    double pixelsPerTick() const;
    double xFor(qint64 ticks) const;
    std::optional<qint64> ticksAt(double x, TimeMap::Rounding rounding) const;
    std::optional<qint64> ticksForLength(double pixels) const;

    Q_INVOKABLE double xForTicks(const QString &ticks) const;
    Q_INVOKABLE QString ticksForX(double x, int rounding = TimeMap::NearestAway) const;
    Q_INVOKABLE QString ticksForPixels(double pixels) const;
    Q_INVOKABLE double pixelsForTicks(const QString &ticks) const;
    Q_INVOKABLE int trackIndexAtY(double y) const;
    Q_INVOKABLE QString trackIdAtY(double y) const;
    Q_INVOKABLE QString trackIdAt(int visibleIndex) const;
    Q_INVOKABLE int trackIndexOf(const QString &trackId) const;
    Q_INVOKABLE double trackY(const QString &trackId) const;
    Q_INVOKABLE double trackHeight(const QString &trackId) const;
    Q_INVOKABLE QVariantMap hitTest(double x, double y) const;
    Q_INVOKABLE QVariantMap clipGeometry(const QString &clipId) const;
    Q_INVOKABLE void scrollByPixels(double dx, double dy);
    Q_INVOKABLE void zoomAround(double x, double factor);
    Q_INVOKABLE bool ensureTicksVisible(const QString &ticks, double margin = 0);
    Q_INVOKABLE bool ensureClipVisible(const QString &clipId, double margin = 0);
    Q_INVOKABLE bool ensureTrackVisible(const QString &trackId);
    Q_INVOKABLE void fitToContent();
    Q_INVOKABLE void refresh();

signals:
    void modelChanged();
    void viewChanged();
    void layoutChanged();
    void refreshed();

private:
    struct TrackSlot {
        QUuid id;
        double top = 0;
        double height = 0;
    };
    void rebuildLayout();
    void rebuildClips(qint64 start, qint64 end);
    void rebuildMarkers(qint64 start, qint64 end);
    double heightFor(const TimelineTrack &track) const;
    void changed();
    // Effective clips (committed plus preview) on a track meeting [start, end),
    // in (start, id) order; ghosts are the committed places of previewed clips.
    QVector<TimelineClip> clipsOnTrack(const QUuid &trackId, qint64 start, qint64 end) const;
    QVector<TimelineClip> ghostsOnTrack(const QUuid &trackId, qint64 start, qint64 end) const;

    QPointer<TimelineModel> m_model;
    TimelineVisibleList *m_clips = nullptr;
    TimelineVisibleList *m_tracks = nullptr;
    TimelineVisibleList *m_markers = nullptr;
    qint64 m_origin = 0;
    double m_pixelsPerSecond = 100.0;
    double m_minimumZoom = 1e-4;
    double m_maximumZoom = 1e7;
    double m_width = 0;
    double m_height = 0;
    double m_scrollY = 0;
    double m_defaultTrackHeight = 56;
    double m_minimumTrackHeight = 16;
    double m_overscan = 64;
    double m_summaryPixels = 3;
    double m_handlePixels = 6;
    int m_maximumClipRows = 1024;
    bool m_showGhosts = true;
    bool m_batch = false;
    QVector<TrackSlot> m_order;
    QHash<QUuid, int> m_slotIndex;
    double m_contentHeight = 0;
    qint64 m_visibleStart = 0;
    qint64 m_visibleEnd = 0;
    int m_firstTrack = -1;
    int m_lastTrack = -1;
    int m_summarized = 0;
    bool m_truncated = false;
};

} // namespace QindaTK
