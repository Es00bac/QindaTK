// SPDX-License-Identifier: LGPL-3.0-or-later
// LocalDataDesignDocument: construction, snapshots, history and edits.
// Persistence and the DocumentCodec are in local_data_design_codec.cpp.
#include "local_data_design_document.h"

#include "local_data_design_store.h"
#include "local_database_provider.h"
#include "local_report_spec.h"
#include "sqlite_query_source.h"
#include "undo_service.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QMetaType>
#include <QUuid>

namespace QindaTK {
namespace {
QVariantMap mapAt(const QVariantMap &map, const QString &key) { return map.value(key).toMap(); }

} // namespace

// The first-cut report keys (one groupField, a SUM field, one equality
// filter) become groups/totals and a WP-029 filter before an edit, so an
// edited design has one representation. Reading still accepts them
// (LocalReport::parse). `types` is binding.types: it types the parameter.
void LocalDataDesignDocument::migrateLegacyReport(QVariantMap *report, const QVariantMap &types)
{
    const QString group = report->take(QStringLiteral("groupField")).toString();
    const QString sum = report->take(QStringLiteral("sumField")).toString();
    if (!group.isEmpty() && !report->contains(QStringLiteral("groups"))) {
        QVariantList totals;
        if (!sum.isEmpty())
            totals.append(QVariantMap{{QStringLiteral("field"), sum}, {QStringLiteral("function"), QStringLiteral("sum")}});
        report->insert(QStringLiteral("groups"), QVariantList{QVariantMap{{QStringLiteral("field"), group},
                                                                          {QStringLiteral("totals"), totals}}});
        report->insert(QStringLiteral("totals"), totals);
        report->insert(QStringLiteral("detail"), false);
    }
    const QString filter = report->take(QStringLiteral("filterField")).toString();
    const QVariant raw = report->take(QStringLiteral("filterValue"));
    if (!filter.isEmpty() && mapAt(mapAt(*report, QStringLiteral("query")), QStringLiteral("filter")).isEmpty()) {
        const QString typeName = types.value(filter, QStringLiteral("text")).toString();
        QueryType type = QueryType::Text;
        for (QueryType candidate : {QueryType::Boolean, QueryType::Integer, QueryType::Number})
            if (SqliteTableSource::typeName(candidate) == typeName) type = candidate;
        QueryValue value;
        if ((raw.toString().isEmpty() && type != QueryType::Text) || !LocalReport::convert(raw, type, &value))
            value = QueryValue::null();
        QVariantMap query = mapAt(*report, QStringLiteral("query"));
        query.insert(QStringLiteral("filter"), QVariantMap{{QStringLiteral("kind"), QStringLiteral("compare")},
            {QStringLiteral("field"), filter}, {QStringLiteral("op"), QStringLiteral("eq")},
            {QStringLiteral("parameter"), QStringLiteral("filterValue")}});
        query.insert(QStringLiteral("parameters"), QVariantMap{{QStringLiteral("filterValue"),
                                                                queryValueToJson(value).toVariantMap()}});
        report->insert(QStringLiteral("query"), query);
    }
}

int LocalDataDesignDocument::indexOfField(const QVariantList &list, const QString &key, const QString &field)
{
    for (int i = 0; i < list.size(); ++i)
        if (list.at(i).toMap().value(key).toString() == field) return i;
    return -1;
}

// AGENT-CONTRACT (docs/document-models.md#commands): one whole-design step.
// QJsonObject is implicitly shared, so before/after cost only the design
// they differ in; the byte estimate is the compact JSON of both.
struct LocalDataDesignDocument::DesignCommand final : DocumentCommand {
    QString documentId;
    quint64 resetGeneration = 0;
    QJsonObject before, after;
    quint64 bytes = 0;
    DesignCommand(QString id, quint64 generation, QJsonObject oldState, QJsonObject newState)
        : documentId(std::move(id)), resetGeneration(generation),
          before(std::move(oldState)), after(std::move(newState))
    {
        bytes = sizeof(*this) + quint64(QJsonDocument(before).toJson(QJsonDocument::Compact).size())
            + quint64(QJsonDocument(after).toJson(QJsonDocument::Compact).size());
    }
    quint64 retainedBytes() const override { return bytes; }
    bool isNoOp() const override { return before == after; }
};

LocalDataDesignDocument::LocalDataDesignDocument(QObject *parent) : QObject(parent)
{
    m_documentId = DocumentId::create().toString();
    m_design = emptyDesign();
    m_localHistory = new UndoService(this);
    m_localHistory->setMemoryLimit(quint64(64) * 1024 * 1024);
    m_localHistory->addModel(this);
    m_localHistory->setStepLimit(this, HistorySteps);
}

LocalDataDesignDocument::~LocalDataDesignDocument() = default;

QJsonObject LocalDataDesignDocument::emptyDesign()
{
    const QString id = QStringLiteral("design-") + QUuid::createUuid().toString(QUuid::Id128).left(12);
    return QJsonObject::fromVariantMap({
        {QStringLiteral("format"), QStringLiteral("qindatk.local-data-design")}, {QStringLiteral("version"), 2},
        {QStringLiteral("designId"), id},
        {QStringLiteral("binding"), QVariantMap{{QStringLiteral("provider"), QStringLiteral("sqlite")},
            {QStringLiteral("schema"), QStringLiteral("main")}, {QStringLiteral("table"), QString()},
            {QStringLiteral("fields"), QVariantList{}}, {QStringLiteral("types"), QVariantMap{}}}},
        {QStringLiteral("form"), QVariantMap{{QStringLiteral("title"), tr("Untitled form")},
            {QStringLiteral("tableName"), QString()}, {QStringLiteral("fields"), QVariantList{}}}},
        {QStringLiteral("report"), QVariantMap{{QStringLiteral("tableName"), QString()},
            {QStringLiteral("headerText"), tr("Data report")}, {QStringLiteral("footerText"), QString()},
            {QStringLiteral("detail"), true}, {QStringLiteral("columns"), QVariantList{}},
            {QStringLiteral("groups"), QVariantList{}}, {QStringLiteral("totals"), QVariantList{}}}}});
}

// ---------------------------------------------------------------- snapshots
QString LocalDataDesignDocument::tableName() const
{
    return m_design.value(QStringLiteral("binding")).toObject().value(QStringLiteral("table")).toString();
}

QStringList LocalDataDesignDocument::boundFields() const
{
    QStringList out;
    for (const QJsonValue &v : m_design.value(QStringLiteral("binding")).toObject().value(QStringLiteral("fields")).toArray())
        out.append(v.toString());
    return out;
}

QVariantMap LocalDataDesignDocument::form() const
{
    QVariantMap form = mapAt(design(), QStringLiteral("form"));
    form.insert(QStringLiteral("tableName"), tableName());
    return form;
}

QVariantMap LocalDataDesignDocument::report() const
{
    QVariantMap report = mapAt(design(), QStringLiteral("report"));
    report.insert(QStringLiteral("tableName"), tableName());
    if (report.value(QStringLiteral("columns")).toList().isEmpty()) {
        QVariantList columns;
        for (const QString &field : boundFields()) columns.append(field);
        report.insert(QStringLiteral("columns"), columns);
    }
    return report;
}

QVariantMap LocalDataDesignDocument::validate(LocalDatabaseProvider *provider) const
{
    LocalDataDesignStore store;
    return store.validate(design(), provider);
}

// ------------------------------------------------------------------ history
UndoService *LocalDataDesignDocument::history() const
{
    if (auto *service = historyService()) return service;
    auto *self = const_cast<LocalDataDesignDocument *>(this);
    if (m_localHistory && m_localHistory->addModel(self)) m_localHistory->setStepLimit(self, HistorySteps);
    return historyService();
}
UndoService *LocalDataDesignDocument::undoService() const { return history(); }
bool LocalDataDesignDocument::canUndo() const { return history() && history()->canUndo(this); }
bool LocalDataDesignDocument::canRedo() const { return history() && history()->canRedo(this); }
QString LocalDataDesignDocument::undoText() const { return history() ? history()->undoText(this) : QString(); }
QString LocalDataDesignDocument::redoText() const { return history() ? history()->redoText(this) : QString(); }

void LocalDataDesignDocument::historyAttached(UndoService *service)
{
    disconnect(m_historyConnection);
    m_historyConnection = {};
    if (service)
        m_historyConnection = connect(service, &UndoService::historyChanged, this,
            [this](const QString &id) { if (id == m_documentId) emit historyChanged(); });
    emit historyChanged();
}

SnapshotHeader LocalDataDesignDocument::modelHeader() const
{
    return {schemaId(), DocumentId::parse(m_documentId), {m_revision}, m_resetGeneration};
}

QMetaObject::Connection LocalDataDesignDocument::connectModelChanged(QObject *receiver, std::function<void()> slot)
{
    return connect(this, &LocalDataDesignDocument::changed, receiver, std::move(slot));
}

OperationError LocalDataDesignDocument::prepareCommand(const DocumentCommand &command, bool forward) const
{
    const auto *step = dynamic_cast<const DesignCommand *>(&command);
    if (!step || step->documentId != m_documentId || step->resetGeneration != m_resetGeneration)
        return {ErrorCode::StaleGeneration, tr("Design history belongs to another document"), {}, {}};
    if (m_design != (forward ? step->before : step->after))
        return {ErrorCode::StaleRevision, tr("Design changed outside the recorded step"), {}, {}};
    return {};
}

void LocalDataDesignDocument::publishCommand(const DocumentCommand &command, bool forward)
{
    const auto &step = static_cast<const DesignCommand &>(command);
    m_design = forward ? step.after : step.before;
    ++m_revision;
    m_error.clear();
    emit errorChanged();
    emit changed();
}

DocumentCommandPtr LocalDataDesignDocument::composeCommands(const DocumentCommand &first,
                                                            const DocumentCommand &second) const
{
    const auto *a = dynamic_cast<const DesignCommand *>(&first);
    const auto *b = dynamic_cast<const DesignCommand *>(&second);
    if (!a || !b || a->documentId != b->documentId || a->resetGeneration != b->resetGeneration
        || a->after != b->before) return {};
    return std::make_shared<const DesignCommand>(a->documentId, a->resetGeneration, a->before, b->after);
}

bool LocalDataDesignDocument::commit(const QVariantMap &afterMap, const QString &label)
{
    const QJsonObject after = QJsonObject::fromVariantMap(afterMap);
    const QString problem = LocalDataDesignStore::checkStructure(after.toVariantMap());
    if (!problem.isEmpty()) return fail(problem);
    if (after == m_design) {
        m_error.clear();
        emit errorChanged();
        return true;
    }
    const auto step = std::make_shared<const DesignCommand>(m_documentId, m_resetGeneration, m_design, after);
    if (step->bytes > quint64(2 * MaximumBytes)) return fail(tr("Design exceeds 4 MiB"));
    const OperationError error = history()->commit(this, label, {}, step);
    return error ? fail(error.message) : true;
}

bool LocalDataDesignDocument::undo()
{
    const OperationError error = history()->undo(this);
    return error ? fail(error.message) : true;
}

bool LocalDataDesignDocument::redo()
{
    const OperationError error = history()->redo(this);
    return error ? fail(error.message) : true;
}

bool LocalDataDesignDocument::fail(const QString &message)
{
    m_error = message;
    emit errorChanged();
    return false;
}

void LocalDataDesignDocument::adoptLoaded(QJsonObject design, const SnapshotHeader &header)
{
    // Called by the codec on a fresh document only: history is re-created clean.
    m_localHistory->removeModel(this);
    m_documentId = header.documentId.toString();
    m_revision = std::max<quint64>(1, header.revision.value);
    m_resetGeneration = header.resetGeneration;
    m_design = std::move(design);
    m_localHistory->addModel(this);
    m_localHistory->setStepLimit(this, HistorySteps);
    m_localHistory->markModelClean(this);
}

// -------------------------------------------------------------------- edits
bool LocalDataDesignDocument::setDesign(const QVariantMap &incoming)
{
    QVariantMap next = incoming;
    next.remove(QStringLiteral("session"));
    if (next.value(QStringLiteral("version")).toInt() == 1) next.insert(QStringLiteral("version"), 2);
    return commit(next, tr("Replace design"));
}

bool LocalDataDesignDocument::bindTable(LocalDatabaseProvider *provider, const QString &table,
                                        const QStringList &requested)
{
    if (!provider || !provider->connected()) return fail(tr("SQLite database is not connected"));
    const QVariantMap live = provider->schema(table);
    if (!live.value(QStringLiteral("ok")).toBool()) return fail(live.value(QStringLiteral("error")).toString());
    const QuerySchema schema = SqliteTableSource::schemaFor(table, live);
    QVariantMap types;
    QStringList available;
    for (const QueryField &field : schema.fields) {
        available.append(field.id);
        types.insert(field.id, SqliteTableSource::typeName(field.type));
    }
    const QStringList fields = requested.isEmpty() ? available : requested;
    for (const QString &field : fields)
        if (!available.contains(field)) return fail(tr("Column %1 cannot be bound").arg(field));
    if (fields.isEmpty()) return fail(tr("Table %1 has no bindable columns").arg(table));
    for (auto it = types.begin(); it != types.end();) it = fields.contains(it.key()) ? std::next(it) : types.erase(it);

    QVariantMap next = design();
    QVariantMap binding = mapAt(next, QStringLiteral("binding"));
    binding.insert(QStringLiteral("table"), table);
    binding.insert(QStringLiteral("fields"), fields);
    binding.insert(QStringLiteral("types"), types);
    next.insert(QStringLiteral("binding"), binding);

    QVariantMap form = mapAt(next, QStringLiteral("form"));
    QVariantList formFields;
    for (const QVariant &entry : form.value(QStringLiteral("fields")).toList())
        if (fields.contains(entry.toMap().value(QStringLiteral("fieldName")).toString())) formFields.append(entry);
    for (const QString &field : fields)
        if (indexOfField(formFields, QStringLiteral("fieldName"), field) < 0)
            formFields.append(QVariantMap{{QStringLiteral("fieldName"), field}, {QStringLiteral("label"), field}});
    form.insert(QStringLiteral("fields"), formFields);
    form.insert(QStringLiteral("tableName"), table);
    next.insert(QStringLiteral("form"), form);

    QVariantMap report = mapAt(next, QStringLiteral("report"));
    migrateLegacyReport(&report, types);
    report.insert(QStringLiteral("tableName"), table);
    auto keep = [&fields](const QVariantList &list, const QString &key) {
        QVariantList out;
        for (const QVariant &entry : list) {
            const QString name = entry.metaType().id() == QMetaType::QString ? entry.toString()
                                                                             : entry.toMap().value(key).toString();
            if (name.isEmpty() || fields.contains(name)) out.append(entry);
        }
        return out;
    };
    report.insert(QStringLiteral("columns"), keep(report.value(QStringLiteral("columns")).toList(), QStringLiteral("field")));
    report.insert(QStringLiteral("totals"), keep(report.value(QStringLiteral("totals")).toList(), QStringLiteral("field")));
    QVariantList groups;
    for (const QVariant &entry : keep(report.value(QStringLiteral("groups")).toList(), QStringLiteral("field"))) {
        QVariantMap group = entry.toMap();
        group.insert(QStringLiteral("totals"), keep(group.value(QStringLiteral("totals")).toList(), QStringLiteral("field")));
        groups.append(group);
    }
    report.insert(QStringLiteral("groups"), groups);
    QVariantMap probe{{QStringLiteral("report"), QVariantMap{{QStringLiteral("query"), report.value(QStringLiteral("query"))}}}};
    for (const QString &name : LocalDataDesignStore::referencedFields(probe))
        if (!fields.contains(name)) { report.remove(QStringLiteral("query")); break; }
    next.insert(QStringLiteral("report"), report);
    return commit(next, tr("Bind to %1").arg(table));
}

bool LocalDataDesignDocument::setFormTitle(const QString &title)
{
    QVariantMap next = design();
    QVariantMap form = mapAt(next, QStringLiteral("form"));
    form.insert(QStringLiteral("title"), title.trimmed());
    next.insert(QStringLiteral("form"), form);
    return commit(next, tr("Rename form"));
}

bool LocalDataDesignDocument::setFormFieldLabel(const QString &field, const QString &label)
{
    if (!boundFields().contains(field)) return fail(tr("Field %1 is not bound").arg(field));
    QVariantMap next = design();
    QVariantMap form = mapAt(next, QStringLiteral("form"));
    QVariantList fields = form.value(QStringLiteral("fields")).toList();
    const int i = indexOfField(fields, QStringLiteral("fieldName"), field);
    QVariantMap entry = i < 0 ? QVariantMap{{QStringLiteral("fieldName"), field}} : fields.at(i).toMap();
    entry.insert(QStringLiteral("label"), label.trimmed().isEmpty() ? field : label.trimmed());
    if (i < 0) fields.append(entry); else fields[i] = entry;
    form.insert(QStringLiteral("fields"), fields);
    next.insert(QStringLiteral("form"), form);
    return commit(next, tr("Rename form field"));
}

bool LocalDataDesignDocument::setFormFieldHidden(const QString &field, bool hidden)
{
    QVariantMap next = design();
    QVariantMap form = mapAt(next, QStringLiteral("form"));
    QVariantList fields = form.value(QStringLiteral("fields")).toList();
    const int i = indexOfField(fields, QStringLiteral("fieldName"), field);
    if (i < 0) return fail(tr("Field %1 is not on the form").arg(field));
    QVariantMap entry = fields.at(i).toMap();
    if (hidden) entry.insert(QStringLiteral("hidden"), true); else entry.remove(QStringLiteral("hidden"));
    fields[i] = entry;
    form.insert(QStringLiteral("fields"), fields);
    next.insert(QStringLiteral("form"), form);
    return commit(next, hidden ? tr("Hide form field") : tr("Show form field"));
}

bool LocalDataDesignDocument::moveFormField(const QString &field, int index)
{
    QVariantMap next = design();
    QVariantMap form = mapAt(next, QStringLiteral("form"));
    QVariantList fields = form.value(QStringLiteral("fields")).toList();
    const int from = indexOfField(fields, QStringLiteral("fieldName"), field);
    if (from < 0) return fail(tr("Field %1 is not on the form").arg(field));
    fields.move(from, qBound(0, index, int(fields.size()) - 1));
    form.insert(QStringLiteral("fields"), fields);
    next.insert(QStringLiteral("form"), form);
    return commit(next, tr("Move form field"));
}

bool LocalDataDesignDocument::setReportText(const QString &key, const QString &text)
{
    if (key != QLatin1String("headerText") && key != QLatin1String("footerText"))
        return fail(tr("Unknown report text %1").arg(key));
    QVariantMap next = design();
    QVariantMap report = mapAt(next, QStringLiteral("report"));
    report.insert(key, text);
    next.insert(QStringLiteral("report"), report);
    return commit(next, key == QLatin1String("headerText") ? tr("Edit report header") : tr("Edit report footer"));
}
} // namespace QindaTK
