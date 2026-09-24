// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "timeline_policy.h"
#include "timeline_types.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QtQml/qqmlregistration.h>
#include <limits>
#include <vector>

namespace QindaTK {

// AGENT-CONTRACT: TimelineModel is the reference authority for timeline
// placement (WP-048). Views, delegates and QML never own clip, marker or
// track positions: they read committed state plus the one active preview and
// submit typed, revision-guarded edits (begin -> update -> commit/cancel).
// One accepted gesture is one change set and one history step; a rejected,
// stale or cancelled edit leaves every committed record unchanged. A
// production host either drives this model as its projection or takes the
// history through externalUndo (see docs/edit-timeline.md, D-045).
class TimelineModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString schemaId READ schemaId CONSTANT)
    Q_PROPERTY(QString documentId READ documentId NOTIFY contentReset)
    Q_PROPERTY(QString revision READ revisionText NOTIFY revisionChanged)
    Q_PROPERTY(QString resetGeneration READ resetGenerationText NOTIFY contentReset)
    Q_PROPERTY(QString timebaseNumerator READ timebaseNumeratorText NOTIFY contentReset)
    Q_PROPERTY(QString timebaseDenominator READ timebaseDenominatorText NOTIFY contentReset)
    Q_PROPERTY(QindaTK::TimeMap *timeMap READ timeMap WRITE setTimeMap NOTIFY timeMapChanged)
    Q_PROPERTY(QindaTK::TimelineEditPolicy *policy READ policy WRITE setPolicy NOTIFY policyChanged)
    Q_PROPERTY(int trackCount READ trackCount NOTIFY structureChanged)
    Q_PROPERTY(int clipCount READ clipCount NOTIFY clipsChanged)
    Q_PROPERTY(int markerCount READ markerCount NOTIFY markersChanged)
    Q_PROPERTY(QString contentEndTicks READ contentEndText NOTIFY revisionChanged)
    Q_PROPERTY(bool hasEditRange READ hasEditRange NOTIFY editRangeChanged)
    Q_PROPERTY(QString editRangeStart READ editRangeStartText NOTIFY editRangeChanged)
    Q_PROPERTY(QString editRangeEnd READ editRangeEndText NOTIFY editRangeChanged)
    Q_PROPERTY(bool editing READ editing NOTIFY editStateChanged)
    Q_PROPERTY(QString editOperationId READ editOperationId NOTIFY editStateChanged)
    Q_PROPERTY(QVariantMap editPreview READ editPreview NOTIFY previewChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
    Q_PROPERTY(QString undoText READ undoText NOTIFY historyChanged)
    Q_PROPERTY(QString redoText READ redoText NOTIFY historyChanged)
    Q_PROPERTY(int historyCount READ historyCount NOTIFY historyChanged)
    Q_PROPERTY(int historyLimit READ historyLimit WRITE setHistoryLimit NOTIFY historyChanged)
    Q_PROPERTY(bool externalUndo READ externalUndo WRITE setExternalUndo NOTIFY historyChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
public:
    struct IndexEntry { qint64 start; qint64 end; QUuid id; };
    struct EdgeEntry { qint64 tick; QUuid id; bool end; };
    static constexpr int kMaxTracks = 10000;
    static constexpr int kMaxClips = 2000000;
    static constexpr int kMaxMarkers = 200000;
    static constexpr int kMaxQueryIds = 4096;

    explicit TimelineModel(QObject *parent = nullptr);
    ~TimelineModel() override;

    // ---- committed state (C++) ----
    static QString schemaId() { return QStringLiteral("qindatk.timeline/1"); }
    QString documentId() const { return m_documentId.toString(); }
    quint64 revisionValue() const { return m_revision; }
    QString revisionText() const { return QString::number(m_revision); }
    quint64 resetGenerationValue() const { return m_generation; }
    QString resetGenerationText() const { return QString::number(m_generation); }
    SnapshotHeader snapshotHeader() const;
    TimelineRate timebase() const { return m_timebase; }
    QString timebaseNumeratorText() const { return QString::number(m_timebase.numerator); }
    QString timebaseDenominatorText() const { return QString::number(m_timebase.denominator); }
    TimeMap *timeMap() const;
    void setTimeMap(TimeMap *map);
    TimelineEditPolicy *policy() const;
    void setPolicy(TimelineEditPolicy *policy);
    int trackCount() const { return int(m_trackOrder.size()); }
    int clipCount() const { return int(m_clips.size()); }
    int markerCount() const { return int(m_markers.size()); }
    qint64 contentEnd() const { return m_contentEnd; }
    QString contentEndText() const { return QString::number(m_contentEnd); }
    const QVector<QUuid> &trackOrder() const { return m_trackOrder; }
    QVector<QUuid> visibleTrackOrder() const;
    const TimelineTrack *trackRecord(const QUuid &id) const;
    const TimelineClip *clipRecord(const QUuid &id) const;
    const TimelineMarker *markerRecord(const QUuid &id) const;
    const QHash<QUuid, TimelineMarker> &markerRecords() const { return m_markers; }
    // Sorted by (start, id). Clips on a track intersecting [start, end).
    const std::vector<IndexEntry> &trackIndex(const QUuid &trackId) const;
    qint64 trackMaxDuration(const QUuid &trackId) const;
    QVector<QUuid> clipsInRange(const QUuid &trackId, qint64 start, qint64 end,
                                int limit = std::numeric_limits<int>::max()) const;
    // Committed clips whose linkId is `linkId`, sorted by ID; empty for a null ID.
    QVector<QUuid> linkedClips(const QUuid &linkId) const;
    const std::vector<EdgeEntry> &clipEdges() const;    // lazy, committed
    const std::vector<EdgeEntry> &markerEdges() const;  // lazy, committed
    // Committed markers whose [position, position + duration] meets
    // [start, end), in (position, id) order. Points at `start` are included.
    QVector<QUuid> markersInRange(qint64 start, qint64 end,
                                  int limit = std::numeric_limits<int>::max()) const;
    bool hasEditRange() const { return m_hasRange; }
    qint64 editRangeStartValue() const { return m_rangeStart; }
    qint64 editRangeEndValue() const { return m_rangeEnd; }
    QString editRangeStartText() const { return m_hasRange ? QString::number(m_rangeStart) : QString(); }
    QString editRangeEndText() const { return m_hasRange ? QString::number(m_rangeEnd) : QString(); }
    bool setEditRangeTicks(qint64 start, qint64 end);

    // ---- preview overlay (C++) ----
    bool hasPreview() const;
    const TimelinePlan *previewPlan() const;
    const TimelineChange *previewClipChange(const QUuid &id) const;
    const TimelineChange *previewMarkerChange(const QUuid &id) const;
    std::optional<TimelineClip> effectiveClip(const QUuid &id) const;
    std::optional<TimelineTrack> effectiveTrack(const QUuid &id) const;
    std::optional<TimelineMarker> effectiveMarker(const QUuid &id) const;
    QVector<QUuid> effectiveVisibleTrackOrder() const;

    // ---- exact conversions (C++) ----
    std::optional<qint64> ticksToSamples(qint64 ticks, TimeMap::Rounding rounding) const;
    std::optional<qint64> samplesToTicks(qint64 samples, TimeMap::Rounding rounding) const;
    std::optional<qint64> ticksToFrames(qint64 ticks, TimeMap::Rounding rounding) const;
    std::optional<qint64> frameStartTicks(qint64 frame) const;
    std::optional<qint64> musicalTickFor(qint64 ticks) const;
    std::optional<qint64> ticksForMusicalTick(qint64 musicalTick) const;
    // Beat-locked clips (timeline_model_musical.cpp): anchoredClip() fixes
    // musicalTick/musicalOffset from `start` under the current map (exact
    // round trip); withDerivedStart() derives `start` from them.
    TimelineClip anchoredClip(TimelineClip clip) const;
    TimelineClip withDerivedStart(TimelineClip clip) const;
    TimelineMarker withDerivedPosition(TimelineMarker marker) const;
    // Committed placement after a time-map change: a beat-locked clip that now
    // overlaps (RejectOverlap) or starts before zero is reported, not repaired.
    OperationError musicalPlacementError() const;
    // Rational grid step in ticks and the tick where line zero lies.
    bool gridStep(TimelineSnapGrid grid, quint64 &numerator, quint64 &denominator,
                  qint64 &origin) const;

    // ---- content (C++) ----
    bool setContent(TimelineRate timebase, const QVector<TimelineTrack> &tracks,
                    const QVector<TimelineClip> &clips, const QVector<TimelineMarker> &markers,
                    OperationError *error = nullptr, const QUuid &documentId = {});

    // ---- typed edit transactions (C++) ----
    QString begin(const TimelineEditRequest &request, OperationError *error = nullptr);
    TimelinePlan update(const QString &operationId, const TimelineEditInput &input);
    // createdIds receives the IDs of records the transaction created (new
    // clips, split parts, markers, tracks) in change-set order.
    OperationError commit(const QString &operationId, QString *transactionId = nullptr,
                          QStringList *createdIds = nullptr);
    void cancel(const QString &operationId);
    OperationError apply(const TimelineEditRequest &request, const TimelineEditInput &input,
                         QString *transactionId = nullptr);
    // Guarded structural apply; used by history and external history replay.
    OperationError applyChangeSet(const TimelineChangeSet &changes, bool reverse);
    const TimelineChangeSet *externalStep(const QString &transactionId) const;
    QVariantList changePayload(const TimelineChangeSet &changes) const;

    // ---- QML: content and persistence ----
    Q_INVOKABLE QVariantMap loadContent(const QVariantMap &content);
    Q_INVOKABLE QVariantMap contentMap() const;
    Q_INVOKABLE QString toJson() const;
    Q_INVOKABLE QVariantMap loadJson(const QString &json);
    Q_INVOKABLE void clear();
    Q_INVOKABLE bool setTimebase(const QString &numerator, const QString &denominator);

    // ---- QML: lookup (IDs are UUID strings, ticks decimal strings) ----
    Q_INVOKABLE QStringList trackIds() const;
    Q_INVOKABLE QStringList visibleTrackIds() const;
    Q_INVOKABLE QVariantMap trackData(const QString &id) const;
    Q_INVOKABLE QVariantMap clipData(const QString &id) const;
    Q_INVOKABLE QVariantMap effectiveClipData(const QString &id) const;
    Q_INVOKABLE QVariantMap markerData(const QString &id) const;
    Q_INVOKABLE QStringList markerIds() const;
    Q_INVOKABLE QStringList clipIdsInRange(const QString &trackId, const QString &start,
                                           const QString &end, int limit = kMaxQueryIds) const;
    Q_INVOKABLE QStringList clipIdsInRect(const QString &fromTrackId, const QString &toTrackId,
                                          const QString &start, const QString &end,
                                          int limit = kMaxQueryIds) const;
    Q_INVOKABLE QString clipAt(const QString &trackId, const QString &ticks) const;
    Q_INVOKABLE QString nearestClip(const QString &trackId, const QString &ticks) const;
    Q_INVOKABLE QString adjacentClip(const QString &clipId, int direction) const;
    Q_INVOKABLE QString edgeClip(const QString &trackId, bool last) const;
    Q_INVOKABLE QString adjacentMarker(const QString &ticks, int direction) const;
    Q_INVOKABLE bool hasClip(const QString &id) const;
    Q_INVOKABLE QStringList allClipIds() const;

    // ---- QML: exact arithmetic, conversion and formatting ----
    Q_INVOKABLE QString addTicks(const QString &left, const QString &right) const;
    Q_INVOKABLE QString subtractTicks(const QString &left, const QString &right) const;
    Q_INVOKABLE int compareTicks(const QString &left, const QString &right) const;
    Q_INVOKABLE QString ticksToSamplesText(const QString &ticks, int rounding = TimeMap::NearestAway) const;
    Q_INVOKABLE QString samplesToTicksText(const QString &samples, int rounding = TimeMap::NearestAway) const;
    Q_INVOKABLE QString ticksToFramesText(const QString &ticks, int rounding = TimeMap::Floor) const;
    Q_INVOKABLE QString frameStartTicksText(const QString &frame) const;
    Q_INVOKABLE double ticksToSeconds(const QString &ticks) const;
    Q_INVOKABLE QString secondsToTicks(double seconds) const;
    Q_INVOKABLE QString formatTicks(const QString &ticks, const QString &mode = QStringLiteral("timecode"),
                                    bool dropFrame = false) const;
    Q_INVOKABLE QVariantMap parseTicks(const QString &text, const QString &mode = QStringLiteral("timecode"),
                                       bool dropFrame = false) const;
    // Lengths ignore the TimeMap origin; timecode lengths are non-drop frame counts.
    Q_INVOKABLE QString formatDuration(const QString &ticks, const QString &mode = QStringLiteral("timecode")) const;
    Q_INVOKABLE QVariantMap parseDuration(const QString &text, const QString &mode = QStringLiteral("timecode")) const;
    Q_INVOKABLE QString stepTicks(const QString &fromTicks, const QString &grid, int steps) const;
    // TimeMap musical (PPQ) tick <-> timeline tick, rounded NearestAway; "" on failure.
    Q_INVOKABLE QString musicalTickAt(const QString &ticks) const;
    Q_INVOKABLE QString ticksAtMusicalTick(const QString &musicalTick) const;
    // Snaps one exact position. options: toleranceTicks, grid, playheadTicks,
    // editRange (bool), excludeMarkerId. Returns {ticks, snapped, kind, targetId}.
    Q_INVOKABLE QVariantMap snapTicks(const QString &ticks, const QVariantMap &options) const;

    // ---- QML: transactions ----
    Q_INVOKABLE QVariantMap beginEdit(const QVariantMap &request);
    Q_INVOKABLE QVariantMap updateEdit(const QString &operationId, const QVariantMap &input);
    Q_INVOKABLE QVariantMap commitEdit(const QString &operationId);
    Q_INVOKABLE bool cancelEdit(const QString &operationId);
    Q_INVOKABLE QVariantMap applyEdit(const QVariantMap &request, const QVariantMap &input = {});
    Q_INVOKABLE bool setEditRange(const QString &start, const QString &end);
    Q_INVOKABLE void clearEditRange();

    // ---- history ----
    bool canUndo() const;
    bool canRedo() const;
    QString undoText() const;
    QString redoText() const;
    int historyCount() const { return int(m_history.size()); }
    int historyLimit() const { return m_historyLimit; }
    void setHistoryLimit(int steps);
    bool externalUndo() const { return m_externalUndo; }
    void setExternalUndo(bool enabled);
    QString lastError() const { return m_lastError; }
    Q_INVOKABLE bool undo();
    Q_INVOKABLE bool redo();
    Q_INVOKABLE void clearHistory();
    // AGENT-CONTRACT: with externalUndo the host's command history owns
    // navigation. It records committed(transactionId, ...) and replays with
    // the model's current revision; the model checks every precondition
    // before mutating anything and rejects a stale or diverged replay.
    Q_INVOKABLE bool applyHistoryChanges(const QString &transactionId, bool reverse,
                                         const QString &expectedRevision);
    Q_INVOKABLE bool releaseHistoryTransaction(const QString &transactionId);

    // ---- edit session state ----
    bool editing() const { return m_session.has_value() && !m_session->stale; }
    QString editOperationId() const { return editing() ? m_session->operationId : QString(); }
    QVariantMap editPreview() const;

signals:
    void contentReset();
    void revisionChanged();
    void structureChanged();
    void clipsChanged();
    void markersChanged();
    void previewChanged();
    void editRangeChanged();
    void editStateChanged();
    void historyChanged();
    void timeMapChanged();
    void policyChanged();
    void lastErrorChanged();
    void clipsRemoved(const QStringList &clipIds);
    void committed(const QString &transactionId, const QString &label, const QVariantList &changes);
    void editInvalidated(const QString &operationId, const QString &reason);
    void rejected(const QString &reason);

private:
    struct TrackIndexData {
        std::vector<IndexEntry> entries;
        qint64 maxDuration = 0;
        qint64 maxEnd = std::numeric_limits<qint64>::min();
    };
    struct Session {
        QString operationId;
        TimelineEditRequest request;
        SnapshotHeader base;
        TimelinePlan plan;
        bool hasPlan = false;
        bool stale = false;
        QHash<QUuid, int> clipChanges;
        QHash<QUuid, int> markerChanges;
        QHash<QUuid, int> trackChanges;
        int orderChange = -1;
    };
    struct HistoryStep {
        TimelineChangeSet changes;
        quint64 bytes = 0;
    };
    friend class TimelinePlanner;

    bool fail(const QString &message);
    OperationError failWith(OperationError error);
    void rebuildTrackIndex(const QUuid &trackId);
    void rebuildAllIndexes();
    void rebuildMarkerIndex();
    void recomputeContentEnd();
    void publishPreview(bool changed);
    void invalidateSession(const QString &reason);
    void endSession();
    void recordHistory(const TimelineChangeSet &changes, quint64 bytes);
    static quint64 cost(const TimelineChangeSet &changes);
    OperationError preflight(const TimelineChangeSet &changes, bool reverse) const;
    void resolveMusicalAnchors();
    void emitAfterApply(bool structure, bool clips, bool markers, const QStringList &removed);

    DocumentId m_documentId;
    TimelineRate m_timebase;
    QVector<QUuid> m_trackOrder;
    QHash<QUuid, TimelineTrack> m_tracks;
    QHash<QUuid, TimelineClip> m_clips;
    QHash<QUuid, TimelineMarker> m_markers;
    QHash<QUuid, TrackIndexData> m_indexes;
    mutable std::vector<EdgeEntry> m_clipEdges;
    mutable std::vector<EdgeEntry> m_markerEdges;
    mutable std::vector<IndexEntry> m_markerIndex;
    mutable qint64 m_markerMaxDuration = 0;
    mutable bool m_clipEdgesDirty = true;
    mutable bool m_markerEdgesDirty = true;
    mutable QHash<QUuid, QVector<QUuid>> m_links;   // linkId -> clips, rebuilt per revision
    mutable quint64 m_linksRevision = 0;
    mutable quint64 m_linksGeneration = 0;
    mutable bool m_linksValid = false;
    qint64 m_contentEnd = 0;
    bool m_hasRange = false;
    qint64 m_rangeStart = 0;
    qint64 m_rangeEnd = 0;
    quint64 m_revision = 0;
    quint64 m_generation = 0;
    std::optional<Session> m_session;
    QStringList m_retiredOperations; // invalidated gestures report StaleRevision
    QVector<HistoryStep> m_history;
    qsizetype m_cursor = 0;
    quint64 m_historyBytes = 0;
    quint64 m_historyBudget = 64ULL * 1024ULL * 1024ULL;
    int m_historyLimit = 256;
    bool m_externalUndo = false;
    QHash<QString, TimelineChangeSet> m_externalSteps;
    QStringList m_externalOrder;
    QPointer<TimeMap> m_timeMap;
    TimeMap *m_defaultTimeMap = nullptr;
    QMetaObject::Connection m_timeMapConnection;
    QPointer<TimelineEditPolicy> m_policy;
    TimelineEditPolicy *m_defaultPolicy = nullptr;
    QString m_lastError;
};

} // namespace QindaTK
