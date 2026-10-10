// SPDX-License-Identifier: LGPL-3.0-or-later
#include "local_data_design_store.h"

#include "local_database_provider.h"
#include "local_report_spec.h"
#include "sqlite_query_source.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaType>
#include <QSaveFile>
#include <QStandardPaths>
#include <QSet>
#include <QUrl>

namespace QindaTK {
namespace {
QVariantMap failure(const QString &message)
{
    return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
}

void predicateFields(const QVariantMap &predicate, QStringList *out, int depth = 0)
{
    if (depth > 24) return;
    const QString field = predicate.value(QStringLiteral("field")).toString();
    if (!field.isEmpty()) out->append(field);
    for (const QVariant &child : predicate.value(QStringLiteral("children")).toList())
        predicateFields(child.toMap(), out, depth + 1);
}
}

LocalDataDesignStore::LocalDataDesignStore(QObject *parent) : QObject(parent) {}

QString LocalDataDesignStore::defaultPath() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
        + QStringLiteral("/local-data-design.json");
}

QStringList LocalDataDesignStore::referencedFields(const QVariantMap &design)
{
    QStringList out;
    const QVariantMap form = design.value(QStringLiteral("form")).toMap();
    for (const QVariant &field : form.value(QStringLiteral("fields")).toList())
        out.append(field.toMap().value(QStringLiteral("fieldName")).toString());
    // A bound control (text box, combo box, list box, check box) names a field.
    for (const QVariant &control : form.value(QStringLiteral("controls")).toList()) {
        const QString name = control.toMap().value(QStringLiteral("fieldName")).toString();
        if (!name.isEmpty()) out.append(name);
    }
    const QVariantMap report = design.value(QStringLiteral("report")).toMap();
    for (const QString &key : {QStringLiteral("filterField"), QStringLiteral("groupField"), QStringLiteral("sumField")}) {
        const QString name = report.value(key).toString();
        if (!name.isEmpty()) out.append(name);
    }
    for (const QVariant &column : report.value(QStringLiteral("columns")).toList())
        out.append(column.metaType().id() == QMetaType::QString ? column.toString()
                                                                 : column.toMap().value(QStringLiteral("field")).toString());
    auto totals = [&out](const QVariantList &list) {
        for (const QVariant &total : list) {
            const QString field = total.toMap().value(QStringLiteral("field")).toString();
            if (!field.isEmpty()) out.append(field);
        }
    };
    for (const QVariant &group : report.value(QStringLiteral("groups")).toList()) {
        out.append(group.toMap().value(QStringLiteral("field")).toString());
        totals(group.toMap().value(QStringLiteral("totals")).toList());
    }
    totals(report.value(QStringLiteral("totals")).toList());
    const QVariantMap query = report.value(QStringLiteral("query")).toMap();
    predicateFields(query.value(QStringLiteral("filter")).toMap(), &out);
    for (const QVariant &sort : query.value(QStringLiteral("sort")).toList())
        out.append(sort.toMap().value(QStringLiteral("field")).toString());
    return out;
}

QString LocalDataDesignStore::checkStructure(const QVariantMap &design)
{
    const int version = design.value(QStringLiteral("version")).toInt();
    if (design.value(QStringLiteral("format")).toString() != QStringLiteral("qindatk.local-data-design")
        || (version != 1 && version != 2))
        return tr("Unsupported local data design format or version");
    if (design.value(QStringLiteral("designId")).toString().trimmed().isEmpty())
        return tr("A stable designId is required");
    const QVariantMap binding = design.value(QStringLiteral("binding")).toMap();
    const QString table = binding.value(QStringLiteral("table")).toString();
    if (binding.value(QStringLiteral("provider")).toString() != QStringLiteral("sqlite")
        || binding.value(QStringLiteral("schema")).toString() != QStringLiteral("main"))
        return tr("Binding must name a SQLite main-schema table");
    QSet<QString> fields;
    for (const QVariant &item : binding.value(QStringLiteral("fields")).toList()) {
        const QString name = item.toString();
        if (name.isEmpty() || fields.contains(name)) return tr("Binding fields must be unique nonempty names");
        fields.insert(name);
    }
    // An unbound design (a new document) has no table, fields or references.
    if (table.isEmpty() && (!fields.isEmpty() || version == 1))
        return tr("Binding must name a SQLite main-schema table");
    if (!table.isEmpty() && fields.isEmpty()) return tr("At least one bound field is required");
    for (const QString &key : {QStringLiteral("form"), QStringLiteral("report")}) {
        const QString named = design.value(key).toMap().value(QStringLiteral("tableName")).toString();
        if (!named.isEmpty() && named != table)
            return key == QLatin1String("form") ? tr("Form table does not match the stable binding")
                                                : tr("Report table does not match the stable binding");
    }
    QSet<QString> formFields;
    for (const QVariant &field : design.value(QStringLiteral("form")).toMap().value(QStringLiteral("fields")).toList()) {
        const QString name = field.toMap().value(QStringLiteral("fieldName")).toString();
        if (formFields.contains(name)) return tr("Form fields must resolve to unique bound field names");
        formFields.insert(name);
    }
    const QVariant controls = design.value(QStringLiteral("form")).toMap().value(QStringLiteral("controls"));
    if (controls.isValid() && !controls.isNull()) {
        if (controls.metaType().id() != QMetaType::QVariantList) return tr("Form controls must be a list");
        const QString problem = checkFormControls(controls.toList());
        if (!problem.isEmpty()) return problem;
    }
    for (const QString &name : referencedFields(design))
        if (!fields.contains(name)) return tr("Design references an unbound field: %1").arg(name);
    const QVariantMap types = binding.value(QStringLiteral("types")).toMap();
    for (auto it = types.cbegin(); it != types.cend(); ++it)
        if (!fields.contains(it.key())) return tr("Binding types name an unbound field: %1").arg(it.key());
    return {};
}

QVariantMap LocalDataDesignStore::validate(const QVariantMap &design,
                                            LocalDatabaseProvider *provider) const
{
    const QString structural = checkStructure(design);
    if (!structural.isEmpty()) return failure(structural);
    QVariantMap result{{QStringLiteral("ok"), true}, {QStringLiteral("design"), design}};
    if (!provider) return result;
    if (!provider->connected()) return failure(tr("SQLite database is not connected"));
    const QVariantMap binding = design.value(QStringLiteral("binding")).toMap();
    const QString table = binding.value(QStringLiteral("table")).toString();
    const QVariantMap live = provider->schema(table);
    if (!live.value(QStringLiteral("ok")).toBool()) return failure(live.value(QStringLiteral("error")).toString());
    QStringList unsupported;
    const QuerySchema schema = SqliteTableSource::schemaFor(table, live, &unsupported);
    QHash<QString, QString> liveTypes;
    for (const QueryField &field : schema.fields) liveTypes.insert(field.id, SqliteTableSource::typeName(field.type));
    for (const QString &name : unsupported) liveTypes.insert(name, QStringLiteral("unsupported"));

    const QVariantMap savedTypes = binding.value(QStringLiteral("types")).toMap();
    QVariantList fields, issues;
    QString firstFatal;
    auto issue = [&](const QString &field, const QString &kind, const QString &message, bool fatal) {
        issues.append(QVariantMap{{QStringLiteral("field"), field}, {QStringLiteral("kind"), kind},
                                  {QStringLiteral("message"), message}, {QStringLiteral("fatal"), fatal}});
        if (fatal && firstFatal.isEmpty()) firstFatal = message;
    };
    // AGENT-NOTE: a saved design is qualified against today's schema, not
    // trusted: a dropped column is fatal, a changed type is reported so the
    // host can show it, and the report part must still parse (below).
    for (const QVariant &entry : binding.value(QStringLiteral("fields")).toList()) {
        const QString name = entry.toString();
        const QString saved = savedTypes.value(name).toString();
        const QString current = liveTypes.value(name);
        QString status = QStringLiteral("ok");
        if (current.isEmpty()) {
            status = QStringLiteral("missing");
            issue(name, status, tr("Bound field is missing from the live schema: %1").arg(name), true);
        } else if (current == QLatin1String("unsupported")) {
            status = QStringLiteral("unsupported");
            issue(name, status, tr("Bound field %1 has no reportable type").arg(name), false);
        } else if (!saved.isEmpty() && saved != current) {
            status = QStringLiteral("typeChanged");
            issue(name, status, tr("Bound field %1 changed from %2 to %3").arg(name, saved, current), false);
        }
        fields.append(QVariantMap{{QStringLiteral("name"), name}, {QStringLiteral("status"), status},
                                  {QStringLiteral("savedType"), saved}, {QStringLiteral("liveType"), current}});
    }
    QVariantMap report = design.value(QStringLiteral("report")).toMap();
    if (firstFatal.isEmpty() && !report.isEmpty()) {
        report.insert(QStringLiteral("tableName"), table);
        LocalReport::Spec spec;
        QString stage;
        const QString problem = LocalReport::parse(report, schema, {}, &spec, &stage);
        if (!problem.isEmpty()) issue({}, QStringLiteral("report"), problem, true);
    }
    result.insert(QStringLiteral("fields"), fields);
    result.insert(QStringLiteral("issues"), issues);
    if (!firstFatal.isEmpty()) {
        result.insert(QStringLiteral("ok"), false);
        result.insert(QStringLiteral("error"), firstFatal);
    }
    return result;
}

QVariantMap LocalDataDesignStore::saveToFile(const QString &path, const QVariantMap &design,
                                              LocalDatabaseProvider *provider) const
{
    const QVariantMap checked = validate(design, provider);
    if (!checked.value(QStringLiteral("ok")).toBool()) return checked;
    const QString localPath = QUrl(path).isLocalFile() ? QUrl(path).toLocalFile() : path;
    if (localPath.trimmed().isEmpty()) return failure(tr("Design path is empty"));
    if (!QDir().mkpath(QFileInfo(localPath).absolutePath())) return failure(tr("Cannot create design directory"));
    QSaveFile file(localPath);
    if (!file.open(QIODevice::WriteOnly)) return failure(file.errorString());
    const QByteArray json = QJsonDocument(QJsonObject::fromVariantMap(design)).toJson(QJsonDocument::Indented);
    if (json.size() > 1024 * 1024 || file.write(json) != json.size()) {
        file.cancelWriting();
        return failure(tr("Design is too large or could not be written"));
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
    if (file.size() > 4 * 1024 * 1024) return failure(tr("Design exceeds the 4 MiB read bound"));
    QJsonParseError parseError;
    const QJsonDocument json = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !json.isObject())
        return failure(tr("Invalid local data design JSON: %1").arg(parseError.errorString()));
    QVariantMap design = json.object().toVariantMap();
    design.remove(QStringLiteral("session")); // the session header of a version 2 document file
    QVariantMap checked = validate(design, provider);
    if (!checked.value(QStringLiteral("ok")).toBool()) return checked;
    checked.insert(QStringLiteral("path"), QFileInfo(localPath).absoluteFilePath());
    return checked;
}
}
