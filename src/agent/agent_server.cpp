// SPDX-License-Identifier: LGPL-3.0-or-later
#include "agent_server.h"
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLocalSocket>
#include <QStandardPaths>
#include <QTimer>
#ifdef Q_OS_UNIX
#include <sys/stat.h>
#include <unistd.h>
#endif
namespace QindaTK {
QString AgentServer::runtimeDirectory()
{return QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation)+QStringLiteral("/qindatk-agent");}
AgentServer::AgentServer(AgentHost *host,QObject *parent):QObject(parent),m_host(host)
{
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    connect(&m_server,&QLocalServer::newConnection,this,[this]{
        while(m_server.hasPendingConnections()){
            auto *socket=m_server.nextPendingConnection();
            if(m_connections>=32){socket->abort();socket->deleteLater();continue;}
            ++m_connections;socket->setReadBufferSize(262145);
            auto *buffer=new QByteArray;
            auto *timer=new QTimer(socket);timer->setSingleShot(true);timer->start(30000);
            connect(timer,&QTimer::timeout,socket,&QLocalSocket::abort);
            connect(socket,&QLocalSocket::disconnected,this,[this,socket,buffer]{--m_connections;delete buffer;socket->deleteLater();});
            connect(socket,&QLocalSocket::readyRead,this,[this,socket,buffer,timer]{
                buffer->append(socket->readAll());timer->start(30000);
                if(buffer->size()>262144){socket->abort();return;}
                int count=0;
                while(buffer->contains('\n')){
                    if(++count>64){socket->abort();return;}
                    const auto line=buffer->left(buffer->indexOf('\n'));buffer->remove(0,line.size()+1);
                    QJsonParseError error;const auto document=QJsonDocument::fromJson(line,&error);const auto rpc=document.object();
                    QJsonObject response{{QStringLiteral("jsonrpc"),QStringLiteral("2.0")},{QStringLiteral("id"),rpc.value(QStringLiteral("id")).isUndefined()?QJsonValue(QJsonValue::Null):rpc.value(QStringLiteral("id"))}};
                    if(error.error!=QJsonParseError::NoError||!document.isObject()||rpc.value(QStringLiteral("jsonrpc")).toString()!=QLatin1String("2.0")||!rpc.value(QStringLiteral("method")).isString()||(!rpc.value(QStringLiteral("params")).isUndefined()&&!rpc.value(QStringLiteral("params")).isObject()))
                        response.insert(QStringLiteral("error"),QJsonObject{{QStringLiteral("code"),-32600},{QStringLiteral("message"),QStringLiteral("Invalid JSON-RPC request")}});
                    else {
                        if(!rpc.contains(QStringLiteral("id")))continue;
                        response.insert(QStringLiteral("result"),m_host->request(rpc.value(QStringLiteral("method")).toString(),rpc.value(QStringLiteral("params")).toObject()));
                    }
                    const auto bytes=QJsonDocument(response).toJson(QJsonDocument::Compact)+'\n';
                    if(bytes.size()>8388608||socket->bytesToWrite()>8388608){socket->abort();return;}
                    // Notifications never execute mutations: writing without an ID
                    // makes exactly-once receipts impossible.
                    socket->write(bytes);
                }
            });
        }
    });
}
bool AgentServer::listen(const QString &path,QString *error)
{
    const QFileInfo info(path);const QByteArray directory=QFile::encodeName(info.absolutePath());
    bool safe=info.isAbsolute()&&!info.isSymLink();
#ifdef Q_OS_UNIX
    struct stat s{};safe=safe&&::lstat(directory.constData(),&s)==0&&S_ISDIR(s.st_mode)&&s.st_uid==::getuid()&&(s.st_mode&0077)==0;
#else
    safe=false; // Named-pipe policy needs a separately qualified implementation.
#endif
    if(!safe){if(error)*error=QStringLiteral("Socket parent must be a user-owned private directory (0700)");return false;}
    if(!m_server.listen(path)){if(error)*error=m_server.errorString();return false;}
    return true;
}
}
