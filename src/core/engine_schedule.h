// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "automation_model.h"
#include "tempo_map_model.h"

#include <QObject>
#include <QPointer>
#include <QtQml/qqmlregistration.h>
#include <atomic>
#include <memory>

namespace QindaTK {

// AGENT-CONTRACT: an immutable, timestamped view of the committed tempo map
// and automation that an audio engine reads on any thread (WP-049 -> WP-060).
// It holds no QObject that lives on a thread (its TimeMap copy is detached),
// never calls QML, and never reads a GUI frame clock: every query takes an
// exact sample or tick timestamp. Tempo previews never reach a snapshot; the
// live value of a control that is being written does, as `overriding`.
class EngineScheduleSnapshot {
public:
    struct Parameter {
        AutomationParameter parameter;
        QVector<AutomationPoint> points; // implicitly shared, read-only
        AutomationMode mode = AutomationMode::Read;
        bool overriding = false;         // a write pass or Off: the engine uses liveValue
        double liveValue = 0.0;
    };
    struct Value {
        bool known = false;       // the parameter exists
        bool automated = false;   // the curve drives it (read, or a write mode not writing)
        double value = 0.0;
    };

    EngineScheduleSnapshot();
    ~EngineScheduleSnapshot();
    EngineScheduleSnapshot(const EngineScheduleSnapshot &) = delete;
    EngineScheduleSnapshot &operator=(const EngineScheduleSnapshot &) = delete;

    quint64 generation() const { return m_generation; }
    quint64 tempoRevision() const { return m_tempoRevision; }
    quint64 automationRevision() const { return m_automationRevision; }
    TimelineRate timebase() const { return m_timebase; }
    int sampleRate() const { return m_sampleRate; }
    int ticksPerQuarter() const { return m_ticksPerQuarter; }
    const QVector<TempoEvent> &tempoEvents() const { return m_tempoEvents; }
    QStringList parameterIds() const { return m_order; }
    const Parameter *parameter(const QString &id) const;

    // Timeline ticks and samples share tick 0; musical ticks follow the tempo
    // map exactly as TimelineModel::musicalTickFor does (sample-quantized).
    std::optional<qint64> ticksAtSample(qint64 sample) const;
    std::optional<qint64> sampleAtTicks(qint64 ticks) const;
    std::optional<qint64> musicalTickAtSample(qint64 sample) const;
    std::optional<qint64> sampleAtMusicalTick(qint64 musicalTick) const;
    qint64 microsecondsPerQuarterAt(qint64 musicalTick) const;
    Value valueAtTicks(const QString &parameterId, qint64 ticks) const;
    Value valueAtSample(const QString &parameterId, qint64 sample) const;
    // Sample-accurate block: out[i] is the value at startSample + i.
    bool renderBlock(const QString &parameterId, qint64 startSample, int frames, double *out) const;

private:
    friend class EngineSchedule;
    quint64 m_generation = 0;
    quint64 m_tempoRevision = 0;
    quint64 m_automationRevision = 0;
    TimelineRate m_timebase{TimelineTime::kDefaultTicksPerSecond, 1};
    int m_sampleRate = 48000;
    int m_ticksPerQuarter = 960;
    QVector<TempoEvent> m_tempoEvents;
    std::unique_ptr<TimeMap> m_timeMap;
    QHash<QString, Parameter> m_parameters;
    QStringList m_order;
};

using EngineScheduleSnapshotPtr = std::shared_ptr<const EngineScheduleSnapshot>;

// Publishes a new snapshot whenever committed tempo or automation changes, or
// a write pass changes a live value. acquire() is safe from any thread.
class EngineSchedule : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QindaTK::TempoMapModel *tempoModel READ tempoModel WRITE setTempoModel NOTIFY configurationChanged)
    Q_PROPERTY(QindaTK::AutomationModel *automationModel READ automationModel WRITE setAutomationModel NOTIFY configurationChanged)
    Q_PROPERTY(QString generation READ generationText NOTIFY published)
public:
    explicit EngineSchedule(QObject *parent = nullptr);
    ~EngineSchedule() override;
    TempoMapModel *tempoModel() const { return m_tempo; }
    void setTempoModel(TempoMapModel *model);
    AutomationModel *automationModel() const { return m_automation; }
    void setAutomationModel(AutomationModel *model);
    QString generationText() const;
    EngineScheduleSnapshotPtr acquire() const { return m_current.load(std::memory_order_acquire); }
    Q_INVOKABLE void publish();
signals:
    void configurationChanged();
    void published();
private:
    void reconnect();
    QPointer<TempoMapModel> m_tempo;
    QPointer<AutomationModel> m_automation;
    QList<QMetaObject::Connection> m_connections;
    std::atomic<EngineScheduleSnapshotPtr> m_current;
    quint64 m_generation = 0;
};

} // namespace QindaTK
