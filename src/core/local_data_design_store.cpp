// SPDX-License-Identifier: LGPL-3.0-or-later
#include "local_data_design_store.h"

#include "local_database_provider.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QSet>

namespace QindaTK {
namespace {
QVariantMap failure(const QString &message)
{
    return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
}

bool containsAll(const QSet<QString> &fields, const QVariantList &requested)
{
    QSet<QString> seen;
    for (const QVariant &item : requested) {
        const QString name = item.toString();
        if (name.isEmpty() || seen.contains(name) || !fields.contains(name)) return false;
        seen.insert(name);
    }
    return !requested.isEmpty();
}
}

LocalDataDesignStore::LocalDataDesignStore(QObject *parent) : QObject(parent) {}

QString LocalDataDesignStore::defaultPath() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
        + QStringLiteral("/local-data-design.json");
}

QVariantMap LocalDataDesignStore::validateStructure(const QVariantMap &design)
{
    if (design.value(QStringLiteral("format")).toString() != QStringLiteral("qindatk.local-data-design")
        || design.value(QStringLiteral("version")).toInt() != 1)
        return failure(QStringLiteral("Unsupported local data design format or version"));
    if (design.value(QStringLiteral("designId")).toString().trimmed().isEmpty())
        return failure(QStringLiteral("A stable designId is required"));
    const QVariantMap binding = design.value(QStringLiteral("binding")).toMap();
    if (binding.value(QStringLiteral("provider")).toString() != QStringLiteral("sqlite")
        || binding.value(QStringLiteral("schema")).toString() != QStringLiteral("main")
        || binding.value(QStringLiteral("table")).toString().isEmpty())
        return failure(QStringLiteral("Binding must name a SQLite main-schema table"));
    const QVariantList bindingFields = binding.value(QStringLiteral("fields")).toList();
    QSet<QString> fields;
    for (const QVariant &item : bindingFields) {
        const QString name = item.toString();
        if (name.isEmpty() || fields.contains(name)) return failure(QStringLiteral("Binding fields must be unique nonempty names"));
        fields.insert(name);
    }
    if (fields.isEmpty()) return failure(QStringLiteral("At least one bound field is required"));

    const QVariantMap form = design.value(QStringLiteral("form")).toMap();
    if (!form.value(QStringLiteral("tableName")).toString().isEmpty()
        && form.value(QStringLiteral("tableName")).toString() != binding.value(QStringLiteral("table")).toString())
        return failure(QStringLiteral("Form table does not match the stable binding"));
    QVariantList formFields;
    for (const QVariant &field : form.value(QStringLiteral("fields")).toList())
        formFields.push_back(field.toMap().value(QStringLiteral("fieldName")));
    if (!formFields.isEmpty() && !containsAll(fields, formFields))
        return failure(QStringLiteral("Form fields must resolve to unique bound field names"));

    const QVariantMap report = design.value(QStringLiteral("report")).toMap();
    if (!report.value(QStringLiteral("tableName")).toString().isEmpty()
        && report.value(QStringLiteral("tableName")).toString() != binding.value(QStringLiteral("table")).toString())
        return failure(QStringLiteral("Report table does not match the stable binding"));
    for (const QString &key : {QStringLiteral("filterField"), QStringLiteral("groupField")}) {
        const QString name = report.value(key).toString();
        if (!name.isEmpty() && !fields.contains(name)) return failure(QStringLiteral("Report references an unbound field: ") + name);
    }
    return {{QStringLiteral("ok"), true}, {QStringLiteral("design"), design}};
}

QVariantMap LocalDataDesignStore::validate(const QVariantMap &design,
                                            LocalDatabaseProvider *provider) const
{
    QVariantMap result = validateStructure(design);
    if (!result.value(QStringLiteral("ok")).toBool() || !provider) return result;
    if (!provider->connected()) return failure(QStringLiteral("SQLite database is not connected"));
    const QVariantMap binding = design.value(QStringLiteral("binding")).toMap();
    const QVariantMap schema = provider->schema(binding.value(QStringLiteral("table")).toString());
    if (!schema.value(QStringLiteral("ok")).toBool()) return failure(schema.value(QStringLiteral("error")).toString());
    QSet<QString> actual;
    for (const QVariant &field : schema.value(QStringLiteral("fields")).toList())
        actual.insert(field.toMap().value(QStringLiteral("name")).toString());
    for (const QVariant &field : binding.value(QStringLiteral("fields")).toList()) {
        if (!actual.contains(field.toString()))
            return failure(QStringLiteral("Bound field is missing from the live schema: ") + field.toString());
    }
    return result;
}

QVariantMap LocalDataDesignStore::saveToFile(const QString &path, const QVariantMap &design,
                                              LocalDatabaseProvider *provider) const
{
    const QVariantMap checked = validate(design, provider);
    if (!checked.value(QStringLiteral("ok")).toBool()) return checked;
    const QString localPath = QUrl(path).isLocalFile() ? QUrl(path).toLocalFile() : path;
    if (localPath.trimmed().isEmpty()) return failure(QStringLiteral("Design path is empty"));
    if (!QDir().mkpath(QFileInfo(localPath).absolutePath())) return failure(QStringLiteral("Cannot create design directory"));
    QSaveFile file(localPath);
    if (!file.open(QIODevice::WriteOnly)) return failure(file.errorString());
    const QByteArray json = QJsonDocument(QJsonObject::fromVariantMap(design)).toJson(QJsonDocument::Indented);
    if (json.size() > 1024 * 1024 || file.write(json) != json.size()) {
        file.cancelWriting();
        return failure(QStringLiteral("Design is too large or could not be written"));
    }
    if (!file.commit()) return failure(file.errorString());
    return {{QStringLiteral("ok"), true}, {QStringLiteral("path"), QFileInfo(localPath).absoluteFilePath()}};
}

QVariantMap LocalDataDesignStore::loadFromFile(const QString &path,
                                                LocalDatabaseProvider *provider) const
{
    const QString localPath = QUrl(path).isLocalFile() ? QUrl(path).toLocalFile() : path;
    QFile file(localPath);
    if (!file.open(QIODevice::ReadOnly)) return failure(file.errorString());
    if (file.size() > 1024 * 1024) return failure(QStringLiteral("Design exceeds the 1 MiB read bound"));
    QJsonParseError parseError;
    const QJsonDocument json = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !json.isObject())
        return failure(QStringLiteral("Invalid local data design JSON: ") + parseError.errorString());
    const QVariantMap design = json.object().toVariantMap();
    const QVariantMap checked = validate(design, provider);
    if (!checked.value(QStringLiteral("ok")).toBool()) return checked;
    return {{QStringLiteral("ok"), true}, {QStringLiteral("design"), design},
            {QStringLiteral("path"), QFileInfo(localPath).absoluteFilePath()}};
}
}
