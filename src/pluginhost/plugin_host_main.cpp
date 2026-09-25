// SPDX-License-Identifier: LGPL-3.0-or-later
// qindatk-plugin-host: scans or serves one CLAP plugin out of process (D-190).
//
//   qindatk-plugin-host --scan <binary.clap>
//       prints one JSON object {"ok", "plugins"|"error"} and exits.
//   qindatk-plugin-host --serve <binary.clap> <plugin id> --fd <memfd>
//       hosts one instance: JSON control lines on stdin/stdout, audio blocks
//       in the inherited shared memory (audio_plugin_ipc_p.h).
//
// AGENT-CONTRACT (AudioPluginInstance / AudioPluginRegistry): requests are
// {"id": n, "op": ...}; each gets exactly one reply {"id": n, "ok": bool,
// "error"?: text, ...}. Lines without "id" are events ({"event": ...}).
// A plugin that writes to stdout would corrupt the protocol, so the protocol
// uses a private duplicate of fd 1 and fd 1 itself is pointed at stderr.
#include "plugin_host_clap.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QSocketNotifier>
#include <QTimer>
#include <cerrno>
#include <fcntl.h>
#include <mutex>
#include <sys/mman.h>
#include <unistd.h>

using namespace QindaTK;
using namespace QindaTK::PluginHost;

namespace {

int g_protocol = -1;
std::mutex g_writeMutex;

void writeLine(const QJsonObject &object)
{
    const QByteArray line = QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
    std::lock_guard lock(g_writeMutex);
    qsizetype done = 0;
    while (done < line.size()) {
        const ssize_t written = ::write(g_protocol, line.constData() + done, std::size_t(line.size() - done));
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) return; // the library went away; stdin EOF ends this process
        done += written;
    }
}

void takeProtocolChannel()
{
    g_protocol = ::dup(STDOUT_FILENO);
    ::dup2(STDERR_FILENO, STDOUT_FILENO);
}

int scan(const QString &path)
{
    ClapLibrary library;
    QString error;
    if (!library.open(path, &error)) {
        writeLine({{QStringLiteral("ok"), false}, {QStringLiteral("error"), error}});
        return 2;
    }
    writeLine({{QStringLiteral("ok"), true},
               {QStringLiteral("clap"), QStringLiteral("%1.%2.%3").arg(CLAP_VERSION_MAJOR).arg(CLAP_VERSION_MINOR).arg(CLAP_VERSION_REVISION)},
               {QStringLiteral("plugins"), library.describe()}});
    return 0;
}

class Server {
public:
    Server(QString path, QString pluginId, PluginIpc::Block *block)
        : m_path(std::move(path)), m_pluginId(std::move(pluginId)), m_block(block)
    {
        m_input = new QSocketNotifier(STDIN_FILENO, QSocketNotifier::Read);
        QObject::connect(m_input, &QSocketNotifier::activated, [this] { readInput(); });
        m_tick = new QTimer;
        m_tick->setInterval(10);
        QObject::connect(m_tick, &QTimer::timeout, [this] {
            if (m_instance) m_instance->tick();
        });
        m_tick->start();
    }
    ~Server()
    {
        delete m_tick;
        delete m_input;
        m_instance.reset(); // closes the editor, deactivates, joins the audio thread
        m_library.reset();
    }

private:
    void readInput()
    {
        char chunk[65536];
        const ssize_t count = ::read(STDIN_FILENO, chunk, sizeof chunk);
        if (count == 0 || (count < 0 && errno != EINTR && errno != EAGAIN)) {
            // The library closed the channel (it quit or crashed): leave cleanly.
            m_input->setEnabled(false);
            QCoreApplication::exit(0);
            return;
        }
        if (count < 0) return;
        m_buffer.append(chunk, count);
        for (qsizetype end = m_buffer.indexOf('\n'); end >= 0; end = m_buffer.indexOf('\n')) {
            const QByteArray line = m_buffer.left(end);
            m_buffer.remove(0, end + 1);
            const QJsonObject request = QJsonDocument::fromJson(line).object();
            if (!request.isEmpty()) handle(request);
        }
    }

    void reply(const QJsonObject &request, QJsonObject answer, const QString &error = {})
    {
        answer.insert(QStringLiteral("id"), request.value(QStringLiteral("id")));
        answer.insert(QStringLiteral("ok"), error.isEmpty());
        if (!error.isEmpty()) answer.insert(QStringLiteral("error"), error);
        writeLine(answer);
    }

    void handle(const QJsonObject &request)
    {
        const QString op = request.value(QStringLiteral("op")).toString();
        QString error;
        if (op == QLatin1String("load")) {
            if (m_instance) return reply(request, {}, QStringLiteral("The plugin is already loaded"));
            m_library = std::make_unique<ClapLibrary>();
            if (!m_library->open(m_path, &error)) return reply(request, {}, error);
            m_instance = std::make_unique<ClapInstance>(m_library->factory(), writeLine);
            if (!m_instance->create(m_pluginId, &error)) {
                m_instance.reset();
                return reply(request, {}, error);
            }
            m_instance->attach(m_block);
            QJsonObject answer = m_instance->describe();
            answer.insert(QStringLiteral("protocol"), PluginIpc::kProtocol);
            answer.insert(QStringLiteral("processId"), qint64(::getpid()));
            return reply(request, answer);
        }
        if (op == QLatin1String("quit")) {
            if (m_instance) m_instance->shutdown();
            reply(request, {});
            QCoreApplication::exit(0);
            return;
        }
        if (!m_instance) return reply(request, {}, QStringLiteral("No plugin is loaded"));
        if (op == QLatin1String("activate")) {
            const double rate = request.value(QStringLiteral("sampleRate")).toDouble();
            const auto frames = quint32(request.value(QStringLiteral("maxFrames")).toInt(PluginIpc::kMaxFrames));
            if (rate <= 0 || frames < 1 || frames > quint32(PluginIpc::kMaxFrames))
                return reply(request, {}, QStringLiteral("Invalid activation"));
            if (!m_instance->activate(rate, frames, &error)) return reply(request, {}, error);
            return reply(request, {{QStringLiteral("latency"), qint64(m_instance->latency())},
                                   {QStringLiteral("audioInputs"), m_instance->inputChannels()},
                                   {QStringLiteral("audioOutputs"), m_instance->outputChannels()}});
        }
        if (op == QLatin1String("deactivate")) {
            m_instance->deactivate();
            return reply(request, {});
        }
        if (op == QLatin1String("params"))
            return reply(request, {{QStringLiteral("parameters"), m_instance->parameters()}});
        if (op == QLatin1String("set-params")) {
            QVector<QPair<quint32, double>> values;
            for (const QJsonValue &entry : request.value(QStringLiteral("values")).toArray()) {
                const QJsonArray pair = entry.toArray();
                values.push_back({quint32(pair.at(0).toInteger()), pair.at(1).toDouble()});
            }
            m_instance->setParameters(values);
            return reply(request, {});
        }
        if (op == QLatin1String("state-save")) {
            QByteArray data;
            if (!m_instance->saveState(&data, &error)) return reply(request, {}, error);
            return reply(request, {{QStringLiteral("data"), QString::fromLatin1(data.toBase64())}});
        }
        if (op == QLatin1String("state-load")) {
            const QByteArray data = QByteArray::fromBase64(request.value(QStringLiteral("data")).toString().toLatin1());
            if (!m_instance->loadState(data, &error)) return reply(request, {}, error);
            return reply(request, {});
        }
        if (op == QLatin1String("gui-open")) {
            const QJsonObject answer = m_instance->openEditor(request, &error);
            return reply(request, answer, error);
        }
        if (op == QLatin1String("gui-close")) return reply(request, {{QStringLiteral("closed"), m_instance->closeEditor()}});
        if (op == QLatin1String("gui-size")) {
            const bool resized = m_instance->setEditorSize(quint32(request.value(QStringLiteral("width")).toInt()),
                                                           quint32(request.value(QStringLiteral("height")).toInt()));
            return reply(request, {{QStringLiteral("resized"), resized}});
        }
        reply(request, {}, QStringLiteral("Unknown request %1").arg(op));
    }

    QString m_path;
    QString m_pluginId;
    PluginIpc::Block *m_block = nullptr;
    QSocketNotifier *m_input = nullptr;
    QTimer *m_tick = nullptr;
    QByteArray m_buffer;
    std::unique_ptr<ClapLibrary> m_library;
    std::unique_ptr<ClapInstance> m_instance; // destroyed before the library
};

int serve(const QString &path, const QString &pluginId, int fd)
{
    void *memory = ::mmap(nullptr, sizeof(PluginIpc::Block), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (memory == MAP_FAILED) {
        writeLine({{QStringLiteral("event"), QStringLiteral("fatal")}, {QStringLiteral("message"), QStringLiteral("The shared audio block cannot be mapped")}});
        return 3;
    }
    auto *block = static_cast<PluginIpc::Block *>(memory);
    if (block->magic != PluginIpc::kMagic || block->version != PluginIpc::kVersion) {
        writeLine({{QStringLiteral("event"), QStringLiteral("fatal")}, {QStringLiteral("message"), QStringLiteral("The shared audio block has another version")}});
        return 3;
    }
    ::fcntl(STDIN_FILENO, F_SETFL, ::fcntl(STDIN_FILENO, F_GETFL) | O_NONBLOCK);
    int code = 0;
    {
        Server server(path, pluginId, block);
        code = QCoreApplication::exec();
    }
    ::munmap(memory, sizeof(PluginIpc::Block));
    return code;
}

} // namespace

int main(int argc, char **argv)
{
    takeProtocolChannel();
    QCoreApplication app(argc, argv);
    const QStringList args = QCoreApplication::arguments();
    if (args.size() == 3 && args.at(1) == QLatin1String("--scan")) return scan(args.at(2));
    if (args.size() == 6 && args.at(1) == QLatin1String("--serve") && args.at(4) == QLatin1String("--fd")) {
        bool ok = false;
        const int fd = args.at(5).toInt(&ok);
        if (ok && fd >= 0) return serve(args.at(2), args.at(3), fd);
    }
    writeLine({{QStringLiteral("ok"), false},
               {QStringLiteral("error"), QStringLiteral("usage: qindatk-plugin-host --scan <binary> | --serve <binary> <plugin id> --fd <n>")}});
    return 64;
}
