// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include "protocol_client.h"
namespace QindaTK::SieveJobs {
struct Job {
    Protocol::Endpoint endpoint;
    Protocol::Credentials credentials;
    QString command, name, newName;
    QByteArray script;
};
QVariantMap run(const Job &job);
QString validate(const Job &job);
}
