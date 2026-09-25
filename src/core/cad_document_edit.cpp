// SPDX-License-Identifier: LGPL-3.0-or-later
#include "cad_document.h"

#include <utility>

namespace QindaTK {

// Removing a relation never needs a solve: the remaining constraints were
// already satisfied, so geometry stays exactly where it is.
bool CadDocument::removeConstraint(const QString &constraintId)
{
    const int index = constraintIndex(m_state, constraintId);
    if (index < 0) { setError(QStringLiteral("Unknown CAD constraint")); return false; }
    rememberEdit();
    m_state.constraints.removeAt(index);
    setError({}); emit changed();
    return true;
}

// AGENT-NOTE (D-283): a transaction groups the edits of one view gesture
// (two points plus the segment between them) into one undo step. Nested
// begin/commit pairs join the outer transaction. Undo and redo are refused
// while one is open, and loadFromFile discards it with the old history.
bool CadDocument::beginTransaction()
{
    if (m_transactionDepth == 0) {
        m_transactionRecorded = false;
        m_transactionBase = m_state;
        m_transactionUndo = m_undo;
        m_transactionRedo = m_redo;
    }
    ++m_transactionDepth;
    return true;
}

bool CadDocument::commitTransaction()
{
    if (m_transactionDepth == 0) return false;
    if (--m_transactionDepth == 0) {
        m_transactionRecorded = false;
        m_transactionBase = {};
        m_transactionUndo.clear();
        m_transactionRedo.clear();
    }
    return true;
}

// Keeps lastError: the view reports why the gesture was abandoned.
bool CadDocument::cancelTransaction()
{
    if (m_transactionDepth == 0) return false;
    m_transactionDepth = 0;
    const bool recorded = m_transactionRecorded;
    m_transactionRecorded = false;
    if (recorded) {
        m_state = std::move(m_transactionBase);
        m_undo = std::move(m_transactionUndo);
        m_redo = std::move(m_transactionRedo);
        ++m_revision;
    }
    m_transactionBase = {};
    m_transactionUndo.clear();
    m_transactionRedo.clear();
    if (recorded) emit changed();
    return true;
}

} // namespace QindaTK
