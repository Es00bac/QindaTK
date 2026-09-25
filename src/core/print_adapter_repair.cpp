// SPDX-License-Identifier: LGPL-3.0-or-later
// Preflight repairs (D-132): each action is one document transaction or one
// ticket change, and preflight runs again to prove the issue is gone.
#include "print_adapter_p.h"
#include "rich_text_ops_p.h"

#include <QUrl>

namespace QindaTK {

using namespace RichText;

bool PrintAdapter::repair(const QString &issueId, const QString &actionId, const QVariant &argument)
{
    clearError();
    if (m_preflightStale) runPreflight();
    Print::Issue issue;
    bool found = false;
    for (const Print::Issue &candidate : std::as_const(m_issues))
        if (candidate.id == issueId) { issue = candidate; found = true; }
    if (!found) {
        setError(ErrorCode::InvalidArgument, tr("No preflight issue %1").arg(issueId));
        return false;
    }
    if (!issue.repairActionIds.contains(actionId)) {
        setError(ErrorCode::Unsupported, tr("%1 is not a repair for this issue").arg(actionId));
        return false;
    }
    const quint64 revision = m_document ? m_document->revision() : 0;
    const Print::Ticket ticketBefore = m_ticket;
    if (!applyRepair(issue, actionId, argument)) {
        if (m_lastError.isEmpty()) setError(ErrorCode::Validation, tr("The repair could not be applied"));
        return false;
    }
    runPreflight();
    bool cleared = true;
    for (const Print::Issue &remaining : std::as_const(m_issues)) cleared = cleared && remaining.id != issueId;
    if (cleared) return true;
    // AGENT-NOTE (D-132): a repair that leaves its issue in place is taken
    // back, so the document and ticket never keep a half-fix.
    if (m_document && m_document->revision() != revision) m_document->undo();
    m_ticket = ticketBefore;
    emit settingsChanged();
    invalidateOutput();
    runPreflight();
    setError(ErrorCode::Validation, tr("The repair did not resolve \"%1\"").arg(issue.title));
    return false;
}

bool PrintAdapter::applyRepair(const Print::Issue &issue, const QString &actionId, const QVariant &argument)
{
    const auto settings = [this](const QString &key, const QVariant &value) {
        return applySettings(QVariantMap{{key, value}});
    };
    if (actionId == QLatin1String("replace-font"))
        return replaceFont(issue.id.mid(int(qstrlen("font-missing:"))));
    if (actionId == QLatin1String("relink-image"))
        return relinkImage(issue.targetId.mid(int(qstrlen("object:"))), argument);
    if (actionId == QLatin1String("update-fields")) {
        if (m_document && m_document->updateFields()) return true;
        setError(ErrorCode::Validation, m_document ? m_document->lastError() : tr("There is no document"));
        return false;
    }
    if (actionId == QLatin1String("use-supported")) {
        const QVariant value = Print::supportedValue(destination(), issue.fieldPath);
        if (!value.isValid()) {
            setError(ErrorCode::Unsupported, tr("%1 offers no alternative for this setting").arg(destination().label));
            return false;
        }
        return settings(issue.fieldPath, value);
    }
    if (actionId == QLatin1String("print-all-pages")) return settings(QStringLiteral("pages"), QString());
    if (actionId == QLatin1String("fit-to-paper")) return settings(QStringLiteral("scaleMode"), QStringLiteral("fit"));
    if (actionId == QLatin1String("disable-bleed")) return settings(QStringLiteral("includeBleed"), false);
    if (actionId == QLatin1String("choose-output-file")) {
        const QString path = argument.typeId() == QMetaType::QUrl ? argument.toUrl().toString() : argument.toString();
        if (Print::localPath(path).isEmpty()) {
            setError(ErrorCode::InvalidArgument, tr("Choose a local file to write"));
            return false;
        }
        return settings(QStringLiteral("outputPath"), path);
    }
    setError(ErrorCode::Unsupported, tr("Unknown repair %1").arg(actionId));
    return false;
}

bool PrintAdapter::replaceFont(const QString &family)
{
    if (!m_document || family.isEmpty()) return false;
    RichTextDocument &doc = *m_document;
    const QString replacement = Print::replacementFamily(doc, family);
    if (!doc.beginTransaction(tr("Replace font %1").arg(family))) {
        setError(ErrorCode::Conflict, doc.lastError());
        return false;
    }
    bool ok = true;
    const QVector<Paragraph *> paragraphs = doc.paragraphsInOrder();
    for (const Paragraph *p : paragraphs) {
        if (!ok) break;
        ok = Detail::rewriteFormats(doc, *p, 0, int(p->text.size()), [&](CharFormat &format) {
            if (format.family == family) format.family = replacement;
        });
        if (ok && doc.charFormat(p->markFormat).family == family) {
            CharFormat mark = doc.charFormat(p->markFormat);
            mark.family = replacement;
            StepSpans step;
            step.paragraph = p->id;
            step.markBefore = p->markFormat;
            step.markAfter = doc.internFormat(mark);
            ok = !doc.apply(step);
        }
    }
    QStringList styles;
    for (const NamedStyle &style : doc.styleSheet())
        if (style.character.family && *style.character.family == family) styles.append(style.id);
    for (const QString &id : std::as_const(styles))
        ok = ok && doc.updateStyle(id, {{QStringLiteral("character"), QVariantMap{{QStringLiteral("family"), replacement}}}});
    if (ok && doc.defaults().family == family) {
        DocumentDefaults defaults = doc.defaults();
        defaults.family = replacement;
        ok = doc.setDefaults(defaults);
    }
    if (!ok) {
        const QString message = doc.lastError();
        doc.cancelTransaction();
        setError(ErrorCode::Validation, message.isEmpty() ? tr("Cannot replace %1").arg(family) : message);
        return false;
    }
    return doc.commitTransaction();
}

bool PrintAdapter::relinkImage(const QString &objectId, const QVariant &argument)
{
    const RichObject *object = m_document ? m_document->object(objectId) : nullptr;
    if (!object) {
        setError(ErrorCode::InvalidArgument, tr("The image is no longer in the document"));
        return false;
    }
    QString source = argument.typeId() == QMetaType::QUrl ? argument.toUrl().toString() : argument.toString();
    if (!source.isEmpty() && QUrl(source).scheme().isEmpty()) source = QUrl::fromLocalFile(source).toString();
    RichTextImageRenderer probe;
    if (source.isEmpty() || probe.image(source).isNull()) {
        setError(ErrorCode::Validation, tr("%1 is not a readable image").arg(source.isEmpty() ? tr("The file") : source));
        return false;
    }
    RichObject next = *object;
    next.source = source;
    if (!m_document->setObject(next)) {
        setError(ErrorCode::Validation, m_document->lastError());
        return false;
    }
    return true;
}

} // namespace QindaTK
