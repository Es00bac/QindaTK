// SPDX-License-Identifier: LGPL-3.0-or-later
#include "extension_registry.h"
#include "capability_registry.h"
#include "extension_provider.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPluginLoader>
#include <QSet>
#include <QThread>
#include <QVersionNumber>
#include <algorithm>

namespace QindaTK {
namespace {
constexpr int manifestSchema = 1;
constexpr int providerAbi = 1;
constexpr qsizetype maxDescriptorBytes = 256 * 1024;
QString backendId(const QString &id) { return QStringLiteral("extension:") + id; }
bool validId(const QString &value)
{
    if (value.isEmpty() || value.size() > 160) return false;
    for (const QChar c : value)
        if (!c.isLetterOrNumber() && c != u'.' && c != u'-' && c != u'_') return false;
    return true;
}
QString versionError(const QString &version)
{
    const auto parsed = QVersionNumber::fromString(version);
    if (parsed.isNull() || parsed.toString() != version)
        return QStringLiteral("version must be a dotted numeric version");
    return {};
}
} // namespace

struct ExtensionRegistry::Record {
    struct Dependency { QString id; QVersionNumber minimum; };
    struct Capability { QString feature; QString operation; };
    QString id;
    QString version;
    QString descriptorPath;
    QString libraryPath;
    QString error;
    QStringList operations;
    QVector<Dependency> dependencies;
    QVector<Capability> requiresCapabilities;
    QVector<Capability> providesCapabilities;
    QVariantMap savedState;
    bool descriptorValid = false;
    bool loaded = false;
    std::unique_ptr<QPluginLoader> loader;
    ExtensionProvider *provider = nullptr;
};

ExtensionRegistry::ExtensionRegistry(QObject *parent) : QObject(parent) {}
ExtensionRegistry::~ExtensionRegistry()
{
    // Stop dependents before their providers. No caller-owned document data
    // belongs to this registry, so closing it never rewrites that data.
    while (true) {
        std::shared_ptr<Record> next;
        for (const auto &candidate : m_records) {
            if (!candidate->loaded) continue;
            bool hasDependent = false;
            for (const auto &other : m_records) {
                if (!other->loaded || other == candidate) continue;
                for (const auto &dependency : other->dependencies)
                    if (dependency.id == candidate->id) hasDependent = true;
            }
            if (!hasDependent) { next = candidate; break; }
        }
        if (!next) break;
        auto &record = *next;
        record.savedState = record.provider->saveState();
        record.provider->stop();
        retract(record);
        record.provider = nullptr;
        record.loaded = false;
        record.loader->unload();
    }
}

void ExtensionRegistry::setCapabilities(CapabilityRegistry *capabilities)
{
    Q_ASSERT(thread() == QThread::currentThread());
    if (m_capabilities == capabilities) return;
    QObject::disconnect(m_capabilityChange);
    if (m_capabilities)
        for (const auto &record : m_records)
            if (record->loaded) m_capabilities->removeBackend(backendId(record->id));
    m_capabilities = capabilities;
    if (m_capabilities)
        m_capabilityChange = connect(m_capabilities, &CapabilityRegistry::generationChanged,
                                     this, &ExtensionRegistry::extensionsChanged);
    if (m_capabilities)
        for (const auto &record : m_records)
            if (record->loaded)
                for (const auto &capability : record->providesCapabilities)
                    m_capabilities->publish({capability.feature, capability.operation,
                                             backendId(record->id), record->version, true, {}, 0});
    emit capabilitiesChanged();
    emit extensionsChanged();
}

QString ExtensionRegistry::diagnostic(const Record &record) const
{
    if (!record.descriptorValid) return record.error.isEmpty()
        ? QStringLiteral("Descriptor is not installed") : record.error;
    if (!QFileInfo::exists(record.libraryPath))
        return QStringLiteral("Extension library is missing: %1").arg(record.libraryPath);
    for (const auto &dependency : record.dependencies) {
        const auto match = m_records.value(dependency.id);
        if (!match || !match->loaded)
            return QStringLiteral("Required extension is not loaded: %1").arg(dependency.id);
        if (QVersionNumber::compare(QVersionNumber::fromString(match->version),
                                    dependency.minimum) < 0)
            return QStringLiteral("Required extension %1 needs version %2")
                .arg(dependency.id, dependency.minimum.toString());
    }
    for (const auto &capability : record.requiresCapabilities)
        if (!m_capabilities || !m_capabilities->isAvailable(capability.feature,
                                                             capability.operation))
            return QStringLiteral("Required capability unavailable: %1/%2")
                .arg(capability.feature, capability.operation);
    return record.error;
}

QVariantMap ExtensionRegistry::view(const Record &record) const
{
    QVariantList dependencies;
    for (const auto &dependency : record.dependencies)
        dependencies.append(QVariantMap{{QStringLiteral("id"), dependency.id},
                                        {QStringLiteral("minVersion"), dependency.minimum.toString()}});
    const auto capabilityList = [](const QVector<Record::Capability> &capabilities) {
        QVariantList list;
        for (const auto &capability : capabilities)
            list.append(QVariantMap{{QStringLiteral("featureId"), capability.feature},
                                    {QStringLiteral("operationId"), capability.operation}});
        return list;
    };
    const QString issue = diagnostic(record);
    return {{QStringLiteral("id"), record.id},
            {QStringLiteral("version"), record.version},
            {QStringLiteral("descriptorPath"), record.descriptorPath},
            {QStringLiteral("operations"), record.operations},
            {QStringLiteral("dependencies"), dependencies},
            {QStringLiteral("requiresCapabilities"), capabilityList(record.requiresCapabilities)},
            {QStringLiteral("providesCapabilities"), capabilityList(record.providesCapabilities)},
            {QStringLiteral("installed"), record.descriptorValid},
            {QStringLiteral("loaded"), record.loaded},
            {QStringLiteral("diagnostic"), issue},
            {QStringLiteral("state"), record.loaded ? record.provider->saveState() : record.savedState}};
}

QVariantList ExtensionRegistry::extensions() const
{
    QVariantList result;
    for (const auto &record : m_records) result.append(view(*record));
    return result;
}

QVariantMap ExtensionRegistry::extension(const QString &id) const
{
    const auto record = m_records.value(id);
    return record ? view(*record) : QVariantMap{{QStringLiteral("id"), id},
                                               {QStringLiteral("installed"), false},
                                               {QStringLiteral("loaded"), false},
                                               {QStringLiteral("diagnostic"),
                                                QStringLiteral("Extension is unknown")}};
}

bool ExtensionRegistry::registerDescriptor(const QString &path)
{
    Q_ASSERT(thread() == QThread::currentThread());
    const QFileInfo info(path);
    if (!info.isAbsolute() || !info.isFile()) return false;
    QFile file(info.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly) || file.size() > maxDescriptorBytes) return false;
    QJsonParseError parseError;
    const auto doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) return false;
    const QJsonObject object = doc.object();
    const QString id = object.value(QStringLiteral("id")).toString();
    if (!validId(id)) return false;
    const auto old = m_records.value(id);
    if (old && old->loaded) return false;
    auto record = std::make_shared<Record>();
    record->id = id;
    record->version = object.value(QStringLiteral("version")).toString();
    record->descriptorPath = info.canonicalFilePath();
    if (old) record->savedState = old->savedState;
    const auto fail = [&](const QString &message) {
        record->error = message;
        m_records.insert(id, record);
        emit extensionsChanged();
        return false;
    };
    if (object.value(QStringLiteral("schemaVersion")).toInt() != manifestSchema)
        return fail(QStringLiteral("Unsupported descriptor schema version"));
    if (object.value(QStringLiteral("abiVersion")).toInt() != providerAbi)
        return fail(QStringLiteral("Incompatible extension ABI version"));
    if (const QString issue = versionError(record->version); !issue.isEmpty()) return fail(issue);
    const QString library = object.value(QStringLiteral("library")).toString();
    if (library.isEmpty()) return fail(QStringLiteral("Missing extension library path"));
    record->libraryPath = QDir(info.absolutePath()).absoluteFilePath(library);
    const auto operations = object.value(QStringLiteral("operations"));
    if (!operations.isArray()) return fail(QStringLiteral("operations must be an array"));
    QSet<QString> unique;
    for (const auto &entry : operations.toArray()) {
        const QString operation = entry.toString();
        if (!validId(operation) || unique.contains(operation))
            return fail(QStringLiteral("Invalid or duplicate operation"));
        unique.insert(operation);
        record->operations.append(operation);
    }
    const auto dependencies = object.value(QStringLiteral("dependencies"));
    if (!dependencies.isUndefined() && !dependencies.isArray())
        return fail(QStringLiteral("dependencies must be an array"));
    for (const auto &entry : dependencies.toArray()) {
        const auto dependency = entry.toObject();
        const QString dependentId = dependency.value(QStringLiteral("id")).toString();
        const QString version = dependency.value(QStringLiteral("minVersion")).toString();
        if (!validId(dependentId) || dependentId == id || !versionError(version).isEmpty())
            return fail(QStringLiteral("Invalid extension dependency"));
        record->dependencies.append({dependentId, QVersionNumber::fromString(version)});
    }
    const auto parseCapabilities = [&](const QString &key, QVector<Record::Capability> *target) {
        const auto value = object.value(key);
        if (!value.isUndefined() && !value.isArray()) return false;
        for (const auto &entry : value.toArray()) {
            if (!entry.isObject()) return false;
            const auto cap = entry.toObject();
            const QString feature = cap.value(QStringLiteral("featureId")).toString();
            const QString operation = cap.value(QStringLiteral("operationId")).toString();
            if (!validId(feature) || !validId(operation)) return false;
            target->append({feature, operation});
        }
        return true;
    };
    if (!parseCapabilities(QStringLiteral("requiresCapabilities"), &record->requiresCapabilities)
        || !parseCapabilities(QStringLiteral("providesCapabilities"), &record->providesCapabilities))
        return fail(QStringLiteral("Invalid capability declaration"));
    record->descriptorValid = true;
    m_records.insert(id, record);
    emit extensionsChanged();
    return true;
}

bool ExtensionRegistry::load(const QString &id)
{
    Q_ASSERT(thread() == QThread::currentThread());
    const auto record = m_records.value(id);
    if (!record) return false;
    if (record->loaded) return true;
    record->error.clear();
    const QString issue = diagnostic(*record);
    if (!issue.isEmpty()) { emit extensionsChanged(); return false; }
    auto loader = std::make_unique<QPluginLoader>(record->libraryPath);
    loader->setLoadHints({});
    QObject *object = loader->instance();
    if (!object) { record->error = loader->errorString(); emit extensionsChanged(); return false; }
    auto *provider = qobject_cast<ExtensionProvider *>(object);
    if (!provider || provider->extensionId() != id) {
        record->error = QStringLiteral("Plugin interface or extension ID does not match descriptor");
        loader->unload();
        emit extensionsChanged();
        return false;
    }
    QString error;
    if (!provider->start(record->savedState, &error)) {
        record->error = error.isEmpty() ? QStringLiteral("Extension start failed") : error;
        loader->unload();
        emit extensionsChanged();
        return false;
    }
    record->provider = provider;
    record->loader = std::move(loader);
    record->loaded = true;
    if (m_capabilities)
        for (const auto &capability : record->providesCapabilities)
            m_capabilities->publish({capability.feature, capability.operation,
                                     backendId(id), record->version, true, {}, 0});
    emit extensionsChanged();
    return true;
}

void ExtensionRegistry::retract(Record &record)
{
    if (m_capabilities) m_capabilities->removeBackend(backendId(record.id));
}

bool ExtensionRegistry::unload(const QString &id)
{
    Q_ASSERT(thread() == QThread::currentThread());
    const auto record = m_records.value(id);
    if (!record || !record->loaded) return false;
    for (const auto &other : m_records) {
        if (!other->loaded || other->id == id) continue;
        for (const auto &dependency : other->dependencies)
            if (dependency.id == id) {
                record->error = QStringLiteral("Loaded extension depends on this extension: %1")
                    .arg(other->id);
                emit extensionsChanged();
                return false;
            }
    }
    record->savedState = record->provider->saveState();
    record->provider->stop();
    retract(*record);
    record->provider = nullptr;
    record->loaded = false;
    const bool unloaded = record->loader->unload();
    if (!unloaded) record->error = record->loader->errorString();
    record->loader.reset();
    emit extensionsChanged();
    return unloaded;
}

QVariantMap ExtensionRegistry::invoke(const QString &id, const QString &operation,
                                       const QVariantMap &arguments)
{
    Q_ASSERT(thread() == QThread::currentThread());
    const auto record = m_records.value(id);
    if (!record || !record->loaded)
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("error"), QStringLiteral("Extension is not loaded")}};
    if (!record->operations.contains(operation))
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("error"), QStringLiteral("Operation is not declared")}};
    return record->provider->invoke(operation, arguments);
}

QVariantList ExtensionRegistry::saveState() const
{
    QVariantList result;
    for (const auto &record : m_records)
        result.append(QVariantMap{{QStringLiteral("id"), record->id},
                                  {QStringLiteral("descriptorPath"), record->descriptorPath},
                                  {QStringLiteral("version"), record->version},
                                  {QStringLiteral("state"), record->loaded
                                      ? record->provider->saveState() : record->savedState}});
    return result;
}

bool ExtensionRegistry::restoreState(const QVariantList &snapshot)
{
    Q_ASSERT(thread() == QThread::currentThread());
    if (std::any_of(m_records.cbegin(), m_records.cend(),
                    [](const auto &record) { return record->loaded; })) return false;
    QMap<QString, std::shared_ptr<Record>> restored;
    for (const auto &value : snapshot) {
        const auto item = value.toMap();
        const QString id = item.value(QStringLiteral("id")).toString();
        if (!validId(id) || restored.contains(id)) return false;
        auto record = std::make_shared<Record>();
        record->id = id;
        record->version = item.value(QStringLiteral("version")).toString();
        record->descriptorPath = item.value(QStringLiteral("descriptorPath")).toString();
        record->savedState = item.value(QStringLiteral("state")).toMap();
        restored.insert(id, record);
    }
    m_records = std::move(restored);
    emit extensionsChanged();
    return true;
}

} // namespace QindaTK
