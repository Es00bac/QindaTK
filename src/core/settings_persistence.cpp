// SPDX-License-Identifier: LGPL-3.0-or-later
#include "settings_persistence.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QDir>
#include <cmath>
#include <limits>

namespace QindaTK {
namespace {
constexpr qsizetype kMaximumSettingsBytes = 4 * 1024 * 1024;

std::optional<QJsonObject> encodeValue(const QVariant &value)
{
    const int type = value.metaType().id();
    if (type == QMetaType::Bool)
        return QJsonObject{{QStringLiteral("type"), QStringLiteral("bool")},
                           {QStringLiteral("value"), value.toBool()}};
    if (type == QMetaType::Int || type == QMetaType::UInt || type == QMetaType::LongLong
        || type == QMetaType::ULongLong) {
        const QString kind = type == QMetaType::Int ? QStringLiteral("int")
            : type == QMetaType::UInt ? QStringLiteral("uint")
            : type == QMetaType::LongLong ? QStringLiteral("longlong")
            : QStringLiteral("ulonglong");
        return QJsonObject{{QStringLiteral("type"), kind},
                           {QStringLiteral("value"), value.toString()}};
    }
    if (type == QMetaType::Double && std::isfinite(value.toDouble()))
        return QJsonObject{{QStringLiteral("type"), QStringLiteral("double")},
                           {QStringLiteral("value"), value.toDouble()}};
    if (type == QMetaType::QString)
        return QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                           {QStringLiteral("value"), value.toString()}};
    if (type == QMetaType::QByteArray)
        return QJsonObject{{QStringLiteral("type"), QStringLiteral("bytes")},
                           {QStringLiteral("value"), QString::fromLatin1(value.toByteArray().toBase64())}};
    if (type == QMetaType::QStringList)
        return QJsonObject{{QStringLiteral("type"), QStringLiteral("strings")},
                           {QStringLiteral("value"), QJsonArray::fromStringList(value.toStringList())}};
    return std::nullopt;
}

std::optional<QVariant> decodeValue(const QJsonValue &item)
{
    if (!item.isObject()) return std::nullopt;
    const QJsonObject record = item.toObject();
    if (record.size() != 2 || !record.value(QStringLiteral("type")).isString()
        || !record.contains(QStringLiteral("value"))) return std::nullopt;
    const QString kind = record.value(QStringLiteral("type")).toString();
    const QJsonValue raw = record.value(QStringLiteral("value"));
    if (kind == QStringLiteral("bool") && raw.isBool()) return QVariant(raw.toBool());
    if (kind == QStringLiteral("string") && raw.isString()) return QVariant(raw.toString());
    if (kind == QStringLiteral("double") && raw.isDouble()
        && std::isfinite(raw.toDouble())) return QVariant(raw.toDouble());
    if (kind == QStringLiteral("bytes") && raw.isString()) {
        const QByteArray source = raw.toString().toLatin1();
        const auto decoded = QByteArray::fromBase64Encoding(source,
            QByteArray::AbortOnBase64DecodingErrors);
        if (decoded && decoded.decoded.toBase64() == source) return QVariant(decoded.decoded);
    }
    if (kind == QStringLiteral("strings") && raw.isArray()) {
        QStringList result;
        for (const QJsonValue &value : raw.toArray()) {
            if (!value.isString()) return std::nullopt;
            result.append(value.toString());
        }
        return QVariant(result);
    }
    if (raw.isString()) {
        const QString text = raw.toString();
        bool ok = false;
        if (kind == QStringLiteral("int")) {
            const int number = text.toInt(&ok);
            if (ok && QString::number(number) == text) return QVariant(number);
        } else if (kind == QStringLiteral("uint")) {
            const uint number = text.toUInt(&ok);
            if (ok && QString::number(number) == text) return QVariant(number);
        } else if (kind == QStringLiteral("longlong")) {
            const qlonglong number = text.toLongLong(&ok);
            if (ok && QString::number(number) == text) return QVariant(number);
        } else if (kind == QStringLiteral("ulonglong")) {
            const qulonglong number = text.toULongLong(&ok);
            if (ok && QString::number(number) == text) return QVariant(number);
        }
    }
    return std::nullopt;
}

std::optional<QVariant> migrateLegacy(const QJsonValue &raw)
{
    if (raw.isBool()) return QVariant(raw.toBool());
    if (raw.isString()) return QVariant(raw.toString());
    if (raw.isDouble()) {
        const double value = raw.toDouble();
        if (!std::isfinite(value)) return std::nullopt;
        if (std::floor(value) == value && value >= std::numeric_limits<int>::min()
            && value <= std::numeric_limits<int>::max()) return QVariant(int(value));
        return QVariant(value);
    }
    return std::nullopt;
}
} // namespace

SettingsPersistence::SettingsPersistence(QObject *parent)
    : QObject(parent), m_store(std::make_shared<AtomicStateStore>())
{
    m_path = QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation))
                 .filePath(QStringLiteral("settings.json"));
}

void SettingsPersistence::setPath(const QString &path)
{
    if (path == m_path) return;
    m_path = path;
    emit pathChanged();
}

void SettingsPersistence::setStore(StateStorePtr store)
{
    if (store) m_store = std::move(store);
}

void SettingsPersistence::fail(const OperationError &error)
{
    const QString next = error.message;
    if (next == m_lastError) return;
    m_lastError = next;
    emit lastErrorChanged();
}

QByteArray SettingsPersistence::encode(const QVariantMap &values,
                                       const QJsonObject &extensions,
                                       OperationError *error)
{
    QJsonObject records;
    for (auto it = values.cbegin(); it != values.cend(); ++it) {
        const auto encoded = encodeValue(it.value());
        if (!encoded) {
            if (error) *error = {ErrorCode::Unsupported,
                QStringLiteral("Unsupported settings value type"), it.key(), {}};
            return {};
        }
        records.insert(it.key(), *encoded);
    }
    return QJsonDocument(QJsonObject{{QStringLiteral("version"), 2},
        {QStringLiteral("values"), records},
        {QStringLiteral("extensions"), extensions}}).toJson(QJsonDocument::Compact);
}

std::optional<QVariantMap> SettingsPersistence::decode(const QByteArray &bytes,
                                                        QJsonObject *extensions,
                                                        OperationError *error)
{
    QJsonParseError parse{};
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) *error = {ErrorCode::Validation, QStringLiteral("Invalid settings JSON"), {}, {}};
        return std::nullopt;
    }
    const QJsonObject root = document.object();
    const QJsonValue version = root.value(QStringLiteral("version"));
    if (!version.isDouble() || (version.toInt() != 1 && version.toInt() != 2)
        || version.toDouble() != version.toInt()
        || !root.value(QStringLiteral("values")).isObject()
        || (root.contains(QStringLiteral("extensions"))
            && !root.value(QStringLiteral("extensions")).isObject())) {
        if (error) *error = {ErrorCode::Unsupported,
            QStringLiteral("Unsupported settings schema"), QStringLiteral("version"), {}};
        return std::nullopt;
    }
    QVariantMap values;
    const QJsonObject records = root.value(QStringLiteral("values")).toObject();
    for (auto it = records.begin(); it != records.end(); ++it) {
        const auto decoded = version.toInt() == 1 ? migrateLegacy(it.value())
                                                    : decodeValue(it.value());
        if (!decoded) {
            if (error) *error = {ErrorCode::Validation,
                QStringLiteral("Invalid settings value"), it.key(), {}};
            return std::nullopt;
        }
        values.insert(it.key(), *decoded);
    }
    if (extensions) *extensions = root.value(QStringLiteral("extensions")).toObject();
    if (error) *error = {};
    return values;
}

bool SettingsPersistence::load(SettingsModel *model)
{
    if (!model) { fail({ErrorCode::InvalidArgument, QStringLiteral("Settings model required"), {}, {}}); return false; }
    const StateReadResult read = m_store->read(m_path, kMaximumSettingsBytes);
    if (!read) { fail(read.error); return false; }
    QJsonObject extensions;
    OperationError error;
    auto values = decode(read.bytes, &extensions, &error);
    if (!values) { fail(error); return false; }
    // V1 had no numeric type tags. Registered descriptors resolve integral
    // values that could be either an integer preference or a double slider.
    const QJsonDocument source = QJsonDocument::fromJson(read.bytes);
    if (source.object().value(QStringLiteral("version")).toInt() == 1) {
        for (auto it = values->begin(); it != values->end(); ++it) {
            const QVariant expected = model->valueForKey(it.key());
            if (!expected.isValid()) continue;
            const int type = expected.metaType().id();
            if (type == it.value().metaType().id()) continue;
            if (type == QMetaType::Double && it.value().metaType().id() == QMetaType::Int)
                it.value() = QVariant(double(it.value().toInt()));
            else if (type == QMetaType::Int && it.value().metaType().id() == QMetaType::Double) {
                const double v = it.value().toDouble();
                if (std::floor(v) == v && v >= std::numeric_limits<int>::min()
                    && v <= std::numeric_limits<int>::max()) it.value() = QVariant(int(v));
            }
        }
    }
    if (!model->importValues(*values)) {
        fail({ErrorCode::Validation, model->error(), {}, {}});
        return false;
    }
    model->markRestartBaseline();
    m_extensions = extensions;
    fail({});
    return true;
}

bool SettingsPersistence::save(const SettingsModel *model)
{
    if (!model) { fail({ErrorCode::InvalidArgument, QStringLiteral("Settings model required"), {}, {}}); return false; }
    OperationError error;
    const QByteArray bytes = encode(model->exportValues(), m_extensions, &error);
    if (bytes.isEmpty()) { fail(error); return false; }
    if (bytes.size() > kMaximumSettingsBytes) {
        fail({ErrorCode::Unsupported, QStringLiteral("Settings file exceeds limit"), m_path, {}});
        return false;
    }
    error = m_store->write(m_path, bytes);
    fail(error);
    return !error;
}

} // namespace QindaTK
