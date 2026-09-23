// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "transport_model.h"

#include <QHash>
#include <QStringList>

namespace QindaTK {

// Deterministic TEST clock adapter. Advance only when a test calls advance().
// This has no audio/video devices, realtime scheduling, or engine qualification.
class TestTransportClock final : public TransportAdapter {
    Q_OBJECT
public:
    explicit TestTransportClock(QObject *parent = nullptr);
    TransportSnapshot snapshot() const override { return m_state; }
    void submit(const TransportCommand &command, ReplyCallback completion) override;
    void setSnapshotSink(SnapshotCallback sink) override { m_sink = std::move(sink); }
    SnapshotCallback capturedSnapshotSinkForTest() const { return m_sink; }
    QStringList pendingIds() const { return m_pending.keys(); }
    bool process(const QString &requestId);
    bool deliver(const QString &requestId);
    bool acknowledge(const QString &requestId) { return process(requestId) && deliver(requestId); }
    bool advance(qint64 samples);
    bool setPlaybackRate(double rate);
    void externalState(qint64 positionSample, bool playing, bool recording);
    void setDenyRecording(bool denied) { m_denyRecording = denied; }
    void setCanRecord(bool available, const QString &reason = {});
    void setCanSeek(bool available, const QString &reason = {});
    void setDeviceError(const QString &error);
private:
    struct Pending {
        TransportCommand command;
        ReplyCallback callback;
        std::optional<TransportReply> reply;
    };
    void push();
    TransportSnapshot m_state;
    SnapshotCallback m_sink;
    QHash<QString, Pending> m_pending;
    bool m_denyRecording = false;
};
} // namespace QindaTK
