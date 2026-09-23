// SPDX-License-Identifier: LGPL-3.0-or-later
#include "settings_model.h"

#include <QThread>
#include <algorithm>

namespace QindaTK {

SettingsModel::SettingsModel(QObject *parent) : QObject(parent) {}

QString SettingsModel::scopedKey(const QString &key, SettingScope scope,
                                 const QString &instanceId)
{
    if (key.isEmpty() || key.contains(QLatin1Char(':')))
        return {};
    switch (scope) {
    case SettingScope::App:
        return instanceId.isEmpty() ? QStringLiteral("app:") + key : QString{};
    case SettingScope::Document:
        return instanceId.isEmpty() || instanceId.contains(QLatin1Char(':'))
            ? QString{} : QStringLiteral("document:") + instanceId + QLatin1Char(':') + key;
    case SettingScope::Workspace:
        return instanceId.isEmpty() || instanceId.contains(QLatin1Char(':'))
            ? QString{} : QStringLiteral("workspace:") + instanceId + QLatin1Char(':') + key;
    }
    return {};
}

std::optional<SettingsModel::ParsedKey> SettingsModel::parseScopedKey(const QString &text)
{
    const auto parts = text.split(QLatin1Char(':'));
    if (parts.size() == 2 && parts[0] == QLatin1String("app") && !parts[1].isEmpty())
        return ParsedKey{SettingScope::App, {}, parts[1]};
    if (parts.size() == 3 && !parts[1].isEmpty() && !parts[2].isEmpty()) {
        if (parts[0] == QLatin1String("document"))
            return ParsedKey{SettingScope::Document, parts[1], parts[2]};
        if (parts[0] == QLatin1String("workspace"))
            return ParsedKey{SettingScope::Workspace, parts[1], parts[2]};
    }
    return std::nullopt;
}

QString SettingsModel::entryId(const QString &key, SettingScope scope,
                               const QString &instanceId) const
{
    return scopedKey(key, scope, instanceId);
}

bool SettingsModel::onOwnerThread() const
{
    return QThread::currentThread() == thread();
}

void SettingsModel::setError(const OperationError &error)
{
    m_error = error;
    emit stateChanged();
}

OperationError SettingsModel::validate(const Entry &entry, const QVariant &value) const
{
    const auto key = scopedKey(entry.descriptor.key, entry.descriptor.scope, entry.instanceId);
    if (!value.isValid() || value.metaType() != entry.descriptor.valueType)
        return {ErrorCode::Validation, QStringLiteral("Value has the wrong type"), key, {}};
    if (entry.descriptor.validate)
        return entry.descriptor.validate(value);
    return {};
}

bool SettingsModel::registerSetting(const SettingDescriptor &descriptor,
                                    const QString &instanceId)
{
    if (!onOwnerThread())
        return false;
    const auto key = scopedKey(descriptor.key, descriptor.scope, instanceId);
    if (key.isEmpty() || !descriptor.valueType.isValid() ||
        !descriptor.defaultValue.isValid() ||
        descriptor.defaultValue.metaType() != descriptor.valueType || m_entries.contains(key)) {
        setError({ErrorCode::InvalidArgument, QStringLiteral("Invalid or duplicate setting descriptor"), key, {}});
        return false;
    }
    Entry entry{descriptor, instanceId, descriptor.defaultValue};
    const auto problem = validate(entry, entry.committed);
    if (problem) {
        setError(problem);
        return false;
    }
    m_entries.insert(key, std::move(entry));
    if (descriptor.restartRequired)
        m_restartBaseline.insert(key, descriptor.defaultValue);
    if (m_staging)
        m_staged.insert(key, descriptor.defaultValue);
    m_error = {};
    emit valuesChanged();
    emit stateChanged();
    return true;
}

bool SettingsModel::beginStage()
{
    if (!onOwnerThread() || m_staging)
        return false;
    m_staged.clear();
    for (auto it = m_entries.cbegin(); it != m_entries.cend(); ++it)
        m_staged.insert(it.key(), it->committed);
    m_staging = true;
    m_error = {};
    emit stateChanged();
    return true;
}

bool SettingsModel::setStagedValue(const QString &key, const QVariant &value,
                                   SettingScope scope, const QString &instanceId)
{
    if (!onOwnerThread() || !m_staging)
        return false;
    const QString id = entryId(key, scope, instanceId);
    auto it = m_entries.find(id);
    if (it == m_entries.end()) {
        setError({ErrorCode::Unsupported, QStringLiteral("Unknown setting"), id, {}});
        return false;
    }
    const auto problem = validate(it.value(), value);
    if (problem) {
        setError(problem);
        emit validationFailed(id, problem.message);
        return false;
    }
    m_staged.insert(id, value);
    m_error = {};
    emit stateChanged();
    return true;
}

bool SettingsModel::apply()
{
    if (!onOwnerThread() || !m_staging)
        return false;
    for (auto it = m_entries.cbegin(); it != m_entries.cend(); ++it) {
        const auto problem = validate(it.value(), m_staged.value(it.key(), it->committed));
        if (problem) {
            setError(problem);
            emit validationFailed(it.key(), problem.message);
            return false;
        }
    }
    bool changed = false;
    for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
        const auto next = m_staged.value(it.key(), it->committed);
        changed |= next != it->committed;
        it->committed = next;
    }
    m_staged.clear();
    m_staging = false;
    m_error = {};
    emit stateChanged();
    if (changed)
        emit valuesChanged();
    emit applied();
    return true;
}

void SettingsModel::cancel()
{
    if (!onOwnerThread() || !m_staging)
        return;
    m_staged.clear();
    m_staging = false;
    m_error = {};
    emit stateChanged();
}

bool SettingsModel::resetKey(const QString &key, SettingScope scope,
                             const QString &instanceId)
{
    if (!onOwnerThread())
        return false;
    const QString id = entryId(key, scope, instanceId);
    auto it = m_entries.find(id);
    if (it == m_entries.end()) {
        setError({ErrorCode::Unsupported, QStringLiteral("Unknown setting"), id, {}});
        return false;
    }
    const auto defaultValue = it->descriptor.defaultValue;
    const auto problem = validate(it.value(), defaultValue);
    if (problem) {
        setError(problem);
        return false;
    }
    if (m_staging) {
        m_staged.insert(id, defaultValue);
    } else if (it->committed != defaultValue) {
        it->committed = defaultValue;
        emit valuesChanged();
    }
    m_error = {};
    emit stateChanged();
    return true;
}

bool SettingsModel::resetScope(SettingScope scope, const QString &instanceId)
{
    if (!onOwnerThread() || (scope == SettingScope::App ? !instanceId.isEmpty() : instanceId.isEmpty()))
        return false;
    bool found = false;
    bool changed = false;
    for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
        if (it->descriptor.scope != scope || it->instanceId != instanceId)
            continue;
        found = true;
        const auto problem = validate(it.value(), it->descriptor.defaultValue);
        if (problem) {
            setError(problem);
            return false;
        }
    }
    if (!found)
        return false;
    for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
        if (it->descriptor.scope != scope || it->instanceId != instanceId)
            continue;
        if (m_staging) {
            m_staged.insert(it.key(), it->descriptor.defaultValue);
        } else {
            changed |= it->committed != it->descriptor.defaultValue;
            it->committed = it->descriptor.defaultValue;
        }
    }
    m_error = {};
    emit stateChanged();
    if (changed)
        emit valuesChanged();
    return true;
}

void SettingsModel::markRestartBaseline()
{
    if (!onOwnerThread())
        return;
    for (auto it = m_entries.cbegin(); it != m_entries.cend(); ++it) {
        if (it->descriptor.restartRequired)
            m_restartBaseline.insert(it.key(), it->committed);
    }
    emit stateChanged();
}

QVariant SettingsModel::value(const QString &key, SettingScope scope,
                              const QString &instanceId) const
{
    const auto it = m_entries.constFind(entryId(key, scope, instanceId));
    return it == m_entries.cend() ? QVariant{} : it->committed;
}

QVariant SettingsModel::stagedValue(const QString &key, SettingScope scope,
                                    const QString &instanceId) const
{
    const auto id = entryId(key, scope, instanceId);
    const auto it = m_entries.constFind(id);
    if (it == m_entries.cend())
        return {};
    return m_staging ? m_staged.value(id, it->committed) : it->committed;
}

bool SettingsModel::needsRestart() const
{
    for (auto it = m_entries.cbegin(); it != m_entries.cend(); ++it) {
        if (it->descriptor.restartRequired &&
            it->committed != m_restartBaseline.value(it.key(), it->descriptor.defaultValue))
            return true;
    }
    return false;
}

QVariantMap SettingsModel::exportValues() const
{
    QVariantMap result = m_orphaned;
    for (auto it = m_entries.cbegin(); it != m_entries.cend(); ++it)
        result.insert(it.key(), it->committed);
    return result;
}

bool SettingsModel::importValues(const QVariantMap &values)
{
    if (!onOwnerThread() || m_staging)
        return false;
    auto next = m_entries;
    QVariantMap orphaned;
    QStringList unsupported;
    for (auto it = values.cbegin(); it != values.cend(); ++it) {
        auto entry = next.find(it.key());
        if (entry == next.end()) {
            orphaned.insert(it.key(), it.value());
            unsupported.append(it.key());
            continue;
        }
        const auto problem = validate(entry.value(), it.value());
        if (problem) {
            setError(problem);
            emit validationFailed(it.key(), problem.message);
            return false;
        }
        entry->committed = it.value();
    }
    bool changed = orphaned != m_orphaned || next.size() != m_entries.size();
    for (auto it = next.cbegin(); !changed && it != next.cend(); ++it) {
        const auto previous = m_entries.constFind(it.key());
        changed = previous == m_entries.cend() || previous->committed != it->committed;
    }
    m_entries = std::move(next);
    m_orphaned = std::move(orphaned);
    m_error = {};
    emit stateChanged();
    if (changed)
        emit valuesChanged();
    if (!unsupported.isEmpty())
        emit unsupportedKeysImported(unsupported);
    return true;
}

bool SettingsModel::setValue(const QString &scopedKey, const QVariant &value)
{
    const auto parsed = parseScopedKey(scopedKey);
    return parsed && setStagedValue(parsed->key, value, parsed->scope, parsed->instanceId);
}

QVariant SettingsModel::valueForKey(const QString &scopedKey) const
{
    const auto parsed = parseScopedKey(scopedKey);
    return parsed ? value(parsed->key, parsed->scope, parsed->instanceId) : QVariant{};
}

bool SettingsModel::resetSetting(const QString &scopedKey)
{
    const auto parsed = parseScopedKey(scopedKey);
    return parsed && resetKey(parsed->key, parsed->scope, parsed->instanceId);
}

} // namespace QindaTK
