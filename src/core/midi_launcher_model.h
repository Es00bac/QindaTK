// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "midi_sequence_model.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

class AudioEngine;

// AGENT-CONTRACT (D-153): the clip launcher of an AudioSession
// (`session.launcher`, WP-064, docs/audio-midi.md). Slots come from the MIDI
// model; launch and stop requests go to the engine, which quantizes them on
// the device thread. Slot states change only from engine events: "queued"
// when the engine accepted a launch for a boundary, "playing" when that
// boundary was rendered, "ready" when it stopped. `slotList` is the
// ClipLauncher `slots` snapshot; launchAcknowledged carries the one answer
// to each request (accepted | rejected | canceled) for acknowledgeLaunch().
class MidiLauncherModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("The launcher belongs to an AudioSession (session.launcher)")
    Q_PROPERTY(QVariantList slotList READ slotList NOTIFY slotListChanged)
    Q_PROPERTY(QStringList playingSlots READ playingSlots NOTIFY slotListChanged)
    Q_PROPERTY(QStringList queuedSlots READ queuedSlots NOTIFY slotListChanged)
    Q_PROPERTY(QString quantization READ quantization WRITE setQuantization NOTIFY slotListChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
public:
    MidiLauncherModel(MidiSequenceModel *midi, QObject *parent = nullptr);

    void setEngine(AudioEngine *engine);
    QVariantList slotList() const;
    QStringList playingSlots() const;
    QStringList queuedSlots() const;
    QString quantization() const { return m_midi->quantization(); }
    void setQuantization(const QString &name) { m_midi->setQuantization(name); }
    QString lastError() const { return m_lastError; }

    // An empty quantization uses the session's (none, beat or bar). Returns
    // false when refused at once; launchAcknowledged has then already been
    // emitted with "rejected".
    Q_INVOKABLE bool launch(const QString &requestId, const QString &slotId, const QString &quantization = {});
    Q_INVOKABLE bool stop(const QString &requestId, const QString &slotId, const QString &quantization = {});
    Q_INVOKABLE bool stopAll(const QString &requestId, const QString &quantization = {});
    // {state, startSample, stopSample, stopping} of one slot.
    Q_INVOKABLE QVariantMap slotState(const QString &slotId) const;

signals:
    void slotListChanged();
    void lastErrorChanged();
    void launchAcknowledged(const QString &requestId, const QString &state, const QString &message);
    void slotStarted(const QString &slotId, const QString &sample);
    void slotStopped(const QString &slotId, const QString &sample);

private:
    struct Request { QString id; QUuid slot; };
    struct Runtime {
        bool queued = false;
        bool playing = false;
        bool stopping = false;
        qint64 startSample = -1;
        qint64 stopSample = -1;
    };
    const MidiSlot *findSlot(const QUuid &id) const;
    bool post(const QString &requestId, const QUuid &slot, quint64 lane, bool launching, const QString &quantization);
    bool reject(const QString &requestId, const QString &message);
    void acknowledge(quint64 serial, const QString &state, const QString &message);
    void engineEvent(int kind, quint64 serial, quint64 lane, quint64 slot, qint64 sample, int code);
    void reset(const QString &reason);
    void refreshKeys();
    static QString codeText(int code);

    MidiSequenceModel *m_midi = nullptr;
    QPointer<AudioEngine> m_engine;
    QList<QMetaObject::Connection> m_connections;
    QHash<quint64, Request> m_requests;  // waiting for their answer
    QHash<quint64, Request> m_launches;  // accepted, waiting for their boundary
    QHash<QUuid, Runtime> m_runtime;
    QHash<quint64, QUuid> m_slotKeys;
    QHash<quint64, QUuid> m_laneKeys;
    QString m_lastError;
};

} // namespace QindaTK
