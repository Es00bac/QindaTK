// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "application_contracts.h"
#include "time_map.h"
#include "timeline_model.h"

#include <QObject>
#include <QPointer>
#include <QUuid>
#include <QVariantList>
#include <QVector>
#include <QtQml/qqmlregistration.h>
#include <optional>

namespace QindaTK {

// One tempo or meter change at an exact musical position (TimeMap PPQ ticks).
// Tempo is integer microseconds per quarter note (the MIDI unit TimeMap
// stores); BPM is presentation. A meter change must fall on a bar line of the
// meter before it. The first tempo and first meter sit at tick 0 and cannot
// move or be removed.
struct TempoEvent {
    enum class Kind { Tempo, Meter };
    QUuid id;
    Kind kind = Kind::Tempo;
    qint64 tick = 0;
    qint64 microsecondsPerQuarter = 500000; // Tempo
    int numerator = 4;                      // Meter
    int denominator = 4;                    // Meter
    friend bool operator==(const TempoEvent &, const TempoEvent &) = default;
};

// Values a request or a preview update supplies; unset fields keep the
// event's current value.
struct TempoEventFields {
    std::optional<qint64> tick;
    std::optional<qint64> microsecondsPerQuarter;
    std::optional<int> numerator;
    std::optional<int> denominator;
};

struct TempoChange {
    QUuid id;
    std::optional<TempoEvent> before, after; // missing before = add, missing after = remove
};

struct TempoChangeSet {
    QString transactionId;
    QString label;
    QVector<TempoChange> changes;
};

enum class TempoEditKind { AddTempo, AddMeter, Move, Edit, Remove };

struct TempoEditRequest {
    TempoEditKind kind = TempoEditKind::Edit;
    QUuid eventId;                        // Move/Edit/Remove target
    QString label;                        // history text; a per-kind default when empty
    std::optional<quint64> expectedRevision;
    TempoEventFields fields;              // Add template, or initial Edit values
};

// AGENT-CONTRACT: TempoMapModel is the single authority for tempo and meter
// events (WP-049). TimeMap::tempoSegments is its published projection: the
// model writes it on commit, undo, load and while a gesture previews, so the
// TimelineModel re-derives beat-locked clips and musical markers live, and
// cancel republishes the committed map. Nothing else may write
// tempoSegments on a TimeMap this model drives. Engines read the committed
// events through EngineSchedule snapshots, never the preview (see
// docs/tempo-automation.md).
class TempoMapModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString schemaId READ schemaId CONSTANT)
    Q_PROPERTY(QindaTK::TimeMap *timeMap READ timeMap WRITE setTimeMap NOTIFY timeMapChanged)
    Q_PROPERTY(QindaTK::TimelineModel *timeline READ timeline WRITE setTimeline NOTIFY timelineChanged)
    Q_PROPERTY(QString revision READ revisionText NOTIFY revisionChanged)
    Q_PROPERTY(QVariantList events READ events NOTIFY eventsChanged)
    Q_PROPERTY(int eventCount READ eventCount NOTIFY eventsChanged)
    Q_PROPERTY(int ticksPerQuarter READ ticksPerQuarter NOTIFY timeMapChanged)
    Q_PROPERTY(double minimumBpm READ minimumBpm WRITE setMinimumBpm NOTIFY settingsChanged)
    Q_PROPERTY(double maximumBpm READ maximumBpm WRITE setMaximumBpm NOTIFY settingsChanged)
    Q_PROPERTY(bool editing READ editing NOTIFY editStateChanged)
    Q_PROPERTY(QString editOperationId READ editOperationId NOTIFY editStateChanged)
    Q_PROPERTY(QString previewError READ previewError NOTIFY eventsChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
    Q_PROPERTY(QString undoText READ undoText NOTIFY historyChanged)
    Q_PROPERTY(QString redoText READ redoText NOTIFY historyChanged)
    Q_PROPERTY(int historyCount READ historyCount NOTIFY historyChanged)
    Q_PROPERTY(QVariantMap capabilities READ capabilities CONSTANT)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
public:
    static constexpr int kMaxEvents = 4096;
    static constexpr int kHistoryLimit = 256;

    explicit TempoMapModel(QObject *parent = nullptr);
    ~TempoMapModel() override;

    static QString schemaId() { return QStringLiteral("qindatk.tempo/1"); }
    TimeMap *timeMap() const { return m_timeMap; }
    void setTimeMap(TimeMap *map);
    TimelineModel *timeline() const { return m_timeline; }
    void setTimeline(TimelineModel *timeline);
    quint64 revisionValue() const { return m_revision; }
    QString revisionText() const { return QString::number(m_revision); }
    QVariantList events() const;
    int eventCount() const { return int(effectiveEvents().size()); }
    int ticksPerQuarter() const { return m_timeMap ? m_timeMap->ticksPerQuarter() : 960; }
    double minimumBpm() const { return m_minimumBpm; }
    void setMinimumBpm(double bpm);
    double maximumBpm() const { return m_maximumBpm; }
    void setMaximumBpm(double bpm);
    bool editing() const { return m_session.has_value(); }
    QString editOperationId() const { return m_session ? m_session->operationId : QString(); }
    QString previewError() const { return m_session ? m_session->error.message : QString(); }
    bool canUndo() const { return m_cursor > 0; }
    bool canRedo() const { return m_cursor < m_history.size(); }
    QString undoText() const { return canUndo() ? m_history.at(m_cursor - 1).label : QString(); }
    QString redoText() const { return canRedo() ? m_history.at(m_cursor).label : QString(); }
    int historyCount() const { return int(m_history.size()); }
    QVariantMap capabilities() const;
    QString lastError() const { return m_lastError; }

    // ---- C++ state and rules ----
    const QVector<TempoEvent> &committedEvents() const { return m_events; }
    QVector<TempoEvent> effectiveEvents() const;
    const TempoEvent *event(const QUuid &id) const;
    OperationError validate(const QVector<TempoEvent> &events) const;
    // Merged TimeMap segments (tick, tempo, meter at every change), decimal strings.
    QVariantList segmentsFor(const QVector<TempoEvent> &events) const;
    bool setEvents(const QVector<TempoEvent> &events, OperationError *error = nullptr);
    static QVector<TempoEvent> defaultEvents();
    static void sortEvents(QVector<TempoEvent> &events);
    std::pair<qint64, qint64> microsecondRange() const; // from minimumBpm/maximumBpm
    // Beat and bar grid of `events`, optionally as if `exclude` were absent.
    struct MeterSpan { qint64 tick = 0; qint64 bar = 0; qint64 beatTicks = 960; int numerator = 4; };
    QVector<MeterSpan> meterSpans(const QVector<TempoEvent> &events, const QUuid &exclude = {}) const;

    // ---- C++ transactions: begin -> update* -> commit | cancel ----
    QString begin(const TempoEditRequest &request, OperationError *error = nullptr);
    OperationError update(const QString &operationId, const TempoEventFields &input);
    OperationError commit(const QString &operationId, QString *transactionId = nullptr,
                          QStringList *createdIds = nullptr);
    void cancel(const QString &operationId);

    // ---- QML: transactions and history ----
    Q_INVOKABLE QVariantMap beginEdit(const QVariantMap &request);
    Q_INVOKABLE QVariantMap updateEdit(const QString &operationId, const QVariantMap &input);
    Q_INVOKABLE QVariantMap commitEdit(const QString &operationId);
    Q_INVOKABLE bool cancelEdit(const QString &operationId);
    Q_INVOKABLE QVariantMap applyEdit(const QVariantMap &request, const QVariantMap &input = {});
    Q_INVOKABLE bool undo();
    Q_INVOKABLE bool redo();
    Q_INVOKABLE void clearHistory();

    // ---- QML: persistence ----
    Q_INVOKABLE QVariantMap contentMap() const;
    Q_INVOKABLE QVariantMap loadContent(const QVariantMap &content);
    Q_INVOKABLE QString toJson() const;
    Q_INVOKABLE QVariantMap loadJson(const QString &json);
    Q_INVOKABLE void reset();

    // ---- QML: lookup and musical arithmetic (ticks are decimal strings) ----
    Q_INVOKABLE QVariantMap eventData(const QString &id) const;
    Q_INVOKABLE QStringList eventIds() const;
    Q_INVOKABLE QString adjacentEvent(const QString &fromId, int direction) const;
    Q_INVOKABLE QVariantMap tempoAt(const QString &tick) const;
    Q_INVOKABLE QVariantMap meterAt(const QString &tick) const;
    Q_INVOKABLE QString formatPosition(const QString &tick) const;  // "bar.beat.tick", 1-based
    Q_INVOKABLE QVariantMap parsePosition(const QString &text) const;
    Q_INVOKABLE QString formatBpm(const QString &microsecondsPerQuarter) const;
    Q_INVOKABLE QVariantMap parseBpm(const QString &text) const;
    Q_INVOKABLE QString microsecondsForBpm(double bpm) const;
    // grid: "bar", "beat" or "none"; excludeId ignores that meter event.
    Q_INVOKABLE QString snapTick(const QString &tick, const QString &grid,
                                 const QString &excludeId = {}) const;
    Q_INVOKABLE QString stepTick(const QString &tick, const QString &grid, int steps,
                                 const QString &excludeId = {}) const;

signals:
    void timeMapChanged();
    void timelineChanged();
    void revisionChanged();
    void eventsChanged();
    void editStateChanged();
    void historyChanged();
    void settingsChanged();
    void lastErrorChanged();
    void contentReset();
    void committed(const QString &transactionId, const QString &label);
    void editInvalidated(const QString &operationId, const QString &reason);
    void rejected(const QString &reason);

private:
    struct Session {
        QString operationId;
        TempoEditRequest request;
        quint64 baseRevision = 0;
        QUuid createdId;
        QVector<TempoEvent> preview;
        bool hasPreview = false;
        OperationError error;
    };
    OperationError plan(Session &session, const TempoEventFields &input) const;
    // Writes events to the TimeMap; reports a beat-locked placement conflict.
    OperationError publish(const QVector<TempoEvent> &events);
    OperationError applyChanges(const TempoChangeSet &changes, bool reverse);
    void endSession();
    void onTimeMapChanged();
    OperationError fail(OperationError error);
    QVariantMap eventRow(const TempoEvent &event, const QVector<MeterSpan> &spans, bool preview) const;
    static QString positionText(qint64 tick, const QVector<MeterSpan> &spans);
    static QString bpmText(qint64 microsecondsPerQuarter);

    QVector<TempoEvent> m_events;
    quint64 m_revision = 0;
    std::optional<Session> m_session;
    QVector<TempoChangeSet> m_history;
    qsizetype m_cursor = 0;
    QPointer<TimeMap> m_timeMap;
    QMetaObject::Connection m_timeMapConnection;
    QPointer<TimelineModel> m_timeline;
    bool m_publishing = false;
    double m_minimumBpm = 10.0;
    double m_maximumBpm = 999.0;
    QString m_lastError;
};

} // namespace QindaTK
