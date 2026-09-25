// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "audio_mixer_model.h"

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QTimer>
#include <QtQml/qqmlregistration.h>
#include <memory>

namespace QindaTK {

class AudioParameterBinding;

// AGENT-CONTRACT (WP-062, AUD-13, docs/audio-mixer.md, D-110): a hardware
// control surface speaking the Mackie Control (MCU) protocol, bound to one
// AudioMixerModel. It edits and follows the SAME parameter model as every
// strip and as automation: faders and V-Pots go through the mixer's shared
// AudioParameterBinding (one transaction per touch, or per burst on a surface
// without touch sensing), buttons are mixer operations. Feedback (motor
// faders, V-Pot rings, button LEDs) is sent whenever the parameter or flag
// changes, from any writer, except to a fader the surface itself holds.
// Surface strips 1-8 show `bank` onward of the mixer's shown channels (the
// master has its own fader). A bank change waits while a surface gesture is
// open, as MixerView does (D-113). MIDI arrives through receive() or the
// ALSA sequencer port (openPort); feedback leaves through outgoing() and
// that port. Messages (MIDI channel 1 unless noted):
//   fader n    pitch bend on channel n (1-8), 14-bit travel; the master on 9
//   touch n    note 104+n (the master 112), velocity 127 touch, 0 release
//   V-Pot n    CC 16+n: bit 6 set turns left, bits 0-5 count steps of the
//              parameter's step (pan: 0.01); the ring CC 48+n, dot mode 1-11
//   buttons    note 0+n arm, 8+n solo, 16+n mute, 24+n select (LED: the same
//              note, velocity 127 on, 0 off); 46/47 bank -8/+8, 48/49 -1/+1
class AudioControlSurface : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QindaTK::AudioMixerModel *mixer READ mixer WRITE setMixer NOTIFY mixerChanged)
    Q_PROPERTY(int bank READ bank WRITE setBank NOTIFY bankChanged)
    Q_PROPERTY(int stripCount READ stripCount CONSTANT)
    Q_PROPERTY(QStringList channels READ channels NOTIFY bankChanged)
    Q_PROPERTY(QString selectedChannelId READ selectedChannelId NOTIFY selectedChannelChanged)
    Q_PROPERTY(bool held READ held NOTIFY heldChanged)
    Q_PROPERTY(bool portOpen READ portOpen NOTIFY portChanged)
    Q_PROPERTY(QString portAddress READ portAddress NOTIFY portChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
public:
    static constexpr int kStrips = 8;
    static constexpr int kMasterStrip = 8;   // index of the master fader
    static constexpr int kIdleMs = 300;      // an untouched fader or V-Pot burst ends after this
    static constexpr int kFaderTop = 16383;  // 14-bit pitch bend

    explicit AudioControlSurface(QObject *parent = nullptr);
    ~AudioControlSurface() override;

    AudioMixerModel *mixer() const { return m_mixer; }
    void setMixer(AudioMixerModel *mixer);
    int bank() const { return m_bank; }
    void setBank(int bank);
    int stripCount() const { return kStrips; }
    // The channel on each surface strip ("" when the bank runs out).
    QStringList channels() const;
    QString selectedChannelId() const { return m_selected; }
    bool held() const;
    bool portOpen() const;
    QString portAddress() const;
    QString lastError() const { return m_lastError; }

    // MIDI from the surface: one or more complete messages (running status
    // and interleaved realtime bytes are accepted; SysEx is skipped).
    Q_INVOKABLE bool receive(const QByteArray &bytes);
    // Sends the whole bank again (faders, rings and LEDs), e.g. after a
    // surface reconnects.
    Q_INVOKABLE void refresh();
    // Ends every open surface gesture on the binding it began on (a surface
    // that goes away mid-touch); the values reached are committed.
    Q_INVOKABLE void releaseAll();

    // ---- ALSA sequencer transport (audio_control_surface_port.cpp) ----
    // A duplex sequencer port named `name`; false (and lastError) without
    // ALSA or a sequencer. connectPort("client:port") subscribes both ways.
    Q_INVOKABLE bool openPort(const QString &name = QStringLiteral("QindaTK control surface"));
    Q_INVOKABLE void closePort();
    Q_INVOKABLE bool connectPort(const QString &address);

signals:
    void mixerChanged();
    void bankChanged();
    void selectedChannelChanged();
    void heldChanged();
    void portChanged();
    void lastErrorChanged();
    // One feedback message for the surface.
    void outgoing(const QByteArray &message);
    // The surface's SELECT button: hosts move their selection (MixerView).
    void channelSelected(const QString &channelId);

private:
    struct Gesture {
        QPointer<AudioParameterBinding> binding;
        double value = 0.0;
        bool silent = false;
        bool touched = false;
    };
    struct Port;

    QStringList shownChannels() const;
    QString channelAt(int strip) const;
    AudioParameterBinding *bindingFor(int strip, const QString &kind);
    void handle(quint8 status, quint8 a, quint8 b);
    void button(quint8 note, bool pressed);
    void fader(int strip, int travel);
    void touch(int strip, bool on);
    void vpot(int strip, quint8 value);
    Gesture *open(QHash<int, Gesture> &gestures, int strip, AudioParameterBinding *binding, bool touched);
    void commit(QHash<int, Gesture> &gestures, int strip);
    void idle();
    void applyPendingBank();
    void rewire();
    void sendFader(int strip);
    void sendRing(int strip);
    void sendLeds();
    void send(quint8 status, quint8 a, quint8 b);
    void setError(const QString &message);
    void portWrite(const QByteArray &message);
    void portRead();

    QPointer<AudioMixerModel> m_mixer;
    QList<QMetaObject::Connection> m_mixerConnections;
    QList<QMetaObject::Connection> m_stripConnections;
    int m_bank = 0;
    int m_pendingBank = -1;
    QString m_selected;
    QHash<int, Gesture> m_faders; // by surface strip (kMasterStrip: the master)
    QHash<int, Gesture> m_vpots;
    QHash<int, int> m_sentFader;  // last travel sent per strip
    QHash<int, int> m_sentRing;
    QHash<int, int> m_sentLed;    // by note
    QTimer m_idle;
    quint8 m_running = 0;         // MIDI running status
    QByteArray m_pending;         // bytes of an incomplete message
    bool m_sysex = false;
    std::unique_ptr<Port> m_port;
    QString m_lastError;
};

} // namespace QindaTK
