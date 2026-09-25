// SPDX-License-Identifier: LGPL-3.0-or-later
// qindatk.richtext version-2 members of WP-052 (D-117): reference fields,
// sources, comment threads and tracked changes, and the marks in character
// formats that refer to them (docs/references-review.md#persistence).
#include "rich_text_codec_p.h"

#include <QJsonArray>
#include <algorithm>

namespace QindaTK::RichText::Codec {

namespace {

template <typename T>
QList<T> sortedById(const QHash<QString, T> &records)
{
    QList<T> list = records.values();
    std::sort(list.begin(), list.end(), [](const T &a, const T &b) { return a.id < b.id; });
    return list;
}

void insertText(QJsonObject &o, const char *key, const QString &value)
{
    if (!value.isEmpty()) o.insert(QLatin1String(key), value);
}

struct Reader {
    PublishingError *error;
    QStringList *warnings;

    bool fail(const QString &path, const QString &message)
    {
        if (error && error->path.isEmpty()) *error = {message, path};
        return false;
    }

    void checkKeys(const QJsonObject &o, const QString &path, std::initializer_list<const char *> known)
    {
        if (!warnings) return;
        for (auto it = o.begin(); it != o.end(); ++it) {
            bool found = false;
            for (const char *key : known) found = found || it.key() == QLatin1String(key);
            if (!found) warnings->append(QStringLiteral("%1.%2 ignored").arg(path, it.key()));
        }
    }

    // A string member; `required` members must be non-empty.
    bool text(const QJsonObject &o, const char *key, QString *out, const QString &path, bool required = false)
    {
        const QJsonValue value = o.value(QLatin1String(key));
        if (value.isUndefined() && !required) return true;
        if (!value.isString() || value.toString().size() > 100000 || (required && value.toString().isEmpty()))
            return fail(path + u'.' + QLatin1String(key), QStringLiteral("Text expected"));
        *out = value.toString();
        return true;
    }

    template <typename Parse>
    bool array(const QJsonObject &root, const char *key, Parse parse)
    {
        const QJsonValue value = root.value(QLatin1String(key));
        if (value.isUndefined()) return true;
        if (!value.isArray()) return fail(QLatin1String(key), QStringLiteral("Array expected"));
        const QJsonArray list = value.toArray();
        if (list.size() > MaximumAnnotations) return fail(QLatin1String(key), QStringLiteral("Too many annotations"));
        for (int i = 0; i < list.size(); ++i) {
            const QString path = QStringLiteral("%1[%2]").arg(QLatin1String(key)).arg(i);
            if (!list.at(i).isObject()) return fail(path, QStringLiteral("Object expected"));
            if (!parse(list.at(i).toObject(), path)) return false;
        }
        return true;
    }
};

} // namespace

void insertAnnotationMembers(const Annotations &a, QJsonObject *root)
{
    QJsonArray fields;
    for (const ReferenceField &field : sortedById(a.fields)) {
        QJsonObject o{{QStringLiteral("id"), field.id}, {QStringLiteral("kind"), fieldKindName(field.kind)}};
        insertText(o, "sequence", field.sequence);
        insertText(o, "target", field.target);
        insertText(o, "label", field.label);
        fields.append(o);
    }
    QJsonArray sources;
    for (const ReferenceSource &source : sortedById(a.sources)) {
        QJsonObject o{{QStringLiteral("id"), source.id}};
        insertText(o, "author", source.author);
        insertText(o, "title", source.title);
        insertText(o, "year", source.year);
        insertText(o, "publisher", source.publisher);
        sources.append(o);
    }
    QJsonArray comments;
    for (const CommentThread &thread : sortedById(a.threads)) {
        QJsonObject o{{QStringLiteral("id"), thread.id}, {QStringLiteral("body"), thread.body}};
        insertText(o, "author", thread.author);
        insertText(o, "time", thread.time);
        if (thread.resolved) o.insert(QStringLiteral("resolved"), true);
        QJsonArray replies;
        for (const CommentReply &reply : thread.replies) {
            QJsonObject r{{QStringLiteral("id"), reply.id}, {QStringLiteral("body"), reply.body}};
            insertText(r, "author", reply.author);
            insertText(r, "time", reply.time);
            replies.append(r);
        }
        if (!replies.isEmpty()) o.insert(QStringLiteral("replies"), replies);
        comments.append(o);
    }
    QJsonArray changes;
    for (const TrackedChange &change : sortedById(a.changes)) {
        QJsonObject o{{QStringLiteral("id"), change.id}, {QStringLiteral("kind"), changeKindName(change.kind)}};
        insertText(o, "author", change.author);
        insertText(o, "time", change.time);
        changes.append(o);
    }
    if (!fields.isEmpty()) root->insert(QStringLiteral("fields"), fields);
    if (!sources.isEmpty()) root->insert(QStringLiteral("sources"), sources);
    if (!comments.isEmpty()) root->insert(QStringLiteral("comments"), comments);
    if (!changes.isEmpty()) root->insert(QStringLiteral("changes"), changes);
}

bool annotationsFromJson(const QJsonObject &root, Annotations *out, PublishingError *error, QStringList *warnings)
{
    Reader reader{error, warnings};
    Annotations a;
    auto unique = [&](const QString &id, const QString &path) {
        return a.contains(id) ? reader.fail(path + QStringLiteral(".id"), QStringLiteral("Duplicate identifier")) : true;
    };
    const bool ok = reader.array(root, "fields", [&](const QJsonObject &o, const QString &path) {
        reader.checkKeys(o, path, {"id", "kind", "sequence", "target", "label"});
        ReferenceField field;
        QString kind;
        if (!reader.text(o, "id", &field.id, path, true) || !reader.text(o, "kind", &kind, path, true)
            || !reader.text(o, "sequence", &field.sequence, path) || !reader.text(o, "target", &field.target, path)
            || !reader.text(o, "label", &field.label, path) || !unique(field.id, path))
            return false;
        const std::optional<FieldKind> parsed = fieldKindFromName(kind);
        if (!parsed) return reader.fail(path + QStringLiteral(".kind"), QStringLiteral("Unknown field kind"));
        field.kind = *parsed;
        if (field.kind == FieldKind::Sequence && !validSequenceName(field.sequence))
            return reader.fail(path + QStringLiteral(".sequence"), QStringLiteral("Invalid sequence name"));
        // A target that is gone is kept: the field reports itself broken.
        a.fields.insert(field.id, field);
        return true;
    }) && reader.array(root, "sources", [&](const QJsonObject &o, const QString &path) {
        reader.checkKeys(o, path, {"id", "author", "title", "year", "publisher"});
        ReferenceSource source;
        if (!reader.text(o, "id", &source.id, path, true) || !reader.text(o, "author", &source.author, path)
            || !reader.text(o, "title", &source.title, path) || !reader.text(o, "year", &source.year, path)
            || !reader.text(o, "publisher", &source.publisher, path) || !unique(source.id, path))
            return false;
        a.sources.insert(source.id, source);
        return true;
    }) && reader.array(root, "comments", [&](const QJsonObject &o, const QString &path) {
        reader.checkKeys(o, path, {"id", "author", "time", "body", "resolved", "replies"});
        CommentThread thread;
        if (!reader.text(o, "id", &thread.id, path, true) || !reader.text(o, "author", &thread.author, path)
            || !reader.text(o, "time", &thread.time, path) || !reader.text(o, "body", &thread.body, path)
            || !unique(thread.id, path))
            return false;
        const QJsonValue resolved = o.value(QStringLiteral("resolved"));
        if (!resolved.isUndefined() && !resolved.isBool())
            return reader.fail(path + QStringLiteral(".resolved"), QStringLiteral("Boolean expected"));
        thread.resolved = resolved.toBool(false);
        const QJsonArray replies = o.value(QStringLiteral("replies")).toArray();
        for (int i = 0; i < replies.size(); ++i) {
            const QString replyPath = path + QStringLiteral(".replies[%1]").arg(i);
            const QJsonObject r = replies.at(i).toObject();
            CommentReply reply;
            if (!reader.text(r, "id", &reply.id, replyPath, true) || !reader.text(r, "author", &reply.author, replyPath)
                || !reader.text(r, "time", &reply.time, replyPath) || !reader.text(r, "body", &reply.body, replyPath))
                return false;
            thread.replies.append(reply);
        }
        a.threads.insert(thread.id, thread);
        return true;
    }) && reader.array(root, "changes", [&](const QJsonObject &o, const QString &path) {
        reader.checkKeys(o, path, {"id", "kind", "author", "time"});
        TrackedChange change;
        QString kind;
        if (!reader.text(o, "id", &change.id, path, true) || !reader.text(o, "kind", &kind, path, true)
            || !reader.text(o, "author", &change.author, path) || !reader.text(o, "time", &change.time, path)
            || !unique(change.id, path))
            return false;
        const std::optional<ChangeKind> parsed = changeKindFromName(kind);
        if (!parsed) return reader.fail(path + QStringLiteral(".kind"), QStringLiteral("Unknown change kind"));
        change.kind = *parsed;
        a.changes.insert(change.id, change);
        return true;
    });
    if (!ok) return false;
    if (const QString problem = annotationsProblem(a); !problem.isEmpty())
        return reader.fail(QStringLiteral("comments"), problem);
    *out = std::move(a);
    return true;
}

void insertMarks(const CharFormat &f, QJsonObject *o)
{
    if (!f.fieldId.isEmpty()) o->insert(QStringLiteral("field"), f.fieldId);
    if (!f.commentIds.isEmpty()) o->insert(QStringLiteral("comments"), QJsonArray::fromStringList(f.commentIds));
    if (!f.insertionId.isEmpty()) o->insert(QStringLiteral("insertion"), f.insertionId);
    if (!f.deletionId.isEmpty()) o->insert(QStringLiteral("deletion"), f.deletionId);
}

bool readMarks(const QJsonObject &o, const Annotations *known, CharFormat *f, const QString &path,
               PublishingError *error, QStringList *warnings)
{
    Reader reader{error, warnings};
    if (!reader.text(o, "field", &f->fieldId, path) || !reader.text(o, "insertion", &f->insertionId, path)
        || !reader.text(o, "deletion", &f->deletionId, path))
        return false;
    const QJsonValue comments = o.value(QStringLiteral("comments"));
    if (!comments.isUndefined()) {
        if (!comments.isArray()) return reader.fail(path + QStringLiteral(".comments"), QStringLiteral("Array expected"));
        for (const QJsonValue &id : comments.toArray()) {
            if (!id.isString() || id.toString().isEmpty())
                return reader.fail(path + QStringLiteral(".comments"), QStringLiteral("Comment identifier expected"));
            if (!f->commentIds.contains(id.toString())) f->commentIds.append(id.toString());
        }
        f->commentIds.sort();
    }
    if (!known) return true; // a fragment: the pasting document decides
    // AGENT-NOTE: like character styles, a session's live encoding keeps its
    // whole format pool, which may name a record whose creation was undone.
    // Such a mark is dropped with a warning instead of refusing the file.
    auto drop = [&](const QString &member) {
        if (warnings) warnings->append(QStringLiteral("%1.%2 ignored: unknown record").arg(path, member));
    };
    if (!f->fieldId.isEmpty() && !known->fields.contains(f->fieldId)) {
        drop(QStringLiteral("field"));
        f->fieldId.clear();
    }
    const auto change = [known](const QString &id, ChangeKind kind) {
        const auto found = known->changes.constFind(id);
        return found != known->changes.constEnd() && found->kind == kind;
    };
    if (!f->insertionId.isEmpty() && !change(f->insertionId, ChangeKind::Insertion)) {
        drop(QStringLiteral("insertion"));
        f->insertionId.clear();
    }
    if (!f->deletionId.isEmpty() && !change(f->deletionId, ChangeKind::Deletion)) {
        drop(QStringLiteral("deletion"));
        f->deletionId.clear();
    }
    const qsizetype before = f->commentIds.size();
    f->commentIds.removeIf([known](const QString &id) { return !known->threads.contains(id); });
    if (f->commentIds.size() != before) drop(QStringLiteral("comments"));
    return true;
}

} // namespace QindaTK::RichText::Codec
