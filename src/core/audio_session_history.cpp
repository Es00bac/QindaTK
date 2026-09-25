// SPDX-License-Identifier: LGPL-3.0-or-later
#include "audio_session_history.h"

#include "audio_mixer_model.h"
#include "automation_model.h"
#include "timeline_model.h"

#include <algorithm>

namespace QindaTK {

AudioSessionHistory::AudioSessionHistory(TimelineModel *timeline, AutomationModel *automation,
                                         AudioMixerModel *mixer, QObject *parent)
    : QObject(parent), m_timeline(timeline), m_automation(automation), m_mixer(mixer)
{
    connect(timeline, &TimelineModel::historyChanged, this, [this] { observe(Timeline); });
    connect(automation, &AutomationModel::historyChanged, this, [this] { observe(Automation); });
    connect(mixer, &AudioMixerModel::historyChanged, this, [this] { observe(Mixer); });
    for (int owner = 0; owner < OwnerCount; ++owner) m_state[owner] = stateOf(Owner(owner));
}

AudioSessionHistory::State AudioSessionHistory::stateOf(Owner owner) const
{
    switch (owner) {
    case Timeline:
        return m_timeline ? State{m_timeline->undoDepth(), m_timeline->redoDepth(), m_timeline->historySerial()} : State{};
    case Automation:
        return m_automation ? State{m_automation->undoDepth(), m_automation->redoDepth(), m_automation->historySerial()}
                            : State{};
    case Mixer:
        return m_mixer ? State{m_mixer->undoDepth(), m_mixer->redoDepth(), m_mixer->historySerial()} : State{};
    case OwnerCount:
        break;
    }
    return {};
}

QString AudioSessionHistory::name(Owner owner)
{
    return owner == Timeline ? QStringLiteral("timeline") : owner == Automation ? QStringLiteral("automation")
                                                                                : QStringLiteral("mixer");
}

bool AudioSessionHistory::step(Owner owner, bool undo)
{
    switch (owner) {
    case Timeline:
        return m_timeline && (undo ? m_timeline->undo() : m_timeline->redo());
    case Automation:
        return m_automation && (undo ? m_automation->undo() : m_automation->redo());
    case Mixer:
        return m_mixer && (undo ? m_mixer->undo() : m_mixer->redo());
    case OwnerCount:
        break;
    }
    return false;
}

QString AudioSessionHistory::text(Owner owner, bool undo) const
{
    switch (owner) {
    case Timeline:
        return !m_timeline ? QString() : undo ? m_timeline->undoText() : m_timeline->redoText();
    case Automation:
        return !m_automation ? QString() : undo ? m_automation->undoText() : m_automation->redoText();
    case Mixer:
        return !m_mixer ? QString() : undo ? m_mixer->undoText() : m_mixer->redoText();
    case OwnerCount:
        break;
    }
    return {};
}

int AudioSessionHistory::newest(const QVector<Owner> &journal, bool undo) const
{
    // An entry is stale when its model dropped that step (a history limit
    // trims the oldest); the newest entry of a model maps to its next step.
    for (int i = int(journal.size()) - 1; i >= 0; --i) {
        const State state = stateOf(journal.at(i));
        if (undo ? state.undo > 0 : state.redo > 0) return i;
    }
    return -1;
}

QString AudioSessionHistory::undoText() const
{
    const int i = newest(m_undo, true);
    return i < 0 ? QString() : text(m_undo.at(i), true);
}

QString AudioSessionHistory::redoText() const
{
    const int i = newest(m_redo, false);
    return i < 0 ? QString() : text(m_redo.at(i), false);
}

QString AudioSessionHistory::undoOwner() const
{
    const int i = newest(m_undo, true);
    return i < 0 ? QString() : name(m_undo.at(i));
}

QString AudioSessionHistory::redoOwner() const
{
    const int i = newest(m_redo, false);
    return i < 0 ? QString() : name(m_redo.at(i));
}

bool AudioSessionHistory::undo()
{
    const int i = newest(m_undo, true);
    if (i < 0) return false;
    m_undo.resize(i + 1); // entries above it were stale
    return step(m_undo.at(i), true); // observe() moves the entry to m_redo
}

bool AudioSessionHistory::redo()
{
    const int i = newest(m_redo, false);
    if (i < 0) return false;
    m_redo.resize(i + 1);
    return step(m_redo.at(i), false);
}

void AudioSessionHistory::move(Owner owner, int steps, QVector<Owner> &from, QVector<Owner> &to)
{
    for (int n = 0; n < steps; ++n) {
        const qsizetype at = from.lastIndexOf(owner);
        if (at < 0) return;
        from.removeAt(at);
        to.push_back(owner);
    }
}

void AudioSessionHistory::observe(Owner owner)
{
    State &last = m_state[owner];
    const State now = stateOf(owner);
    if (now.serial > last.serial) {
        // New steps: one entry each; a new step ends every redo (one stack).
        const quint64 added = std::min<quint64>(now.serial - last.serial, kLimit);
        for (quint64 n = 0; n < added; ++n) m_undo.push_back(owner);
        if (m_undo.size() > kLimit) m_undo.remove(0, m_undo.size() - kLimit);
        m_redo.clear();
    } else if (now.undo + now.redo == last.undo + last.redo && now.undo != last.undo) {
        // Undo or redo on the model (through here or directly).
        if (now.undo < last.undo) move(owner, last.undo - now.undo, m_undo, m_redo);
        else move(owner, now.undo - last.undo, m_redo, m_undo);
    } else if (now.undo == 0 && now.redo == 0) {
        // Loaded or cleared: nothing of this model can be reached any more.
        m_undo.removeAll(owner);
        m_redo.removeAll(owner);
    }
    last = now;
    emit changed();
}

} // namespace QindaTK
