// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QtQml/qqmlregistration.h>
#include <bitset>
#include <memory>

namespace QindaTK {

class AudioEngine;

// AGENT-CONTRACT (D-153): MIDI note input for an AudioSession
// (`session.midiInput`, WP-064, docs/audio-midi.md). One ALSA sequencer
// input port (Linux, the supported platform; D-088 keeps ALSA optional):
// keyboards and software ports connect to it (connectPort, or any sequencer
// patchbay). Note-on/off become audition notes on the session's engine, so
// they sound through the test instrument and a running launcher recording
// (session.launcher.record) captures them at the engine position. Reading
// runs on the GUI thread through a socket notifier, never on the device
// thread. Closing the port or losing the engine releases every held note.
// Device *configuration* (DeviceSettingsPanel, PLT-19) is WP-060's; this is
// the note path WP-064's recording needs.
class MidiInputPort : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("The MIDI input belongs to an AudioSession (session.midiInput)")
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(bool portOpen READ portOpen NOTIFY portChanged)
    Q_PROPERTY(QString portAddress READ portAddress NOTIFY portChanged)
    Q_PROPERTY(int heldNotes READ heldNotes NOTIFY notesChanged)
    Q_PROPERTY(int receivedNotes READ receivedNotes NOTIFY notesChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
public:
    explicit MidiInputPort(QObject *parent = nullptr);
    ~MidiInputPort() override;

    static bool available(); // built with the ALSA sequencer
    void setEngine(AudioEngine *engine);
    bool portOpen() const;
    QString portAddress() const; // "client:port"
    int heldNotes() const { return int(m_held.count()); }
    int receivedNotes() const { return m_received; }
    QString lastError() const { return m_lastError; }

    Q_INVOKABLE bool openPort(const QString &name = QStringLiteral("QindaTK MIDI in"));
    Q_INVOKABLE void closePort();
    // Subscribes the port to a sequencer source ("client:port" or a client name).
    Q_INVOKABLE bool connectPort(const QString &address);
    // A decoded note: what the port does with each incoming message (tests,
    // hosts with their own transport). velocity7 0 is a note-off.
    Q_INVOKABLE void receiveNote(int pitch, int velocity7);

signals:
    void portChanged();
    void notesChanged();
    void lastErrorChanged();

private:
    struct Port;
    void read();
    void releaseAll();
    void setError(const QString &message);

    std::unique_ptr<Port> m_port;
    QPointer<AudioEngine> m_engine;
    std::bitset<128> m_held;
    int m_received = 0;
    QString m_lastError;
};

} // namespace QindaTK
