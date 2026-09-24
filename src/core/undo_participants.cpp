// SPDX-License-Identifier: LGPL-3.0-or-later
#include "undo_service.h"

#include <QMetaMethod>
#include <QThread>

// History participants (docs/editing.md#history-participants): a document
// model with its own history joins UndoService routing.
namespace QindaTK {

UndoParticipant *UndoService::participant() const
{
    return m_participantObject ? dynamic_cast<UndoParticipant *>(m_participantObject.data()) : nullptr;
}

void UndoService::setActiveParticipant(QObject *participant)
{
    Q_ASSERT(thread() == QThread::currentThread());
    if (participant && !dynamic_cast<UndoParticipant *>(participant)) {
        fail(QStringLiteral("Not an undo participant"));
        return;
    }
    if (m_participantObject == participant) return;
    disconnect(m_participantConnection);
    m_participantObject = participant;
    if (participant) {
        // AGENT-CONTRACT: participants notify through historyChanged().
        const int index = participant->metaObject()->indexOfSignal("historyChanged()");
        if (index >= 0)
            m_participantConnection = connect(participant, participant->metaObject()->method(index), this,
                                              metaObject()->method(metaObject()->indexOfSignal("changed()")));
    }
    emit changed();
}

} // namespace QindaTK
