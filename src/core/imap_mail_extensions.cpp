// SPDX-License-Identifier: LGPL-3.0-or-later
#include "imap_mail_provider.h"

namespace QindaTK {
bool ImapMailProvider::updateFlags(const QString &requestId, const QString &messageId,
                                    const QStringList &add, const QStringList &remove)
{
    return start(Operation::Flags, requestId, {{QStringLiteral("messageId"), messageId},
        {QStringLiteral("addFlags"), add}, {QStringLiteral("removeFlags"), remove}});
}

bool ImapMailProvider::setCategories(const QString &requestId, const QString &messageId,
                                      const QStringList &categories)
{
    return start(Operation::Flags, requestId, {{QStringLiteral("messageId"), messageId},
        {QStringLiteral("categories"), categories}});
}

bool ImapMailProvider::fetchHeaders(const QString &requestId, const QString &messageId, const QStringList &fields)
{
    return start(Operation::Headers, requestId, {{QStringLiteral("messageId"), messageId},
        {QStringLiteral("fields"), fields}});
}

bool ImapMailProvider::copyMessage(const QString &requestId, const QString &messageId, const QString &targetMailbox)
{
    return start(Operation::Action, requestId, {{QStringLiteral("messageId"), messageId},
        {QStringLiteral("action"), QStringLiteral("copy")}, {QStringLiteral("targetMailbox"), targetMailbox}});
}
} // namespace QindaTK
