// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "engine_schedule.h"

#include <QObject>
#include <QPointer>
#include <QtQml/qqmlregistration.h>
#include <vector>

namespace QindaTK {

// A MOCK engine: a deterministic sample clock that consumes EngineSchedule
// snapshots, reports its transport to the AutomationModel and records
// gestures at its own timestamps. It proves the scheduling contract; it opens
// no device and is not realtime. The host decides when a block runs (a test,
// or a timer in an example); timestamps come only from the sample counter.
class MockAutomationEngine : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QindaTK::EngineSchedule *schedule READ schedule WRITE setSchedule NOTIFY scheduleChanged)
    Q_PROPERTY(QString positionSample READ positionSample NOTIFY processed)
    Q_PROPERTY(bool playing READ playing NOTIFY processed)
    Q_PROPERTY(int blockFrames READ blockFrames WRITE setBlockFrames NOTIFY scheduleChanged)
    Q_PROPERTY(QVariantMap lastBlock READ lastBlock NOTIFY processed)
public:
    explicit MockAutomationEngine(QObject *parent = nullptr);
    EngineSchedule *schedule() const { return m_schedule; }
    void setSchedule(EngineSchedule *schedule);
    QString positionSample() const { return QString::number(m_position); }
    qint64 position() const { return m_position; }
    bool playing() const { return m_playing; }
    int blockFrames() const { return m_blockFrames; }
    void setBlockFrames(int frames);
    QVariantMap lastBlock() const { return m_lastBlock; }

    // Thread-safe: reads one acquired snapshot.
    std::vector<double> renderBlock(const QString &parameterId, qint64 startSample, int frames) const;

    Q_INVOKABLE void play();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void locate(const QString &sample);
    // One engine block: evaluates every parameter at the block start, then
    // advances the clock by `frames` (blockFrames when 0).
    Q_INVOKABLE QVariantMap process(int frames = 0);
    Q_INVOKABLE QVariantMap valuesAt(const QString &sample) const;
    Q_INVOKABLE QVariantList render(const QString &parameterId, const QString &startSample, int frames,
                                    int stride = 1) const;
    // Control gestures stamped with the engine clock.
    Q_INVOKABLE QVariantMap touch(const QString &parameterId, double value);
    Q_INVOKABLE QVariantMap move(const QString &parameterId, double value);
    Q_INVOKABLE QVariantMap release(const QString &parameterId);
signals:
    void scheduleChanged();
    void processed();
private:
    QVariantMap describe(const EngineScheduleSnapshot &snapshot, qint64 sample) const;
    qint64 ticksNow() const;
    AutomationModel *automation() const;
    QPointer<EngineSchedule> m_schedule;
    qint64 m_position = 0;
    bool m_playing = false;
    int m_blockFrames = 512;
    QVariantMap m_lastBlock;
};

} // namespace QindaTK
