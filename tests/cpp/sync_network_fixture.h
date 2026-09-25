// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
// Shared by the WP-082 network tests: an authoritative hub on a TCP port and
// peers that each hold their own replica, durable local log and bridge,
// exactly as separate machines would.
#include "property_sync_socket.h"
#include "reference_document_codec.h"
#include <QTemporaryDir>
#include <memory>

namespace sync_test {
using namespace QindaTK;

constexpr auto ActorA = "00000000-0000-0000-0000-00000000000a";
constexpr auto ActorB = "00000000-0000-0000-0000-00000000000b";
constexpr auto ActorC = "00000000-0000-0000-0000-00000000000c";
constexpr auto CredentialA = "network-credential-aaaa";
constexpr auto CredentialB = "network-credential-bbbb";
constexpr auto CredentialC = "network-credential-cccc";

inline QString gain() { return QStringLiteral("gain"); }
inline QString name() { return QStringLiteral("name"); }

inline PropertyDescriptor describe(const QString &id, QMetaType type, const QVariant &defaultValue)
{
    PropertyDescriptor descriptor;
    descriptor.id = id;
    descriptor.type = type;
    descriptor.defaultValue = defaultValue;
    return descriptor;
}

struct Peer {
    std::unique_ptr<PropertyDocument> document;
    LocalPropertySyncService local;
    LocalPropertySyncClient client;
    PropertySyncSocketClient socket;
};

struct Network {
    QTemporaryDir directory;
    EntityId entity = EntityId::create();
    QByteArray baseline;
    QString documentId;
    LocalPropertySyncService service;
    PropertySyncSocketServer server;

    bool start()
    {
        PropertyDocument base;
        if (!directory.isValid()
            || !base.defineProperty(describe(gain(), QMetaType::fromType<double>(), 0.0))
            || !base.defineProperty(describe(name(), QMetaType::fromType<QString>(),
                                             QStringLiteral("base")))
            || !base.addEntity(entity))
            return false;
        const auto snapshot = ReferenceDocumentCodec::capture(base);
        if (!snapshot) return false;
        baseline = snapshot->payload;
        documentId = base.documentIdString();
        service.setDocumentId(documentId);
        service.setStoragePath(directory.filePath(QStringLiteral("server.json")));
        // A headless hub: it folds its log through a scratch replica (D-178).
        service.setCompactionBaseline(baseline, {});
        server.setService(&service);
        return service.open()
            && server.grant(QString::fromLatin1(ActorA), QString::fromLatin1(CredentialA), true)
            && server.grant(QString::fromLatin1(ActorB), QString::fromLatin1(CredentialB), true)
            && server.listenNetwork(QHostAddress::LocalHost);
    }

    std::unique_ptr<PropertyDocument> replica() const
    {
        auto decoded = ReferenceDocumentCodec::decode(baseline);
        return decoded ? std::move(decoded->document) : nullptr;
    }

    std::unique_ptr<Peer> peer(const char *actor, const char *credential)
    {
        auto result = std::make_unique<Peer>();
        result->document = replica();
        if (!result->document) return nullptr;
        result->local.setDocumentId(documentId);
        result->local.setStoragePath(
            directory.filePath(QString::fromLatin1(actor) + QStringLiteral(".json")));
        if (!result->local.open()) return nullptr;
        result->client.setDocument(result->document.get());
        result->client.setService(&result->local);
        result->client.setActorId(QString::fromLatin1(actor));
        if (!result->client.connectToService()) return nullptr;
        result->socket.setService(&result->local);
        result->socket.setActorId(QString::fromLatin1(actor));
        result->socket.setCredential(QString::fromLatin1(credential));
        result->socket.setHost(QStringLiteral("127.0.0.1"), server.networkPort());
        result->socket.setLocalClient(&result->client);
        return result;
    }

    double valueOf(const Peer &peer) const
    {
        return peer.document->value(entity, gain()).toDouble();
    }
};

inline QStringList dump(const PropertyDocument &document)
{
    QStringList rows;
    for (const auto &change : document.committedValues())
        rows.append(change.target.toString() + QLatin1Char('/') + change.propertyId
                    + QLatin1Char('=') + change.after.toString());
    return rows;
}
} // namespace sync_test
