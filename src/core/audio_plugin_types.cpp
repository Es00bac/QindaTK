// SPDX-License-Identifier: LGPL-3.0-or-later
#include "audio_plugin_types.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QStandardPaths>
#include <QUuid>
#include <dlfcn.h>

namespace QindaTK {
namespace {

// Role features (what the plugin is), as opposed to descriptive categories.
bool roleFeature(const QString &feature)
{
    static const QStringList roles = {QStringLiteral("instrument"), QStringLiteral("audio-effect"),
                                      QStringLiteral("note-effect"), QStringLiteral("note-detector"),
                                      QStringLiteral("analyzer"), QStringLiteral("mono"), QStringLiteral("stereo"),
                                      QStringLiteral("surround"), QStringLiteral("ambisonic")};
    return roles.contains(feature);
}

QString layoutName(int channels)
{
    return channels == 1 ? QStringLiteral("Mono") : channels == 2 ? QStringLiteral("Stereo")
                                                                   : QStringLiteral("%1 ch").arg(channels);
}

QStringList strings(const QJsonValue &value)
{
    QStringList out;
    for (const QJsonValue &entry : value.toArray()) out.push_back(entry.toString());
    return out;
}

} // namespace

QString AudioPluginDescriptor::kind() const
{
    if (features.contains(QStringLiteral("instrument"))) return QStringLiteral("Instrument");
    if (features.contains(QStringLiteral("note-effect"))) return QStringLiteral("Note effect");
    if (features.contains(QStringLiteral("analyzer"))) return QStringLiteral("Analyzer");
    return QStringLiteral("Effect");
}

QString AudioPluginDescriptor::category() const
{
    for (const QString &feature : features) {
        if (feature.isEmpty() || roleFeature(feature)) continue;
        QString title = feature;
        title.replace(QLatin1Char('-'), QLatin1Char(' '));
        title[0] = title.at(0).toUpper();
        return title;
    }
    return QStringLiteral("Other");
}

QStringList AudioPluginDescriptor::layouts() const
{
    if (audioOutputs <= 0) return {};
    if (audioInputs <= 0 || audioInputs == audioOutputs) return {layoutName(audioOutputs)};
    return {QStringLiteral("%1→%2").arg(layoutName(audioInputs), layoutName(audioOutputs))};
}

QVariantMap AudioPluginDescriptor::browserRow() const
{
    return {{QStringLiteral("pluginId"), pluginId},
            {QStringLiteral("format"), format},
            {QStringLiteral("name"), name.isEmpty() ? pluginId : name},
            {QStringLiteral("vendor"), vendor},
            {QStringLiteral("version"), version},
            {QStringLiteral("category"), category()},
            {QStringLiteral("kind"), kind()},
            {QStringLiteral("layouts"), layouts()},
            {QStringLiteral("available"), available},
            {QStringLiteral("state"), scanState},
            {QStringLiteral("reason"), reason},
            {QStringLiteral("path"), path},
            {QStringLiteral("editor"), !editors.isEmpty()},
            {QStringLiteral("presets"), QVariantList()}};
}

QJsonObject AudioPluginDescriptor::toJson() const
{
    return {{QStringLiteral("pluginId"), pluginId},
            {QStringLiteral("format"), format},
            {QStringLiteral("name"), name},
            {QStringLiteral("vendor"), vendor},
            {QStringLiteral("version"), version},
            {QStringLiteral("description"), description},
            {QStringLiteral("url"), url},
            {QStringLiteral("path"), path},
            {QStringLiteral("features"), QJsonArray::fromStringList(features)},
            {QStringLiteral("audioInputs"), audioInputs},
            {QStringLiteral("audioOutputs"), audioOutputs},
            {QStringLiteral("noteInputs"), noteInputs},
            {QStringLiteral("parameterCount"), parameterCount},
            {QStringLiteral("state"), state},
            {QStringLiteral("editors"), QJsonArray::fromStringList(editors)},
            {QStringLiteral("available"), available},
            {QStringLiteral("scanState"), scanState},
            {QStringLiteral("reason"), reason}};
}

AudioPluginDescriptor AudioPluginDescriptor::fromJson(const QJsonObject &object)
{
    AudioPluginDescriptor out;
    out.pluginId = object.value(QStringLiteral("pluginId")).toString();
    out.format = object.value(QStringLiteral("format")).toString(QStringLiteral("CLAP"));
    out.name = object.value(QStringLiteral("name")).toString();
    out.vendor = object.value(QStringLiteral("vendor")).toString();
    out.version = object.value(QStringLiteral("version")).toString();
    out.description = object.value(QStringLiteral("description")).toString();
    out.url = object.value(QStringLiteral("url")).toString();
    out.path = object.value(QStringLiteral("path")).toString();
    out.features = strings(object.value(QStringLiteral("features")));
    out.audioInputs = object.value(QStringLiteral("audioInputs")).toInt();
    out.audioOutputs = object.value(QStringLiteral("audioOutputs")).toInt();
    out.noteInputs = object.value(QStringLiteral("noteInputs")).toInt();
    out.parameterCount = object.value(QStringLiteral("parameterCount")).toInt();
    out.state = object.value(QStringLiteral("state")).toBool();
    out.editors = strings(object.value(QStringLiteral("editors")));
    out.available = object.value(QStringLiteral("available")).toBool(true);
    out.scanState = object.value(QStringLiteral("scanState")).toString(QStringLiteral("available"));
    out.reason = object.value(QStringLiteral("reason")).toString();
    return out;
}

namespace AudioPlugins {

QString newInstanceId()
{
    return QStringLiteral("plugin:") + QUuid::createUuid().toString(QUuid::WithoutBraces);
}

bool isInstanceId(const QString &id)
{
    return id.startsWith(QLatin1String("plugin:")) && !QUuid::fromString(id.mid(7)).isNull();
}

QString parameterId(const QString &instanceId, quint32 clapParameterId)
{
    return QStringLiteral("%1/%2").arg(instanceId).arg(clapParameterId);
}

QString hostExecutable()
{
    const QString configured = qEnvironmentVariable("QINDATK_PLUGIN_HOST");
    if (!configured.isEmpty() && QFileInfo(configured).isExecutable()) return configured;
    // AGENT-CONTRACT (src/CMakeLists.txt, D-202): the host is found relative
    // to libqindatk itself, so a moved install keeps its host and no build or
    // install path is compiled in. A build-tree library sits in a directory
    // CMake gave a CMakeFiles/ folder; an installed one does not. The build
    // tree's host comes first: a build must not pick up the host of an older
    // installed QindaTK (the protocol check would refuse it anyway).
    Dl_info info{};
    if (::dladdr(reinterpret_cast<const void *>(&hostExecutable), &info) && info.dli_fname) {
        const QDir libDir = QFileInfo(QString::fromLocal8Bit(info.dli_fname)).absoluteDir();
        const QString relative = libDir.exists(QStringLiteral("CMakeFiles")) ? QStringLiteral(QINDATK_PLUGIN_HOST_BUILD_REL)
                                                                             : QStringLiteral(QINDATK_PLUGIN_HOST_INSTALL_REL);
        const QString host = QDir::cleanPath(libDir.filePath(relative));
        if (QFileInfo(host).isExecutable()) return host;
    }
    return QStandardPaths::findExecutable(QStringLiteral("qindatk-plugin-host"));
}

QStringList defaultSearchPaths()
{
    QStringList paths;
    for (const QString &entry : qEnvironmentVariable("CLAP_PATH").split(QLatin1Char(':'), Qt::SkipEmptyParts))
        paths.push_back(entry);
    paths << QDir::home().filePath(QStringLiteral(".clap")) << QStringLiteral("/usr/lib/clap")
          << QStringLiteral("/usr/lib64/clap") << QStringLiteral("/usr/local/lib/clap");
    paths.removeDuplicates();
    return paths;
}

QVariantList formats()
{
    const bool host = !hostExecutable().isEmpty();
    const QVariantMap clap{
        {QStringLiteral("format"), QStringLiteral("CLAP")},
        {QStringLiteral("sdk"), QStringLiteral("CLAP 1.2.9 headers (MIT), vendored")},
        {QStringLiteral("available"), host},
        {QStringLiteral("reason"), host ? QString() : QStringLiteral("qindatk-plugin-host is not installed")},
        {QStringLiteral("isolation"), QStringLiteral("process")},
        {QStringLiteral("discovery"), QStringLiteral("*.clap under the CLAP search paths, cached by path, size and time")},
        {QStringLiteral("audio"), QStringLiteral("main port, mono or stereo, 32-bit float, blocks of at most 256 frames")},
        {QStringLiteral("notes"), QStringLiteral("CLAP note on/off from MIDI tracks")},
        {QStringLiteral("parameters"), QStringLiteral("automatable parameters as automation parameters, applied per block")},
        {QStringLiteral("state"), true},
        {QStringLiteral("latency"), QStringLiteral("reported latency is compensated")},
        {QStringLiteral("editor"), QStringLiteral("embedded x11 under xcb, floating otherwise")},
        {QStringLiteral("transport"), false}};
    const QVariantMap lv2{{QStringLiteral("format"), QStringLiteral("LV2")},
                          {QStringLiteral("available"), false},
                          {QStringLiteral("reason"), QStringLiteral("No LV2 adapter: lilv is not a dependency of this build")}};
    const QVariantMap vst3{{QStringLiteral("format"), QStringLiteral("VST3")},
                           {QStringLiteral("available"), false},
                           {QStringLiteral("reason"), QStringLiteral("No VST3 adapter: the SDK is not present")}};
    return {clap, lv2, vst3};
}

} // namespace AudioPlugins
} // namespace QindaTK
