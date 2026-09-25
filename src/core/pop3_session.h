// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "protocol_client.h"

#include <QByteArray>
#include <QList>
#include <QStringList>

// POP3 client subset used by Pop3MailProvider on its worker thread (RFC 1939
// USER/PASS, UIDL, LIST, RETR, TOP, DELE, RSET, QUIT; RFC 2449 CAPA and
// response codes; RFC 2595 STLS; RFC 5034 SASL AUTH with PLAIN and XOAUTH2).
// STAT and APOP are not used: UIDL + LIST carry everything sync needs, and
// APOP's MD5 digest is no substitute for TLS. Internal to the library.
// AGENT-NOTE: message numbers are only valid inside one session; everything
// that crosses a session (the store, the provider's jobs) names a message by
// its UIDL. Deletions take effect only when QUIT succeeds (RFC 1939 §6, the
// UPDATE state), so a failed session never removes anything.
namespace QindaTK::Pop3 {

struct Entry {
    qint64 number = 0;            // message number in this session
    QByteArray uidl;              // RFC 1939 unique-id: 1..70 chars 0x21..0x7E
    qint64 size = 0;              // LIST octets (with CRLF, before unstuffing)
};

// True when `uidl` is a valid RFC 1939 unique-id listing.
bool validUidl(const QByteArray &uidl);

class Session {
public:
    explicit Session(Protocol::Channel *channel) : m_channel(channel) {}

    // Greeting, CAPA, STLS when requested, then authentication.
    bool open(const Protocol::Endpoint &endpoint, const Protocol::Credentials &credentials);
    // Single-line command; true on "+OK". `shown` replaces the command in
    // the transcript (credentials).
    bool command(const QByteArray &command, QByteArray *reply = nullptr, const QString &shown = {});
    // Multi-line command: "+OK", then lines up to the "." terminator, dot-
    // unstuffed and CRLF-joined into `body` (the transcript shows only the
    // byte count). A body over `limit` bytes fails the session. When
    // `refused` is given, a "-ERR" reply sets it and returns false without
    // failing the channel (optional commands such as CAPA and TOP).
    bool multiline(const QByteArray &command, QByteArray *body, qint64 limit, bool *refused = nullptr);
    // UIDL and LIST merged by message number, in maildrop order.
    bool listing(QList<Entry> *entries);
    bool retrieve(qint64 number, QByteArray *source, qint64 limit);
    // Headers only (TOP n 0); false without failing when the server has no TOP.
    bool headers(qint64 number, QByteArray *source);
    // DELE; a refused DELE sends RSET so nothing of this session is marked.
    bool remove(qint64 number);
    // Ends the session; on "+OK" every DELE of this session is committed.
    bool quit();
    bool hasCapability(const char *name) const;
    bool offersSasl(const char *mechanism) const;
    // A "-ERR [IN-USE]" or "[SYS/TEMP]" reply (RFC 2449/3206): worth retrying.
    bool transient() const { return m_transient; }

private:
    // 1 on "+OK", 0 on "-ERR" (not yet a failure), -1 on a channel failure.
    int status(QByteArray *reply);
    bool refuse(const QByteArray &verb, const QByteArray &reply);
    bool body(QByteArray *out, qint64 limit);
    bool capa();
    bool authenticate(const Protocol::Credentials &credentials);
    bool sasl(const QByteArray &mechanism, const QByteArray &payload);

    Protocol::Channel *m_channel;
    QList<QByteArrayList> m_capabilities;   // upper-case CAPA lines, split on spaces
    bool m_transient = false;
};

} // namespace QindaTK::Pop3
