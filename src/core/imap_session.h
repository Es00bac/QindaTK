// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "protocol_client.h"

#include <QByteArray>
#include <QList>
#include <QStringList>

// IMAP4rev1 client subset (RFC 3501 with SASL-IR, UIDPLUS, MOVE when offered)
// used by ImapMailProvider on its worker thread. Internal to the library.
namespace QindaTK::Imap {

struct Value {
    enum Kind { Atom, String, List, Nil };
    Kind kind = Nil;
    QByteArray text;              // atom or string bytes (literals included)
    QList<Value> items;           // List children

    bool isAtom(const char *name) const { return kind == Atom && text.compare(name, Qt::CaseInsensitive) == 0; }
};

// Parses one untagged response ("* ..." without the star) into values. Handles
// quoted strings, {n} literals as they arrive on the wire, parenthesized lists
// and section atoms such as BODY[HEADER.FIELDS (SUBJECT)].
QList<Value> parseResponse(const QByteArray &raw);
QByteArray quoted(const QString &text);     // IMAP quoted string

struct FetchItem {
    qint64 sequence = 0;
    QList<Value> attributes;      // alternating name/value
    const Value *find(const char *prefix) const;
};
QList<FetchItem> fetchItems(const QList<QByteArray> &untagged);

class Session {
public:
    explicit Session(Protocol::Channel *channel) : m_channel(channel) {}

    // Greeting, CAPABILITY, STARTTLS when requested, authentication.
    bool open(const Protocol::Endpoint &endpoint, const Protocol::Credentials &credentials);
    // Sends "<tag> <command>"; collects untagged responses; true on tagged OK.
    // `shown` replaces the command in the transcript; `continuation` answers
    // a "+" server request (an empty answer is sent when none is given).
    bool command(const QByteArray &command, QList<QByteArray> *untagged = nullptr,
                 const QString &shown = {}, const QByteArray &continuation = {});
    bool select(const QString &mailbox, qint64 *uidValidity, qint64 *exists);
    void logout();
    bool hasCapability(const char *name) const;
    QString lastStatus() const { return m_lastStatus; }

private:
    bool readResponse(QByteArray *raw);
    bool capability();
    bool authenticate(const Protocol::Credentials &credentials);

    Protocol::Channel *m_channel;
    QStringList m_capabilities;
    QString m_lastStatus;
    int m_tag = 0;
};

} // namespace QindaTK::Imap
