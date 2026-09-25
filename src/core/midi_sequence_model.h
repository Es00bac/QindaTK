// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "midi_types.h"

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
#include <functional>
#include <optional>

namespace QindaTK {

// AGENT-CONTRACT (D-150): the MIDI authority of an AudioSession
// (`session.midi`, WP-064, docs/audio-midi.md). It owns patterns (notes,
// controller and per-note events, step rows), scenes and launcher slots.
// Every accepted change is validated whole and recorded as one step of the
// model's own history (per-owner undo, like the mixer and source edits);
// undo restores the complete previous content exactly. A refused change
// changes nothing and records nothing. PianoRoll, MidiControllerLane and
// StepSequencer requests map one-to-one onto the invokables below; a drag is
// previewed (the snapshots show it, the engine keeps the committed content)
// and becomes one step when it finishes.
class MidiSequenceModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString schemaId READ schemaId CONSTANT)
    Q_PROPERTY(int ticksPerQuarter READ ticksPerQuarter WRITE setTicksPerQuarter NOTIFY ticksPerQuarterChanged)
    Q_PROPERTY(QString revision READ revisionText NOTIFY contentChanged)
    Q_PROPERTY(QVariantList patterns READ patterns NOTIFY contentChanged)
    Q_PROPERTY(QVariantList scenes READ scenes NOTIFY contentChanged)
    Q_PROPERTY(QString quantization READ quantization WRITE setQuantization NOTIFY contentChanged)
    Q_PROPERTY(QVariantList controllers READ controllers CONSTANT)
    Q_PROPERTY(QVariantMap capabilities READ capabilities CONSTANT)
    Q_PROPERTY(bool editing READ editing NOTIFY previewChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
    Q_PROPERTY(QString undoText READ undoText NOTIFY historyChanged)
    Q_PROPERTY(QString redoText READ redoText NOTIFY historyChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
public:
    static constexpr int kHistoryLimit = 256;

    explicit MidiSequenceModel(QObject *parent = nullptr);
    static QString schemaId() { return QStringLiteral("qindatk.midi/1"); }

    // ---- C++ state ----
    const MidiContent &content() const { return m_content; }
    const MidiPattern *pattern(const QUuid &id) const { return Midi::find(m_content, id); }
    // Validates, records one history step and publishes; false (with the
    // reason) changes nothing. An unchanged content records nothing.
    bool commitContent(const MidiContent &next, const QString &label, QString *error = nullptr);
    int ticksPerQuarter() const { return m_ppq; }
    void setTicksPerQuarter(int ppq);
    quint64 revisionValue() const { return m_revision; }
    QString revisionText() const { return QString::number(m_revision); }

    QVariantList patterns() const;
    QVariantList scenes() const;
    QString quantization() const { return Midi::quantizeName(m_content.quantize); }
    void setQuantization(const QString &name);
    QVariantList controllers() const { return Midi::descriptors(); }
    QVariantMap capabilities() const;
    bool editing() const { return m_gesture.has_value(); }
    bool canUndo() const { return m_cursor > 0; }
    bool canRedo() const { return m_cursor < m_history.size(); }
    QString undoText() const { return canUndo() ? m_history.at(m_cursor - 1).label : QString(); }
    QString redoText() const { return canRedo() ? m_history.at(m_cursor).label : QString(); }
    QString lastError() const { return m_lastError; }

    // ---- patterns (midi_sequence_ops.cpp) ----
    Q_INVOKABLE QVariantMap addPattern(const QString &name, const QString &lengthTicks = {});
    Q_INVOKABLE QVariantMap removePattern(const QString &patternId);
    // fields: name, length (decimal ticks), seed (0..2^32-1)
    Q_INVOKABLE QVariantMap setPattern(const QString &patternId, const QVariantMap &fields);
    // The committed pattern (never a drag preview).
    Q_INVOKABLE QVariantMap patternData(const QString &patternId) const;

    // ---- notes and controller events (midi_sequence_edit.cpp) ----
    // note: {tick, duration} or {startTicks, endTicks} (decimal), pitch,
    // channel (0..15), velocity (0..1) or velocity16 (1..65535)
    Q_INVOKABLE QVariantMap insertNote(const QString &patternId, const QVariantMap &note);
    Q_INVOKABLE QVariantMap setNote(const QString &patternId, const QString &noteId, const QVariantMap &fields);
    Q_INVOKABLE QVariantMap moveNotes(const QString &patternId, const QStringList &noteIds, const QString &deltaTicks,
                                      int deltaPitch = 0);
    Q_INVOKABLE QVariantMap deleteNotes(const QString &patternId, const QStringList &noteIds);
    // Moves starts (and with `ends`, ends) toward the grid by strength %;
    // an empty list quantizes every note.
    Q_INVOKABLE QVariantMap quantizeNotes(const QString &patternId, const QStringList &noteIds, const QString &gridTicks,
                                          int strengthPercent = 100, bool ends = false);
    // event: {id? (empty inserts), controller, channel, tick, value (0..1 at
    // the controller's resolution) or raw (decimal 32-bit), noteId (per-note)}
    Q_INVOKABLE QVariantMap setEvent(const QString &patternId, const QVariantMap &event);
    Q_INVOKABLE QVariantMap deleteEvents(const QString &patternId, const QStringList &eventIds);
    // Gestures keyed by the control's request ID: the first preview begins
    // one, finish records it as one step, cancel restores the committed state.
    Q_INVOKABLE QVariantMap previewNoteEdit(const QString &requestId, const QString &patternId, const QString &noteId,
                                            const QString &operation, const QString &targetTicks, int targetPitch);
    Q_INVOKABLE QVariantMap previewEventEdit(const QString &requestId, const QString &patternId,
                                             const QString &controllerId, const QString &eventId,
                                             const QString &ticks, double value);
    Q_INVOKABLE QVariantMap finishEdit(const QString &requestId);
    Q_INVOKABLE bool cancelEdit(const QString &requestId);

    // ---- step rows (midi_sequence_ops.cpp) ----
    // row: {name, pitch, channel, stepTicks (decimal), steps (count)}
    Q_INVOKABLE QVariantMap addRow(const QString &patternId, const QVariantMap &row);
    Q_INVOKABLE QVariantMap setRow(const QString &patternId, const QString &rowId, const QVariantMap &fields);
    Q_INVOKABLE QVariantMap removeRow(const QString &patternId, const QString &rowId);
    // field: enabled, velocity (0..1), gate (0..1], probability [0..1]
    Q_INVOKABLE QVariantMap setStep(const QString &patternId, const QString &rowId, const QString &stepId,
                                    const QString &field, const QVariant &value);

    // ---- scenes and launcher slots (midi_sequence_ops.cpp) ----
    Q_INVOKABLE QVariantMap addScene(const QString &name);
    Q_INVOKABLE QVariantMap removeScene(const QString &sceneId);
    Q_INVOKABLE QVariantMap setSlot(const QString &trackId, const QString &sceneId, const QString &patternId,
                                    const QString &label = {});
    Q_INVOKABLE QVariantMap clearSlot(const QString &slotId);

    // ---- snapshots for the controls (midi_sequence_model.cpp) ----
    // PianoRoll rows over [startTicks, endTicks): {id, pitch, channel,
    // velocity, x0, x1, startTicks, endTicks}; the drag preview when editing.
    Q_INVOKABLE QVariantList noteRows(const QString &patternId, const QString &startTicks, const QString &endTicks) const;
    Q_INVOKABLE QStringList tickBoundaries(const QString &startTicks, const QString &endTicks, const QString &gridTicks) const;
    // MidiControllerLane events: {id, ticks, value, raw, x, noteId}; the
    // "velocity" controller lists notes (id = note ID).
    Q_INVOKABLE QVariantList controllerEvents(const QString &patternId, const QString &controllerId,
                                              const QString &startTicks, const QString &endTicks) const;
    Q_INVOKABLE QVariantMap controllerDescriptor(const QString &controllerId) const { return Midi::descriptor(controllerId); }
    // StepSequencer rows: {id, name, pitch, channel, stepTicks, steps: [{id, enabled, velocity, gate, probability}]}
    Q_INVOKABLE QVariantList stepRows(const QString &patternId) const;

    // ---- history and persistence (midi_sequence_model.cpp) ----
    Q_INVOKABLE bool undo();
    Q_INVOKABLE bool redo();
    Q_INVOKABLE void clearHistory();
    Q_INVOKABLE QVariantMap contentMap() const;
    Q_INVOKABLE QVariantMap loadContent(const QVariantMap &content);
    Q_INVOKABLE QString toJson() const;
    Q_INVOKABLE QVariantMap loadJson(const QString &json);

    // ---- Standard MIDI Files (midi_smf.cpp) ----
    // tempo: TempoEvent maps {kind, tick, microsecondsPerQuarter, numerator, denominator}.
    Q_INVOKABLE QVariantMap exportMidiFile(const QString &patternId, const QString &file,
                                           const QVariantList &tempo = {}) const;
    // One pattern per track holding channel events; one history step.
    Q_INVOKABLE QVariantMap importMidiFile(const QString &file);

signals:
    void ticksPerQuarterChanged();
    void contentChanged();
    void previewChanged();
    void historyChanged();
    void lastErrorChanged();
    void committed(const QString &label);

private:
    struct Step { QString label; MidiContent before, after; };
    struct Gesture { QString requestId; QUuid patternId; MidiPattern base; MidiPattern preview; QString label; };
    // Applies `edit` to a copy of one pattern and commits it as one step.
    QVariantMap editPattern(const QString &patternId, const QString &label,
                            const std::function<QString(MidiPattern &)> &edit, const QVariantMap &extra = {});
    QVariantMap editContent(const QString &label, const std::function<QString(MidiContent &)> &edit,
                            const QVariantMap &extra = {});
    const MidiPattern *viewPattern(const QUuid &id) const; // the preview while a gesture edits it
    bool beginGesture(const QString &requestId, const QUuid &patternId, const QString &label, QString *error);
    QVariantMap previewGesture(const std::function<QString(MidiPattern &)> &edit);
    void publish();
    QVariantMap ok(const QVariantMap &extra = {});
    QVariantMap failure(const QString &message);
    void clearError();

    MidiContent m_content;
    QVector<Step> m_history;
    qsizetype m_cursor = 0;
    std::optional<Gesture> m_gesture;
    quint64 m_revision = 0;
    int m_ppq = 960;
    QString m_lastError;
};

namespace MidiEdit {
// Shared by the model's operations (midi_sequence_edit.cpp).
QString moveNote(MidiPattern &pattern, const QUuid &noteId, qint64 tick, int pitch);
QString resizeNote(MidiPattern &pattern, const QUuid &noteId, qint64 end);
qint64 roundDiv(qint64 numerator, qint64 denominator); // NearestAway
} // namespace MidiEdit

} // namespace QindaTK
