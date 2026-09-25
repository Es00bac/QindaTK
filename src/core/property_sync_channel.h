// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <optional>

class QIODevice;

namespace QindaTK {

// Authenticated frame channel of the property-sync protocol (version 2),
// shared by the owner-only local socket and the local-network TCP transport.
//
// AGENT-CONTRACT (docs/collaboration.md "Wire protocol"; both sides in
// property_sync_socket.cpp and property_sync_socket_client.cpp): the server
// opens with a plain {"type":"challenge","nonce"} frame; the client answers
// with a plain hello whose proof is HMAC-SHA256(credential, hello context).
// Both then derive a per-connection session key from the two nonces and send
// only sealed frames {"seq","body","mac"}: body is compact JSON text, seq
// counts up from 1 per direction and mac covers direction, seq and body.
// A tampered, replayed, reordered or dropped frame fails open() and the
// connection closes; operation replay on reconnect then restores state.
// The credential never crosses the wire. Frames are not encrypted (D-168).
class PropertySyncChannel {
public:
    enum class Role { Server, Client };
    static constexpr qsizetype MaxBodyBytes = 96 * 1024;
    static constexpr qsizetype MaxFrameBytes = 128 * 1024;

    static QByteArray createNonce();
    static QByteArray helloProof(const QString &credential, const QString &documentId,
                                 const QString &actorId, const QByteArray &serverNonce,
                                 const QByteArray &clientNonce);
    static bool sameDigest(const QByteArray &a, const QByteArray &b);
    static bool validNonce(const QByteArray &nonce);

    void begin(Role role, const QString &credential, const QByteArray &serverNonce,
               const QByteArray &clientNonce);
    void reset();
    bool isOpen() const { return !m_key.isEmpty(); }
    // Empty object when the body is too large or the channel is closed.
    QJsonObject seal(const QJsonObject &body);
    std::optional<QJsonObject> open(const QJsonObject &frame);

private:
    QByteArray mac(char direction, quint64 sequence, const QByteArray &body) const;
    QByteArray m_key;
    Role m_role = Role::Client;
    quint64 m_sent = 0;
    quint64 m_received = 0;
};

// Newline-delimited compact JSON framing over a stream device.
bool writeSyncFrame(QIODevice *device, const QJsonObject &frame);
// nullopt: no complete frame buffered; an empty object: an invalid frame.
std::optional<QJsonObject> nextSyncFrame(QByteArray &buffer);

} // namespace QindaTK
