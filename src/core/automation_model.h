// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "automation_types.h"
#include "timeline_time.h"

#include <QHash>
#include <QObject>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

enum class AutomationEditKind {
    MovePoints, AddPoint, RemovePoints, SetPoint, MoveSegment, SetHandle, SetInterpolation, EditParameter
};

struct AutomationEditFields {
    std::optional<qint64> ticks;
    std::optional<double> value;
    std::optional<CurveInterpolation> interpolation;
    std::optional<AutomationHandle> handle;
    bool clearHandle = false;
    std::optional<double> minimum, maximum, defaultValue;
};

struct AutomationEditRequest {
    AutomationEditKind kind = AutomationEditKind::MovePoints;
    QString parameterId;
    QVector<QUuid> pointIds;     // MovePoints/RemovePoints/SetInterpolation
    QUuid anchorPointId;         // MovePoints anchor; SetPoint/SetHandle point; MoveSegment left point
    QString side;                // SetHandle: "in" or "out"
    QString label;
    std::optional<quint64> expectedRevision;
    AutomationEditFields fields; // AddPoint template or initial values
};

// One member of an atomic multi-parameter transaction (applyBatch).
struct AutomationBatchEdit {
    AutomationEditRequest request;
    AutomationEditFields input;
};

// AGENT-CONTRACT: AutomationModel owns automation curves keyed by stable
// parameter IDs (WP-049). Points carry exact timeline ticks and canonical
// unit values; AutomationLane/CurveEditor only request edits. A gesture is one
// session (begin -> update* -> commit | cancel) and one undo step; a record
// pass is one undo step. It is also the reference automation engine for
// read/write/touch/latch: timestamps arrive from the caller's transport clock
// as exact ticks, never from GUI frames. Engines consume committed curves
// through EngineSchedule snapshots (docs/tempo-automation.md).
class AutomationModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString schemaId READ schemaId CONSTANT)
    Q_PROPERTY(QString revision READ revisionText NOTIFY revisionChanged)
    Q_PROPERTY(QStringList parameterIds READ parameterIds NOTIFY parametersChanged)
    Q_PROPERTY(QString timebaseNumerator READ timebaseNumeratorText NOTIFY contentReset)
    Q_PROPERTY(QString timebaseDenominator READ timebaseDenominatorText NOTIFY contentReset)
    Q_PROPERTY(QString returnTicks READ returnTicksText WRITE setReturnTicksText NOTIFY settingsChanged)
    Q_PROPERTY(bool editing READ editing NOTIFY editStateChanged)
    Q_PROPERTY(QString editOperationId READ editOperationId NOTIFY editStateChanged)
    Q_PROPERTY(QString previewError READ previewError NOTIFY pointsChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
    Q_PROPERTY(QString undoText READ undoText NOTIFY historyChanged)
    Q_PROPERTY(QString redoText READ redoText NOTIFY historyChanged)
    Q_PROPERTY(int historyCount READ historyCount NOTIFY historyChanged)
    Q_PROPERTY(bool transportRunning READ transportRunning NOTIFY transportChanged)
    Q_PROPERTY(QString transportTicks READ transportTicksText NOTIFY transportChanged)
    Q_PROPERTY(QVariantMap capabilities READ capabilities NOTIFY capabilitiesChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
public:
    static constexpr int kHistoryLimit = 256;

    struct Lane {
        AutomationParameter parameter;
        QVector<AutomationPoint> points;   // committed, sorted, implicitly shared
        AutomationMode mode = AutomationMode::Read;
        bool touched = false;
        bool latched = false;
        bool writing = false;
        qint64 passStart = 0;
        qint64 lastTicks = 0;
        double lastValue = 0.0;            // the live (gesture or held) value
        QVector<AutomationPoint> pass;     // samples of the active record pass
    };

    explicit AutomationModel(QObject *parent = nullptr);

    static QString schemaId() { return QStringLiteral("qindatk.automation/1"); }
    quint64 revisionValue() const { return m_revision; }
    QString revisionText() const { return QString::number(m_revision); }
    QStringList parameterIds() const { return m_order; }
    TimelineRate timebase() const { return m_timebase; }
    QString timebaseNumeratorText() const { return QString::number(m_timebase.numerator); }
    QString timebaseDenominatorText() const { return QString::number(m_timebase.denominator); }
    qint64 returnTicks() const { return m_returnTicks; }
    QString returnTicksText() const { return QString::number(m_returnTicks); }
    void setReturnTicksText(const QString &ticks);
    bool editing() const { return m_session.has_value(); }
    QString editOperationId() const { return m_session ? m_session->operationId : QString(); }
    QString previewError() const { return m_session ? m_session->error.message : QString(); }
    bool canUndo() const { return m_cursor > 0; }
    bool canRedo() const { return m_cursor < m_history.size(); }
    QString undoText() const { return canUndo() ? m_history.at(m_cursor - 1).label : QString(); }
    QString redoText() const { return canRedo() ? m_history.at(m_cursor).label : QString(); }
    int historyCount() const { return int(m_history.size()); }
    // AudioSessionHistory: steps undo() and redo() can reach, and a count of
    // steps ever recorded here (it never goes back).
    int undoDepth() const { return int(m_cursor); }
    int redoDepth() const { return int(m_history.size() - m_cursor); }
    quint64 historySerial() const { return m_historySerial; }
    bool transportRunning() const { return m_running; }
    QString transportTicksText() const { return QString::number(m_transportTicks); }
    QVariantMap capabilities() const;
    QString lastError() const { return m_lastError; }

    // ---- C++ state ----
    const Lane *lane(const QString &parameterId) const;
    // Committed points plus the active preview or record pass.
    QVector<AutomationPoint> effectivePoints(const QString &parameterId) const;
    bool addParameter(const AutomationParameter &parameter, OperationError *error = nullptr);
    bool modeAvailable(AutomationMode mode, QString *reason = nullptr) const;
    void setModeAvailable(AutomationMode mode, bool available, const QString &reason = {});
    OperationError setMode(const QString &parameterId, AutomationMode mode);
    OperationError setTimebase(TimelineRate timebase);

    // ---- C++ transactions ----
    QString begin(const AutomationEditRequest &request, OperationError *error = nullptr);
    OperationError update(const QString &operationId, const AutomationEditFields &input);
    OperationError commit(const QString &operationId, QString *transactionId = nullptr,
                          QStringList *createdIds = nullptr);
    void cancel(const QString &operationId);
    // AGENT-CONTRACT: MotionComposition path/tangent gestures edit x and y
    // curves together. Every edit is planned against the committed curves
    // (each parameter at most once); all are recorded as ONE history step,
    // or nothing changes. An empty diff records nothing.
    OperationError applyBatch(const QVector<AutomationBatchEdit> &edits, const QString &label,
                              QString *transactionId = nullptr, QStringList *createdIds = nullptr);

    // ---- C++ record path (ticks from the transport/engine clock) ----
    void transportStarted(qint64 ticks);
    void transportStopped(qint64 ticks);
    void transportMoved(qint64 ticks);
    OperationError beginGesture(const QString &parameterId, qint64 ticks, double value);
    OperationError recordGesture(const QString &parameterId, qint64 ticks, double value);
    OperationError endGesture(const QString &parameterId, qint64 ticks);
    // A canceled gesture that opened the active pass (touch or latch): the
    // pass is dropped without a history step and the control is released.
    OperationError discardPass(const QString &parameterId);

    // ---- QML: parameters, points and sampling ----
    Q_INVOKABLE QVariantMap addParameterMap(const QVariantMap &parameter);
    Q_INVOKABLE bool removeParameter(const QString &parameterId);
    Q_INVOKABLE QVariantMap parameterData(const QString &parameterId) const;
    Q_INVOKABLE QVariantList points(const QString &parameterId) const;
    Q_INVOKABLE QVariantMap pointData(const QString &parameterId, const QString &pointId) const;
    Q_INVOKABLE QString adjacentPoint(const QString &parameterId, const QString &pointId, int direction) const;
    Q_INVOKABLE QString segmentAt(const QString &parameterId, const QString &ticks) const;
    Q_INVOKABLE double valueAt(const QString &parameterId, const QString &ticks) const;
    Q_INVOKABLE double normalize(const QString &parameterId, double value) const;
    Q_INVOKABLE double denormalize(const QString &parameterId, double y) const;
    Q_INVOKABLE QString formatValue(const QString &parameterId, double value) const;
    Q_INVOKABLE QVariantMap parseValue(const QString &parameterId, const QString &text) const;
    // {x, y} normalized over [startTicks, endTicks] for CurveEditor.samples.
    Q_INVOKABLE QVariantList sampleLane(const QString &parameterId, const QString &startTicks,
                                        const QString &endTicks, int columns) const;

    // ---- QML: transactions and history ----
    Q_INVOKABLE QVariantMap beginEdit(const QVariantMap &request);
    Q_INVOKABLE QVariantMap updateEdit(const QString &operationId, const QVariantMap &input);
    Q_INVOKABLE QVariantMap commitEdit(const QString &operationId);
    Q_INVOKABLE bool cancelEdit(const QString &operationId);
    Q_INVOKABLE QVariantMap applyEdit(const QVariantMap &request, const QVariantMap &input = {});
    Q_INVOKABLE bool undo();
    Q_INVOKABLE bool redo();
    Q_INVOKABLE void clearHistory();

    // ---- QML: modes and the record path ----
    Q_INVOKABLE QVariantMap setModeName(const QString &parameterId, const QString &mode);
    Q_INVOKABLE QVariantMap writeState(const QString &parameterId) const;
    Q_INVOKABLE void setModeAvailability(const QString &mode, bool available, const QString &reason = {});
    Q_INVOKABLE void setTransportState(bool running, const QString &ticks);
    Q_INVOKABLE void setTransportTicks(const QString &ticks);
    Q_INVOKABLE QVariantMap beginGestureAt(const QString &parameterId, const QString &ticks, double value);
    Q_INVOKABLE QVariantMap recordGestureAt(const QString &parameterId, const QString &ticks, double value);
    Q_INVOKABLE QVariantMap endGestureAt(const QString &parameterId, const QString &ticks);

    // ---- QML: persistence ----
    Q_INVOKABLE QVariantMap contentMap() const;
    Q_INVOKABLE QVariantMap loadContent(const QVariantMap &content);
    Q_INVOKABLE QString toJson() const;
    Q_INVOKABLE QVariantMap loadJson(const QString &json);

signals:
    void revisionChanged();
    void parametersChanged();
    void pointsChanged(const QString &parameterId);
    void modeChanged(const QString &parameterId);
    void writeStateChanged(const QString &parameterId);
    void editStateChanged();
    void historyChanged();
    void transportChanged();
    void capabilitiesChanged();
    void settingsChanged();
    void contentReset();
    void lastErrorChanged();
    void committed(const QString &transactionId, const QString &label);
    void editInvalidated(const QString &operationId, const QString &reason);
    void rejected(const QString &reason);

private:
    struct Session {
        QString operationId;
        AutomationEditRequest request;
        quint64 baseRevision = 0;
        QUuid createdId;
        std::optional<AutomationParameter> parameter; // EditParameter candidate
        QVector<AutomationPoint> preview;
        bool hasPreview = false;
        OperationError error;
    };
    OperationError plan(Session &session, const AutomationEditFields &input) const;
    OperationError record(AutomationChangeSet changes);
    OperationError applyChanges(const AutomationChangeSet &changes, bool reverse);
    static AutomationChangeSet diff(const QString &parameterId, const QVector<AutomationPoint> &before,
                                    const QVector<AutomationPoint> &after);
    void startPass(Lane &lane, qint64 ticks);
    void appendSample(Lane &lane, qint64 ticks, double value);
    QVector<AutomationPoint> passResult(const Lane &lane, qint64 end, bool withReturn) const;
    void finishPass(const QString &parameterId, qint64 end);
    void invalidateSessionFor(const QString &parameterId, const QString &reason);
    OperationError fail(OperationError error);
    QVariantMap pointRow(const AutomationParameter &parameter, const AutomationPoint &point) const;

    QHash<QString, Lane> m_lanes;
    QStringList m_order;
    TimelineRate m_timebase{TimelineTime::kDefaultTicksPerSecond, 1};
    qint64 m_returnTicks = TimelineTime::kDefaultTicksPerSecond / 50;
    quint64 m_revision = 0;
    std::optional<Session> m_session;
    QVector<AutomationChangeSet> m_history;
    qsizetype m_cursor = 0;
    quint64 m_historySerial = 0;
    bool m_running = false;
    qint64 m_transportTicks = 0;
    QHash<int, QString> m_unavailable; // AutomationMode -> reason
    QString m_lastError;
};

} // namespace QindaTK
