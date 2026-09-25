// SPDX-License-Identifier: LGPL-3.0-or-later
#include "audio_plugin_registry.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>

namespace QindaTK {
namespace {

QJsonObject binaryJson(const AudioPluginRegistry::Binary &binary)
{
    QJsonArray plugins;
    for (const AudioPluginDescriptor &plugin : binary.plugins) plugins.push_back(plugin.toJson());
    return {{QStringLiteral("path"), binary.path},
            {QStringLiteral("size"), binary.size},
            {QStringLiteral("modified"), binary.modified},
            {QStringLiteral("state"), binary.state},
            {QStringLiteral("reason"), binary.reason},
            {QStringLiteral("plugins"), plugins}};
}

AudioPluginRegistry::Binary binaryFrom(const QJsonObject &object)
{
    AudioPluginRegistry::Binary binary;
    binary.path = object.value(QStringLiteral("path")).toString();
    binary.size = object.value(QStringLiteral("size")).toInteger(-1);
    binary.modified = object.value(QStringLiteral("modified")).toInteger(-1);
    binary.state = object.value(QStringLiteral("state")).toString();
    binary.reason = object.value(QStringLiteral("reason")).toString();
    for (const QJsonValue &value : object.value(QStringLiteral("plugins")).toArray())
        binary.plugins.push_back(AudioPluginDescriptor::fromJson(value.toObject()));
    return binary;
}

// The row a binary without usable plugins shows in the browser.
QVariantMap failedRow(const AudioPluginRegistry::Binary &binary)
{
    AudioPluginDescriptor row;
    row.pluginId = QStringLiteral("clap-file:") + binary.path;
    row.name = QFileInfo(binary.path).fileName();
    row.path = binary.path;
    row.available = false;
    row.scanState = binary.state;
    row.reason = binary.reason;
    return row.browserRow();
}

} // namespace

AudioPluginRegistry::AudioPluginRegistry(QObject *parent)
    : QObject(parent),
      m_host(AudioPlugins::hostExecutable()),
      m_searchPaths(AudioPlugins::defaultSearchPaths()),
      m_cacheFile(QDir(QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation))
                      .filePath(QStringLiteral("qindatk/clap-plugins.json")))
{
    loadCache();
}

AudioPluginRegistry::~AudioPluginRegistry()
{
    // A running scan keeps only copies; its result is dropped with the watcher.
    delete m_watcher;
}

void AudioPluginRegistry::setSearchPaths(const QStringList &paths)
{
    if (paths == m_searchPaths) return;
    m_searchPaths = paths;
    emit searchPathsChanged();
}

void AudioPluginRegistry::setCacheFile(const QString &file)
{
    if (file == m_cacheFile) return;
    m_cacheFile = file;
    m_binaries.clear();
    m_order.clear();
    m_plugins.clear();
    loadCache();
    emit cacheFileChanged();
    emit pluginsChanged();
}

void AudioPluginRegistry::setScanTimeoutMs(int milliseconds)
{
    milliseconds = std::max(100, milliseconds);
    if (milliseconds == m_scanTimeoutMs) return;
    m_scanTimeoutMs = milliseconds;
    emit scanTimeoutMsChanged();
}

QVariantList AudioPluginRegistry::plugins() const
{
    QVariantList rows;
    for (const QString &path : m_order) {
        const Binary binary = m_binaries.value(path);
        if (binary.state != QLatin1String("ok")) {
            rows.push_back(failedRow(binary));
            continue;
        }
        for (const AudioPluginDescriptor &plugin : binary.plugins)
            if (m_plugins.value(plugin.pluginId).path == path) rows.push_back(plugin.browserRow());
    }
    return rows;
}

QVariantList AudioPluginRegistry::binaries() const
{
    QVariantList rows;
    for (const QString &path : m_order) {
        const Binary binary = m_binaries.value(path);
        rows.push_back(QVariantMap{{QStringLiteral("path"), binary.path},
                                   {QStringLiteral("state"), binary.state},
                                   {QStringLiteral("reason"), binary.reason},
                                   {QStringLiteral("plugins"), int(binary.plugins.size())},
                                   {QStringLiteral("cached"), binary.fromCache}});
    }
    return rows;
}

const AudioPluginDescriptor *AudioPluginRegistry::descriptor(const QString &pluginId) const
{
    const auto found = m_plugins.constFind(pluginId);
    return found == m_plugins.cend() ? nullptr : &found.value();
}

QVariantMap AudioPluginRegistry::descriptorData(const QString &pluginId) const
{
    const AudioPluginDescriptor *plugin = descriptor(pluginId);
    return plugin ? plugin->browserRow() : QVariantMap();
}

QStringList AudioPluginRegistry::candidates() const
{
    QStringList files;
    const auto add = [&files](const QString &file) {
        const QString canonical = QFileInfo(file).canonicalFilePath();
        if (!canonical.isEmpty() && !files.contains(canonical)) files.push_back(canonical);
    };
    for (const QString &entry : m_searchPaths) {
        const QFileInfo info(entry);
        if (info.isFile() && entry.endsWith(QLatin1String(".clap"))) {
            add(entry);
            continue;
        }
        if (!info.isDir()) continue;
        QStringList found;
        QDirIterator it(entry, {QStringLiteral("*.clap")}, QDir::Files, QDirIterator::Subdirectories | QDirIterator::FollowSymlinks);
        while (it.hasNext()) found.push_back(it.next());
        found.sort();
        for (const QString &file : std::as_const(found)) add(file);
    }
    return files;
}

AudioPluginRegistry::Binary AudioPluginRegistry::scanBinary(const QString &host, const QString &path, int timeoutMs)
{
    Binary binary;
    binary.path = path;
    binary.state = QStringLiteral("failed");
    if (host.isEmpty()) {
        binary.reason = tr("qindatk-plugin-host is not installed; plugins cannot be scanned");
        return binary;
    }
    QProcess process;
    process.setProcessChannelMode(QProcess::ForwardedErrorChannel);
    process.start(host, {QStringLiteral("--scan"), path});
    if (!process.waitForStarted(5000)) {
        binary.reason = tr("The plugin host could not start: %1").arg(process.errorString());
        return binary;
    }
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished(2000);
        binary.state = QStringLiteral("timeout");
        binary.reason = tr("The scan did not finish within %1 ms").arg(timeoutMs);
        return binary;
    }
    if (process.exitStatus() == QProcess::CrashExit) {
        binary.state = QStringLiteral("crashed");
        binary.reason = tr("The plugin crashed while it was scanned");
        return binary;
    }
    const QByteArray output = process.readAllStandardOutput();
    const QJsonObject result = QJsonDocument::fromJson(output.left(output.indexOf('\n'))).object();
    if (!result.value(QStringLiteral("ok")).toBool()) {
        binary.reason = result.value(QStringLiteral("error")).toString(tr("The plugin host gave no scan result"));
        return binary;
    }
    binary.state = QStringLiteral("ok");
    for (const QJsonValue &value : result.value(QStringLiteral("plugins")).toArray()) {
        const QJsonObject entry = value.toObject();
        AudioPluginDescriptor plugin = AudioPluginDescriptor::fromJson(entry);
        plugin.path = path;
        plugin.state = entry.value(QStringLiteral("state")).toBool();
        const QString error = entry.value(QStringLiteral("error")).toString();
        plugin.available = error.isEmpty();
        plugin.scanState = error.isEmpty() ? QStringLiteral("available") : QStringLiteral("failed");
        plugin.reason = error;
        if (!plugin.pluginId.isEmpty()) binary.plugins.push_back(plugin);
    }
    return binary;
}

QList<AudioPluginRegistry::Binary> AudioPluginRegistry::run(const QString &host, const QStringList &files,
                                                            const QHash<QString, Binary> &cache, bool force,
                                                            int timeoutMs)
{
    QList<Binary> results;
    for (const QString &file : files) {
        const QFileInfo info(file);
        const qint64 size = info.size();
        const qint64 modified = info.lastModified().toMSecsSinceEpoch();
        const auto cached = cache.constFind(file);
        if (!force && cached != cache.cend() && cached->size == size && cached->modified == modified) {
            Binary binary = *cached;
            binary.fromCache = true;
            results.push_back(binary);
            continue;
        }
        Binary binary = scanBinary(host, file, timeoutMs);
        binary.size = size;
        binary.modified = modified;
        results.push_back(binary);
    }
    return results;
}

void AudioPluginRegistry::scan(bool force)
{
    if (m_watcher) return; // one scan at a time
    setScanState(QStringLiteral("scanning"), tr("Scanning plugins"));
    m_watcher = new QFutureWatcher<QList<Binary>>(this);
    connect(m_watcher, &QFutureWatcher<QList<Binary>>::finished, this, [this] {
        const QList<Binary> results = m_watcher->result();
        m_watcher->deleteLater();
        m_watcher = nullptr;
        apply(results);
    });
    m_watcher->setFuture(QtConcurrent::run(&AudioPluginRegistry::run, m_host, candidates(), m_binaries, force, m_scanTimeoutMs));
}

bool AudioPluginRegistry::scanNow(bool force)
{
    if (m_watcher) {
        m_watcher->waitForFinished();
        const QList<Binary> pending = m_watcher->result();
        m_watcher->disconnect(this);
        m_watcher->deleteLater();
        m_watcher = nullptr;
        apply(pending);
    }
    setScanState(QStringLiteral("scanning"), tr("Scanning plugins"));
    apply(run(m_host, candidates(), m_binaries, force, m_scanTimeoutMs));
    return !m_host.isEmpty();
}

void AudioPluginRegistry::apply(const QList<Binary> &results)
{
    m_binaries.clear();
    m_order.clear();
    m_plugins.clear();
    int failed = 0;
    for (const Binary &binary : results) {
        m_binaries.insert(binary.path, binary);
        m_order.push_back(binary.path);
        if (binary.state != QLatin1String("ok")) ++failed;
        for (const AudioPluginDescriptor &plugin : binary.plugins)
            if (!m_plugins.contains(plugin.pluginId)) m_plugins.insert(plugin.pluginId, plugin);
    }
    saveCache();
    setScanState(QStringLiteral("ready"), failed ? tr("%1 plugins · %2 binaries could not be scanned").arg(m_plugins.size()).arg(failed)
                                                 : tr("%1 plugins").arg(m_plugins.size()));
    emit pluginsChanged();
    emit scanFinished();
}

void AudioPluginRegistry::loadCache()
{
    QFile file(m_cacheFile);
    if (m_cacheFile.isEmpty() || !file.open(QIODevice::ReadOnly)) return;
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    if (root.value(QStringLiteral("schema")).toString() != QLatin1String("qindatk.plugin-cache/1")) return;
    for (const QJsonValue &value : root.value(QStringLiteral("binaries")).toArray()) {
        Binary binary = binaryFrom(value.toObject());
        if (binary.path.isEmpty() || m_binaries.contains(binary.path)) continue;
        binary.fromCache = true;
        m_order.push_back(binary.path);
        for (const AudioPluginDescriptor &plugin : std::as_const(binary.plugins))
            if (!m_plugins.contains(plugin.pluginId)) m_plugins.insert(plugin.pluginId, plugin);
        m_binaries.insert(binary.path, binary);
    }
}

void AudioPluginRegistry::saveCache() const
{
    if (m_cacheFile.isEmpty()) return;
    QJsonArray binaries;
    for (const QString &path : m_order) binaries.push_back(binaryJson(m_binaries.value(path)));
    QDir().mkpath(QFileInfo(m_cacheFile).absolutePath());
    QSaveFile out(m_cacheFile);
    if (!out.open(QIODevice::WriteOnly)) return;
    out.write(QJsonDocument(QJsonObject{{QStringLiteral("schema"), QStringLiteral("qindatk.plugin-cache/1")},
                                        {QStringLiteral("binaries"), binaries}})
                  .toJson());
    out.commit();
}

void AudioPluginRegistry::setScanState(const QString &state, const QString &message)
{
    if (state == m_scanState && message == m_scanMessage) return;
    m_scanState = state;
    m_scanMessage = message;
    emit scanStateChanged();
}

} // namespace QindaTK
