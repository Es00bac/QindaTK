// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QList>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace QindaTK::ProgramStatus {

// AGENT-CONTRACT (OSC 7501, Program Status Protocol revision 0.4): the program
// side is any agent CLI or build tool; the terminal side is TerminalSession.
// A report that breaks a limit, carries text that does not decode, or names
// an unknown state is discarded whole, so a later revision's state is never
// shown as "idle". docs/terminal-session.md lists the limits.
struct Report {
    enum Type { Discard, Query, Clear, Status };
    Type type = Discard;
    QByteArray id;    // empty addresses the root record
    QByteArray state; // idle, working, done, blocked or error
    QByteArray kind;  // permission, question or auth; only with blocked
    QByteArray app;
    int progress = -1; // 0..100, or -1 when indeterminate
    QString title;
    QString message;
};

// `body` is the text between "OSC 7501 ;" and the terminator; `sequenceBytes`
// is the length of the whole sequence, which has its own limit.
Report parse(QByteArrayView body, qsizetype sequenceBytes);

// The reply to "OSC 7501 ; ?", without introducer or terminator.
QByteArray queryReply();

class Store {
public:
    // Each call returns whether the visible set of records changed.
    bool apply(const Report &report);
    // Process exit or a new shell prompt ends working and blocked records.
    bool dropTransient(bool dropIdle);
    // The user has seen the finished work: done and error records go.
    bool acknowledge();
    bool clear();

    // The record a single indicator should show: blocked, then error, working,
    // done, idle; the most recently updated record wins a tie.
    QVariantMap summary() const;
    QVariantList records() const;
    qsizetype size() const { return m_records.size(); }

    static constexpr qsizetype capacity = 256;

private:
    struct Record {
        QByteArray id, state, kind, app;
        int progress = -1;
        QString title, message;
        quint64 updated = 0;
    };
    QByteArray effectiveApp(const Record &record) const;
    QVariantMap toMap(const Record &record) const;

    QList<Record> m_records;
    quint64 m_clock = 0;
};

} // namespace QindaTK::ProgramStatus
