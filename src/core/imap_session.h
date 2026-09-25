// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "protocol_client.h"

#include <QByteArray>
#include <QList>
#include <QStringList>

// IMAP4rev1 client subset (RFC 3501 with SASL-IR, UIDPLUS, MOVE and
// SPECIAL-USE when offered) used by ImapMailProvider on its worker thread.
// Mailbox names cross this boundary as UTF-8 display names and go on the wire
// in modified UTF-7 (imap_mutf7.h). Internal to the library.
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
QByteArray quoted(const QByteArray &bytes); // quoted string of raw wire bytes
// Quoted modified-UTF-7 wire form of a UTF-8 display mailbox name.
QByteArray mailboxArgument(const QString &name);

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
    // a "+" server request (an empty answer is sent when none is given);
    // `continuationShown` replaces it in the transcript (default "<redacted>").
    bool command(const QByteArray &command, QList<QByteArray> *untagged = nullptr,
                 const QString &shown = {}, const QByteArray &continuation = {},
                 const QString &continuationShown = {});
    // Like command(), but a tagged NO/BAD returns false without failing the
    // exchange (per-mailbox STATUS); I/O failures still fail the channel.
    bool optionalCommand(const QByteArray &command, QList<QByteArray> *untagged = nullptr);
    // `mailbox` is the display name; it is sent in modified UTF-7.
    bool select(const QString &mailbox, qint64 *uidValidity, qint64 *exists);
    void logout();
    bool hasCapability(const char *name) const;
    bool failed() const { return m_channel->failure() != Protocol::Failure::None; }
    QString lastStatus() const { return m_lastStatus; }

private:
    bool readResponse(QByteArray *raw);
    bool exchange(const QByteArray &command, QList<QByteArray> *untagged, const QString &shown,
                  const QByteArray &continuation, const QString &continuationShown, bool required);
    bool capability();
    bool authenticate(const Protocol::Credentials &credentials);

    Protocol::Channel *m_channel;
    QStringList m_capabilities;
    QString m_lastStatus;
    int m_tag = 0;
};

} // namespace QindaTK::Imap
