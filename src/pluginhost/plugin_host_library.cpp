// SPDX-License-Identifier: LGPL-3.0-or-later
// ClapLibrary: loading one CLAP binary and describing its plugins (scan).
#include "plugin_host_clap.h"

#include <QFile>
#include <dlfcn.h>

namespace QindaTK::PluginHost {
namespace {

QString text(const char *value)
{
    return value ? QString::fromUtf8(value) : QString();
}

// The scan host offers no extension and ignores every request: a plugin is
// created, initialized, asked about its ports, parameters and editor, and
// destroyed.
const void *scanExtension(const clap_host_t *, const char *) { return nullptr; }
void scanRequest(const clap_host_t *) {}

clap_host_t scanHost()
{
    clap_host_t host{};
    host.clap_version = CLAP_VERSION;
    host.host_data = nullptr;
    host.name = "QindaTK plugin scanner";
    host.vendor = "QindaTK";
    host.url = "";
    host.version = "1";
    host.get_extension = scanExtension;
    host.request_restart = scanRequest;
    host.request_process = scanRequest;
    host.request_callback = scanRequest;
    return host;
}

QJsonObject ports(const clap_plugin_t *plugin, bool input)
{
    QJsonArray list;
    int mainChannels = 0;
    const auto *ext = static_cast<const clap_plugin_audio_ports_t *>(plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS));
    const quint32 count = ext ? ext->count(plugin, input) : 0;
    for (quint32 i = 0; i < count; ++i) {
        clap_audio_port_info_t info{};
        if (!ext->get(plugin, i, input, &info)) continue;
        const bool main = (info.flags & CLAP_AUDIO_PORT_IS_MAIN) != 0;
        if (main || (i == 0 && mainChannels == 0)) mainChannels = int(info.channel_count);
        list.push_back(QJsonObject{{QStringLiteral("name"), QString::fromUtf8(info.name)},
                                   {QStringLiteral("channels"), int(info.channel_count)},
                                   {QStringLiteral("main"), main},
                                   {QStringLiteral("type"), text(info.port_type)}});
    }
    return {{QStringLiteral("ports"), list}, {QStringLiteral("mainChannels"), mainChannels}};
}

} // namespace

ClapLibrary::~ClapLibrary()
{
    if (m_initialized && m_entry) m_entry->deinit();
    if (m_handle) dlclose(m_handle);
}

bool ClapLibrary::open(const QString &path, QString *error)
{
    const QByteArray native = QFile::encodeName(path);
    m_handle = dlopen(native.constData(), RTLD_NOW | RTLD_LOCAL);
    if (!m_handle) {
        const char *reason = dlerror();
        *error = QStringLiteral("The binary cannot be loaded: %1").arg(text(reason));
        return false;
    }
    m_entry = static_cast<const clap_plugin_entry_t *>(dlsym(m_handle, "clap_entry"));
    if (!m_entry) {
        *error = QStringLiteral("The binary is not a CLAP plugin (it exports no clap_entry)");
        return false;
    }
    if (!clap_version_is_compatible(m_entry->clap_version)) {
        *error = QStringLiteral("CLAP %1.%2.%3 is not supported")
                     .arg(m_entry->clap_version.major)
                     .arg(m_entry->clap_version.minor)
                     .arg(m_entry->clap_version.revision);
        return false;
    }
    if (!m_entry->init || !m_entry->init(native.constData())) {
        *error = QStringLiteral("The plugin binary refused to initialise");
        return false;
    }
    m_initialized = true;
    m_factory = static_cast<const clap_plugin_factory_t *>(m_entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    if (!m_factory) {
        *error = QStringLiteral("The binary offers no CLAP plugin factory");
        return false;
    }
    return true;
}

QJsonArray ClapLibrary::describe() const
{
    QJsonArray out;
    if (!m_factory) return out;
    const clap_host_t host = scanHost();
    const quint32 count = m_factory->get_plugin_count(m_factory);
    for (quint32 i = 0; i < count; ++i) {
        const clap_plugin_descriptor_t *descriptor = m_factory->get_plugin_descriptor(m_factory, i);
        if (!descriptor || !descriptor->id) continue;
        QJsonArray features;
        for (const char *const *feature = descriptor->features; feature && *feature; ++feature)
            features.push_back(QString::fromUtf8(*feature));
        QJsonObject entry{{QStringLiteral("pluginId"), text(descriptor->id)},
                          {QStringLiteral("name"), text(descriptor->name)},
                          {QStringLiteral("vendor"), text(descriptor->vendor)},
                          {QStringLiteral("version"), text(descriptor->version)},
                          {QStringLiteral("description"), text(descriptor->description)},
                          {QStringLiteral("url"), text(descriptor->url)},
                          {QStringLiteral("features"), features},
                          {QStringLiteral("clapVersion"), QStringLiteral("%1.%2.%3")
                                                               .arg(descriptor->clap_version.major)
                                                               .arg(descriptor->clap_version.minor)
                                                               .arg(descriptor->clap_version.revision)}};
        const clap_plugin_t *plugin = m_factory->create_plugin(m_factory, &host, descriptor->id);
        if (!plugin || !plugin->init(plugin)) {
            if (plugin) plugin->destroy(plugin);
            entry.insert(QStringLiteral("error"), QStringLiteral("The plugin failed to initialise"));
            out.push_back(entry);
            continue;
        }
        const QJsonObject inputs = ports(plugin, true);
        const QJsonObject outputs = ports(plugin, false);
        entry.insert(QStringLiteral("audioInputs"), inputs.value(QStringLiteral("mainChannels")));
        entry.insert(QStringLiteral("audioOutputs"), outputs.value(QStringLiteral("mainChannels")));
        entry.insert(QStringLiteral("inputPorts"), inputs.value(QStringLiteral("ports")));
        entry.insert(QStringLiteral("outputPorts"), outputs.value(QStringLiteral("ports")));
        const auto *notes = static_cast<const clap_plugin_note_ports_t *>(plugin->get_extension(plugin, CLAP_EXT_NOTE_PORTS));
        entry.insert(QStringLiteral("noteInputs"), notes ? int(notes->count(plugin, true)) : 0);
        const auto *params = static_cast<const clap_plugin_params_t *>(plugin->get_extension(plugin, CLAP_EXT_PARAMS));
        entry.insert(QStringLiteral("parameterCount"), params ? int(params->count(plugin)) : 0);
        const auto *state = plugin->get_extension(plugin, CLAP_EXT_STATE);
        entry.insert(QStringLiteral("state"), state != nullptr);
        const auto *gui = static_cast<const clap_plugin_gui_t *>(plugin->get_extension(plugin, CLAP_EXT_GUI));
        QJsonArray editors;
        if (gui) {
            if (gui->is_api_supported(plugin, CLAP_WINDOW_API_X11, false)) editors.push_back(QStringLiteral("x11"));
            if (gui->is_api_supported(plugin, CLAP_WINDOW_API_X11, true)) editors.push_back(QStringLiteral("x11-floating"));
            if (gui->is_api_supported(plugin, CLAP_WINDOW_API_WAYLAND, true))
                editors.push_back(QStringLiteral("wayland-floating"));
        }
        entry.insert(QStringLiteral("editors"), editors);
        plugin->destroy(plugin);
        out.push_back(entry);
    }
    return out;
}

} // namespace QindaTK::PluginHost
