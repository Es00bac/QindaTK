// SPDX-License-Identifier: LGPL-3.0-or-later
#include "manage_sieve_jobs.h"
#include "manage_sieve_client.h"

namespace QindaTK::SieveJobs {
namespace {
QByteArray quoted(const QString &text)
{ QByteArray bytes = text.toUtf8(); bytes.replace("\\", "\\\\"); bytes.replace("\"", "\\\""); return '"' + bytes + '"'; }
struct Reply { QList<QList<QByteArray>> rows; QString code, error; bool ok = false; };

// RFC 5804 strings may be quoted or byte-counted literals. Read literal bytes
// outside line framing and omit every payload from the diagnostic transcript.
bool frame(Protocol::Channel &channel, QList<QByteArray> *tokens, bool *firstString)
{
    QByteArray line;
    if (!channel.readLine(&line, false)) return false;
    qsizetype i = 0;
    while (i < line.size()) {
        const char c = line[i];
        if (c == ' ' || c == '\t') { ++i; continue; }
        if (c == '(' || c == ')') { tokens->append(QByteArray(1, c)); ++i; continue; }
        if (tokens->isEmpty() && (c == '"' || c == '{')) *firstString = true;
        if (c == '"') {
            QByteArray token;
            bool closed = false;
            for (++i; i < line.size(); ++i) {
                if (line[i] == '"') { ++i; closed = true; break; }
                if (line[i] == '\\') {
                    ++i;
                    if (i >= line.size() || (line[i] != '\\' && line[i] != '"')) break;
                }
                token += line[i];
            }
            if (!closed) break;
            tokens->append(token);
        } else if (c == '{') {
            const qsizetype end = line.indexOf('}', i + 1);
            QByteArray sizeText = end < 0 ? QByteArray() : line.mid(i + 1, end - i - 1);
            if (sizeText.endsWith('+')) sizeText.chop(1);
            bool ok = !sizeText.isEmpty();
            for (const char digit : sizeText) ok = ok && digit >= '0' && digit <= '9';
            bool validCount = false;
            const qint64 length = sizeText.toLongLong(&validCount);
            if (!ok || !validCount || length < 0 || length > ManageSieveClient::MaximumScriptBytes || end + 1 != line.size()) break;
            QByteArray token;
            if (!channel.readBytes(length, &token) || !channel.readLine(&line, false)) return false;
            tokens->append(token);
            i = 0;
        } else {
            const qsizetype start = i;
            while (i < line.size() && line[i] != ' ' && line[i] != '\t' && line[i] != '(' && line[i] != ')') ++i;
            tokens->append(line.mid(start, i - start));
        }
        if (tokens->size() > 256) break;
    }
    if (i == line.size() && !tokens->isEmpty() && tokens->size() <= 256) return true;
    channel.fail(Protocol::Failure::Rejected, QStringLiteral("Malformed ManageSieve response"));
    return false;
}
Reply response(Protocol::Channel &channel)
{
    Reply result;
    qsizetype bytes = 0;
    while (result.rows.size() <= 10000) {
        QList<QByteArray> tokens;
        bool firstString = false;
        if (!frame(channel, &tokens, &firstString)) return result;
        for (const auto &token : tokens) bytes += token.size();
        if (bytes > 16 * 1024 * 1024) break;
        const QByteArray status = tokens.first().toUpper();
        if (!firstString && (status == "OK" || status == "NO" || status == "BYE")) {
            result.ok = status == "OK";
            const qsizetype codeEnd = tokens.indexOf(")");
            if (tokens.value(1) == "(" && codeEnd > 1) {
                QList<QByteArray> code;
                for (qsizetype i = 2; i < codeEnd; ++i) code.append(tokens[i]);
                result.code = QString::fromUtf8(code.join(' '));
            }
            if (!result.ok) {
                result.error = QString::fromUtf8(tokens.last());
                channel.fail(Protocol::Failure::Rejected, QStringLiteral("ManageSieve %1: %2").arg(QString::fromLatin1(status), result.error));
            }
            channel.note(QStringLiteral("S: %1%2").arg(QString::fromLatin1(status), result.code.isEmpty() ? QString() : QStringLiteral(" (") + result.code + QLatin1Char(')')));
            return result;
        }
        result.rows.append(tokens);
    }
    channel.fail(Protocol::Failure::Rejected, QStringLiteral("ManageSieve response limit exceeded"));
    return result;
}
QVariantMap capabilities(const Reply &reply)
{
    QVariantMap result;
    for (const auto &row : reply.rows)
        result.insert(QString::fromUtf8(row.first()).toUpper(), QString::fromUtf8(row.value(1)));
    return result;
}
bool validName(const QString &name)
{
    if (name.toUtf8().size() > 1024) return false;
    for (QChar c : name) if (c.unicode() < 0x20 || c == QChar(0x7f)) return false;
    return true;
}
}
QString validate(const Job &job)
{
    const QStringList commands{QStringLiteral("capabilities"),QStringLiteral("list"),QStringLiteral("get"),QStringLiteral("put"),
        QStringLiteral("check"),QStringLiteral("activate"),QStringLiteral("delete"),QStringLiteral("rename")};
    if (!commands.contains(job.command)) return QStringLiteral("Unknown ManageSieve command");
    if (!validName(job.name) || !validName(job.newName)) return QStringLiteral("Invalid script name");
    if ((job.command == QLatin1String("get") || job.command == QLatin1String("put") || job.command == QLatin1String("delete")
         || job.command == QLatin1String("rename")) && job.name.isEmpty()) return QStringLiteral("A script name is required");
    if (job.command == QLatin1String("rename") && job.newName.isEmpty()) return QStringLiteral("A new script name is required");
    if (job.script.size() > ManageSieveClient::MaximumScriptBytes || job.script.contains('\0')) return QStringLiteral("Script exceeds 4 MiB or contains NUL");
    return {};
}
QVariantMap run(const Job &job)
{
    Protocol::Channel channel;
    channel.addSecret(job.credentials.secret);
    Reply reply;
    QVariantMap caps;
    auto finish = [&](bool ok, QVariantMap extra = {}) {
        extra.insert(QStringLiteral("ok"), ok);
        extra.insert(QStringLiteral("state"), ok ? QStringLiteral("accepted") : Protocol::failureState(channel.failure()));
        extra.insert(QStringLiteral("error"), ok ? QString() : channel.error());
        extra.insert(QStringLiteral("responseCode"), Protocol::redact(reply.code, {job.credentials.secret}));
        extra.insert(QStringLiteral("capabilities"), caps);
        extra.insert(QStringLiteral("transcript"), channel.transcript());
        channel.close(); return extra;
    };
    if (!channel.open(job.endpoint)) return finish(false);
    reply = response(channel);
    if (!reply.ok) return finish(false);
    caps = capabilities(reply);
    if (job.endpoint.security == QLatin1String("starttls")) {
        if (!caps.contains(QStringLiteral("STARTTLS"))) {
            channel.fail(Protocol::Failure::Rejected, QStringLiteral("Server does not offer STARTTLS"));
            return finish(false);
        }
        if (!channel.write("STARTTLS\r\n")) return finish(false);
        reply = response(channel);
        if (!reply.ok || !channel.startTls()) return finish(false);
        reply = response(channel); // RFC 5804: fresh capabilities after TLS
        if (!reply.ok) return finish(false);
        caps = capabilities(reply);
    }
    if (job.credentials.isSet()) {
        if (!channel.authAllowed() || !caps.value(QStringLiteral("SASL")).toString().split(QLatin1Char(' ')).contains(QStringLiteral("PLAIN"))) {
            channel.fail(Protocol::Failure::Rejected, QStringLiteral("Secure AUTHENTICATE PLAIN is unavailable"));
            return finish(false);
        }
        const QByteArray payload = (QByteArray(1, '\0') + job.credentials.user.toUtf8() + QByteArray(1, '\0') + job.credentials.secret.toUtf8()).toBase64();
        channel.addSecret(QString::fromLatin1(payload));
        if (!channel.write("AUTHENTICATE \"PLAIN\" \"" + payload + "\"\r\n", QStringLiteral("AUTHENTICATE PLAIN <redacted>"))) return finish(false);
        reply = response(channel);
        if (!reply.ok) return finish(false);
        if (!reply.rows.isEmpty()) caps = capabilities(reply);
    }
    QByteArray command;
    if (job.command == QLatin1String("capabilities")) command = "CAPABILITY";
    else if (job.command == QLatin1String("list")) command = "LISTSCRIPTS";
    else if (job.command == QLatin1String("get")) command = "GETSCRIPT " + quoted(job.name);
    else if (job.command == QLatin1String("activate")) command = "SETACTIVE " + quoted(job.name);
    else if (job.command == QLatin1String("delete")) command = "DELETESCRIPT " + quoted(job.name);
    else if (job.command == QLatin1String("rename")) command = "RENAMESCRIPT " + quoted(job.name) + ' ' + quoted(job.newName);
    else {
        command = job.command == QLatin1String("put") ? "PUTSCRIPT " + quoted(job.name) + ' ' : QByteArray("CHECKSCRIPT ");
        command += '{' + QByteArray::number(job.script.size()) + "+}\r\n" + job.script;
    }
    if (!channel.write(command + "\r\n", job.command + QStringLiteral(" <request>"))) return finish(false);
    reply = response(channel);
    if (!reply.ok) return finish(false);
    QVariantMap extra;
    if (job.command == QLatin1String("get")) {
        if (reply.rows.size() != 1 || reply.rows.first().size() != 1) {
            channel.fail(Protocol::Failure::Rejected, QStringLiteral("GETSCRIPT returned no single script literal"));
            return finish(false);
        }
        extra.insert(QStringLiteral("script"), reply.rows.first().first());
    }
    else if (job.command == QLatin1String("capabilities")) caps = capabilities(reply);
    else if (job.command == QLatin1String("list")) {
        QVariantList scripts;
        for (const auto &row : reply.rows) scripts.append(QVariantMap{{QStringLiteral("name"), QString::fromUtf8(row.first())},
            {QStringLiteral("active"), row.value(1).toUpper() == "ACTIVE"}});
        extra.insert(QStringLiteral("scripts"), scripts);
    }
    // A failed logout cannot undo an acknowledged script mutation.
    channel.write("LOGOUT\r\n");
    return finish(true, extra);
}
}
