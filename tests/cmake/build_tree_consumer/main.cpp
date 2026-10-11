// SPDX-License-Identifier: LGPL-3.0-or-later
#include <qindatk/imap_mail_provider.h>
#include <qindatk/calendar_mail.h>
#include <qindatk/manage_sieve_client.h>
#include <qindatk/local_report_spec.h>
#include <qindatk/agent_host.h>
#include <QCoreApplication>
int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    QindaTK::ImapMailProvider mail;
    QindaTK::ManageSieveClient sieve;
    QindaTK::CalendarMail calendar;
    QindaTK::AgentHost agent;
    return mail.metaObject()->indexOfMethod("headersFinished(QString,QVariantMap)") < 0
        || sieve.busy() || QindaTK::LocalReport::MaximumGroups != 10;
}
