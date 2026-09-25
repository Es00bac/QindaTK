// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "audio_plugin_types.h"

#include <QFutureWatcher>
#include <QHash>
#include <QList>
#include <QObject>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// AGENT-CONTRACT (WP-066, D-190, docs/audio-plugins.md): plugin discovery.
// Every *.clap binary under the search paths is scanned by
// qindatk-plugin-host --scan in its own process with a time limit, so a
// binary that crashes, hangs or refuses to load becomes an unavailable row
// with its reason and never takes the application down. Results are cached
// by path, size and modification time (cacheFile): an unchanged binary is not
// scanned again, including one that failed, until scan(true). `plugins` is
// the PluginBrowser row list; AudioPluginHost only instantiates plugins found
// here (a session file never names a binary to load).
class AudioPluginRegistry : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QStringList searchPaths READ searchPaths WRITE setSearchPaths NOTIFY searchPathsChanged)
    Q_PROPERTY(QString cacheFile READ cacheFile WRITE setCacheFile NOTIFY cacheFileChanged)
    Q_PROPERTY(int scanTimeoutMs READ scanTimeoutMs WRITE setScanTimeoutMs NOTIFY scanTimeoutMsChanged)
    Q_PROPERTY(QVariantList plugins READ plugins NOTIFY pluginsChanged)
    Q_PROPERTY(QVariantList binaries READ binaries NOTIFY pluginsChanged)
    Q_PROPERTY(QString scanState READ scanState NOTIFY scanStateChanged)
    Q_PROPERTY(QString scanMessage READ scanMessage NOTIFY scanStateChanged)
    Q_PROPERTY(QVariantList formats READ formats CONSTANT)
    Q_PROPERTY(QString hostExecutable READ hostExecutable CONSTANT)
public:
    struct Binary {
        QString path;
        qint64 size = -1;
        qint64 modified = -1;   // ms since the epoch
        QString state;          // ok, failed, crashed, timeout
        QString reason;
        QList<AudioPluginDescriptor> plugins;
        bool fromCache = false;
    };

    explicit AudioPluginRegistry(QObject *parent = nullptr);
    ~AudioPluginRegistry() override;

    QStringList searchPaths() const { return m_searchPaths; }
    void setSearchPaths(const QStringList &paths);
    QString cacheFile() const { return m_cacheFile; }
    void setCacheFile(const QString &file);
    int scanTimeoutMs() const { return m_scanTimeoutMs; }
    void setScanTimeoutMs(int milliseconds);
    QVariantList plugins() const;
    QVariantList binaries() const;
    QString scanState() const { return m_scanState; }
    QString scanMessage() const { return m_scanMessage; }
    QVariantList formats() const { return AudioPlugins::formats(); }
    QString hostExecutable() const { return m_host; }

    // Asynchronous (a worker thread runs the scan processes); `force` rescans
    // unchanged binaries too. scanNow() blocks the caller until it is done.
    Q_INVOKABLE void scan(bool force = false);
    Q_INVOKABLE bool scanNow(bool force = false);
    Q_INVOKABLE QVariantMap descriptorData(const QString &pluginId) const;
    const AudioPluginDescriptor *descriptor(const QString &pluginId) const;
    // One binary in its own scan process (any thread).
    static Binary scanBinary(const QString &host, const QString &path, int timeoutMs);

signals:
    void searchPathsChanged();
    void cacheFileChanged();
    void scanTimeoutMsChanged();
    void pluginsChanged();
    void scanStateChanged();
    void scanFinished();

private:
    static QList<Binary> run(const QString &host, const QStringList &files, const QHash<QString, Binary> &cache,
                             bool force, int timeoutMs);
    QStringList candidates() const;
    void apply(const QList<Binary> &results);
    void loadCache();
    void saveCache() const;
    void setScanState(const QString &state, const QString &message);

    QString m_host;
    QStringList m_searchPaths;
    QString m_cacheFile;
    int m_scanTimeoutMs = 10000;
    QHash<QString, Binary> m_binaries;   // by path
    QStringList m_order;                 // scan order (search path order)
    QHash<QString, AudioPluginDescriptor> m_plugins; // by plugin ID, first binary wins
    QString m_scanState = QStringLiteral("idle");
    QString m_scanMessage;
    QFutureWatcher<QList<Binary>> *m_watcher = nullptr;
};

} // namespace QindaTK
