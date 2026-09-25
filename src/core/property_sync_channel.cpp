// SPDX-License-Identifier: LGPL-3.0-or-later
#include "property_sync_channel.h"
#include <QIODevice>
#include <QJsonDocument>
#include <QMessageAuthenticationCode>
#include <QRandomGenerator>

namespace QindaTK {
namespace {
constexpr qsizetype MaxQueuedBytes = 8 * 1024 * 1024;
constexpr qsizetype NonceBytes = 32;

QByteArray hmac(const QByteArray &key, const QByteArray &message)
{
    return QMessageAuthenticationCode::hash(message, key, QCryptographicHash::Sha256);
}
}

QByteArray PropertySyncChannel::createNonce()
{
    QByteArray bytes(NonceBytes, Qt::Uninitialized);
    auto *generator = QRandomGenerator::system();
    for (qsizetype i = 0; i < bytes.size(); ++i)
        bytes[i] = char(generator->bounded(256));
    return bytes.toHex();
}

bool PropertySyncChannel::validNonce(const QByteArray &nonce)
{
    return nonce.size() == NonceBytes * 2
        && QByteArray::fromHex(nonce).toHex() == nonce;
}

QByteArray PropertySyncChannel::helloProof(const QString &credential, const QString &documentId,
                                           const QString &actorId, const QByteArray &serverNonce,
                                           const QByteArray &clientNonce)
{
    const QByteArray context = "qtk-sync-v2|hello|" + documentId.toUtf8() + '|'
        + actorId.toUtf8() + '|' + serverNonce + '|' + clientNonce;
    return hmac(credential.toUtf8(), context).toHex();
}

bool PropertySyncChannel::sameDigest(const QByteArray &a, const QByteArray &b)
{
    if (a.size() != b.size() || a.isEmpty()) return false;
    unsigned char difference = 0;
    for (qsizetype i = 0; i < a.size(); ++i)
        difference |= static_cast<unsigned char>(a.at(i) ^ b.at(i));
    return difference == 0;
}

void PropertySyncChannel::begin(Role role, const QString &credential,
                                const QByteArray &serverNonce, const QByteArray &clientNonce)
{
    m_role = role;
    m_key = hmac(credential.toUtf8(),
                 "qtk-sync-v2|session|" + serverNonce + '|' + clientNonce);
    m_sent = 0;
    m_received = 0;
}

void PropertySyncChannel::reset()
{
    m_key.clear();
    m_sent = 0;
    m_received = 0;
}

QByteArray PropertySyncChannel::mac(char direction, quint64 sequence,
                                    const QByteArray &body) const
{
    return hmac(m_key, QByteArray(1, direction) + '|' + QByteArray::number(sequence) + '|'
                           + body).toHex();
}

QJsonObject PropertySyncChannel::seal(const QJsonObject &body)
{
    const QByteArray text = QJsonDocument(body).toJson(QJsonDocument::Compact);
    if (!isOpen() || text.size() > MaxBodyBytes) return {};
    const quint64 sequence = ++m_sent;
    const char direction = m_role == Role::Server ? 's' : 'c';
    return {{QStringLiteral("seq"), QString::number(sequence)},
            {QStringLiteral("body"), QString::fromUtf8(text)},
            {QStringLiteral("mac"), QString::fromLatin1(mac(direction, sequence, text))}};
}

std::optional<QJsonObject> PropertySyncChannel::open(const QJsonObject &frame)
{
    if (!isOpen() || frame.size() != 3 || !frame.value(QStringLiteral("body")).isString()
        || !frame.value(QStringLiteral("seq")).isString()
        || !frame.value(QStringLiteral("mac")).isString())
        return std::nullopt;
    const QString sequenceText = frame.value(QStringLiteral("seq")).toString();
    bool ok = false;
    const quint64 sequence = sequenceText.toULongLong(&ok);
    // AGENT-GUARD: exactly the next sequence number. Accepting a gap or an
    // old number would let a network peer drop, reorder or replay frames.
    if (!ok || sequence != m_received + 1 || QString::number(sequence) != sequenceText)
        return std::nullopt;
    const QByteArray text = frame.value(QStringLiteral("body")).toString().toUtf8();
    const char direction = m_role == Role::Server ? 'c' : 's';
    if (text.size() > MaxBodyBytes
        || !sameDigest(mac(direction, sequence, text),
                       frame.value(QStringLiteral("mac")).toString().toLatin1()))
        return std::nullopt;
    const auto document = QJsonDocument::fromJson(text);
    if (!document.isObject()) return std::nullopt;
    m_received = sequence;
    return document.object();
}

bool writeSyncFrame(QIODevice *device, const QJsonObject &frame)
{
    if (!device || frame.isEmpty()) return false;
    QByteArray bytes = QJsonDocument(frame).toJson(QJsonDocument::Compact);
    if (bytes.size() > PropertySyncChannel::MaxFrameBytes
        || device->bytesToWrite() + bytes.size() > MaxQueuedBytes)
        return false;
    bytes.append('\n');
    return device->write(bytes) == bytes.size();
}

std::optional<QJsonObject> nextSyncFrame(QByteArray &buffer)
{
    const auto end = buffer.indexOf('\n');
    if (end < 0) return std::nullopt;
    const QByteArray bytes = buffer.left(end);
    buffer.remove(0, end + 1);
    if (bytes.size() > PropertySyncChannel::MaxFrameBytes) return QJsonObject{};
    const auto document = QJsonDocument::fromJson(bytes);
    return document.isObject() ? document.object() : QJsonObject{};
}

} // namespace QindaTK
