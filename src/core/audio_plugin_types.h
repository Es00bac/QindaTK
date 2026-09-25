// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace QindaTK {

// AGENT-CONTRACT (WP-066, D-190, docs/audio-plugins.md): what a scan learned
// about one plugin. Scans run in qindatk-plugin-host (--scan), so every field
// comes from the plugin's own descriptor and extensions; a binary that fails
// or crashes while scanned becomes one unavailable entry with its reason.
// PluginBrowser rows are browserRow(); the registry cache stores toJson().
struct AudioPluginDescriptor {
    QString pluginId;           // the CLAP plugin ID (stable across versions)
    QString format = QStringLiteral("CLAP");
    QString name;
    QString vendor;
    QString version;
    QString description;
    QString url;
    QString path;               // absolute path of the binary
    QStringList features;       // CLAP feature strings, in the plugin's order
    int audioInputs = 0;        // main input port channels (0: instrument)
    int audioOutputs = 0;       // main output port channels
    int noteInputs = 0;         // note input ports
    int parameterCount = 0;
    bool state = false;         // offers the state extension
    QStringList editors;        // "x11", "x11-floating", "wayland-floating"
    bool available = true;
    QString scanState = QStringLiteral("available"); // available, failed, crashed, timeout, missing
    QString reason;

    QString kind() const;       // "Instrument", "Effect", "Note effect" or "Analyzer"
    QString category() const;   // the first descriptive feature, title-cased; "Other"
    QStringList layouts() const;
    bool acceptsNotes() const { return noteInputs > 0; }
    QVariantMap browserRow() const;
    QJsonObject toJson() const;
    static AudioPluginDescriptor fromJson(const QJsonObject &object);
};

namespace AudioPlugins {

// Instance IDs are "plugin:<uuid>"; a plugin parameter's automation ID is
// "<instance id>/<CLAP parameter id>" (D-191).
QString newInstanceId();
bool isInstanceId(const QString &id);
QString parameterId(const QString &instanceId, quint32 clapParameterId);

// The out-of-process host executable: $QINDATK_PLUGIN_HOST, the build
// tree's copy, the installed libexec copy, then PATH. Empty when none exists.
QString hostExecutable();
// CLAP search paths: $CLAP_PATH entries, ~/.clap, /usr/lib/clap,
// /usr/lib64/clap, /usr/local/lib/clap (the CLAP convention on Linux).
QStringList defaultSearchPaths();
// The capability declaration (docs/audio-plugins.md): one entry per format,
// each with its own availability, isolation and supported features.
QVariantList formats();

} // namespace AudioPlugins
} // namespace QindaTK
