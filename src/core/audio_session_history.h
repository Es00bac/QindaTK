// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QObject>
#include <QPointer>
#include <QVector>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

class AudioMixerModel;
class AutomationModel;
class TimelineModel;

// AGENT-CONTRACT (WP-062, D-110, docs/audio-mixer.md): one Undo and one Redo
// over the three histories a mixer edit can land in: the timeline (track
// mute, solo, arm, name and every clip edit), the automation (gain, pan,
// width and send levels) and the mixer (buses, routing, sends, groups, bus
// and master mute or solo). Each model keeps and applies its own steps; this
// journal keeps only which model recorded each step, in the order they were
// recorded, so undo() always reverses the newest step of the session.
// A step undone or redone on its model directly moves here too, a new step
// ends every redo (one stack), and a model that loads or clears its history
// drops its entries. A timeline in externalUndo mode records nothing here.
class AudioSessionHistory : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("AudioSessionHistory belongs to an AudioSession (session.history)")
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY changed)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY changed)
    Q_PROPERTY(QString undoText READ undoText NOTIFY changed)
    Q_PROPERTY(QString redoText READ redoText NOTIFY changed)
    // "timeline", "automation", "mixer" or "".
    Q_PROPERTY(QString undoOwner READ undoOwner NOTIFY changed)
    Q_PROPERTY(QString redoOwner READ redoOwner NOTIFY changed)
    Q_PROPERTY(int count READ count NOTIFY changed)
public:
    static constexpr int kLimit = 4096; // journal entries; the models hold fewer steps

    AudioSessionHistory(TimelineModel *timeline, AutomationModel *automation, AudioMixerModel *mixer,
                        QObject *parent);

    bool canUndo() const { return newest(m_undo, true) >= 0; }
    bool canRedo() const { return newest(m_redo, false) >= 0; }
    QString undoText() const;
    QString redoText() const;
    QString undoOwner() const;
    QString redoOwner() const;
    int count() const { return int(m_undo.size()); }

    Q_INVOKABLE bool undo();
    Q_INVOKABLE bool redo();

signals:
    void changed();

private:
    enum Owner { Timeline, Automation, Mixer, OwnerCount };
    struct State {
        int undo = 0;
        int redo = 0;
        quint64 serial = 0;
    };
    State stateOf(Owner owner) const;
    bool step(Owner owner, bool undo);
    QString text(Owner owner, bool undo) const;
    static QString name(Owner owner);
    // Index of the newest entry whose model can still take that step, or -1.
    int newest(const QVector<Owner> &journal, bool undo) const;
    void observe(Owner owner);
    void move(Owner owner, int steps, QVector<Owner> &from, QVector<Owner> &to);

    QPointer<TimelineModel> m_timeline;
    QPointer<AutomationModel> m_automation;
    QPointer<AudioMixerModel> m_mixer;
    QVector<Owner> m_undo; // oldest first
    QVector<Owner> m_redo; // the next redo last
    State m_state[OwnerCount];
};

} // namespace QindaTK
