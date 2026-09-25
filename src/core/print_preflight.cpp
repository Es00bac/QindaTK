// SPDX-License-Identifier: LGPL-3.0-or-later
#include "print_preflight.h"

#include "rich_text_document.h"
#include "rich_text_layout.h"
#include "rich_text_objects.h"
#include "rich_text_spans.h"
#include <QCoreApplication>
#include <QHash>
#include <QtMath>

namespace QindaTK::Print {
namespace {

using namespace QindaTK::RichText;

QString tr(const char *text) { return QCoreApplication::translate("QindaTK::Print", text); }
QString trn(const char *text, int n) { return QCoreApplication::translate("QindaTK::Print", text, nullptr, n); }
QString sid(const QUuid &id) { return id.toString(QUuid::WithoutBraces); }

struct Context {
    const PreflightInput &in;
    RichTextDocument &doc;
    RichTextLayout &layout;
    QVector<Issue> issues;
    QVector<Sheet> sheets;
    double scale = 1.0;
    QString minor() const { return in.profile.strict ? QStringLiteral("error") : QStringLiteral("warning"); }
    Issue &add(const QString &id, const QString &severity, const QString &category, const QString &title,
               const QString &description, const QString &targetId, const QString &scope)
    {
        Issue issue;
        issue.id = id;
        issue.severity = severity;
        issue.category = category;
        issue.title = title;
        issue.description = description;
        issue.targetId = targetId;
        issue.scope = scope;
        issues.append(issue);
        return issues.last();
    }
    void locate(Issue &issue, const QString &paragraphId, int offset)
    {
        issue.paragraphId = paragraphId;
        issue.offset = offset;
        const QUuid id(paragraphId);
        if (!paragraphId.isEmpty() && doc.paragraph(id)) issue.pageIndex = layout.pageOfPosition(Position{id, offset});
    }
    void locate(Issue &issue, const QVariant &location)
    {
        const QVariantMap map = location.toMap();
        if (!map.isEmpty())
            locate(issue, map.value(QStringLiteral("paragraphId")).toString(), map.value(QStringLiteral("offset")).toInt());
    }
};

void repairWith(Issue &issue, const QString &action, const QString &label, bool available = true)
{
    issue.repairActionIds = {action};
    issue.repairLabel = label;
    issue.repairAvailable = available;
}

void checkOutput(Context &c)
{
    const Capabilities &d = c.in.destination;
    const int pageCount = c.layout.pageCount();
    for (const ValidationIssue &v : validateTicket(c.in.ticket, d, pageCount)) {
        const bool unavailable = v.code == ErrorCode::Unavailable;
        Issue &issue = c.add(QStringLiteral("output-") + v.fieldPath, unavailable ? QStringLiteral("blocker") : QStringLiteral("error"),
                             QStringLiteral("Output"), unavailable ? tr("Destination unavailable") : tr("Output setting not possible"),
                             v.message, QStringLiteral("setting:") + v.fieldPath, QStringLiteral("output"));
        issue.fieldPath = v.fieldPath;
        if (v.fieldPath == QLatin1String("pages")) repairWith(issue, QStringLiteral("print-all-pages"), tr("Print all pages"));
        else if (v.fieldPath == QLatin1String("outputPath")) repairWith(issue, QStringLiteral("choose-output-file"), tr("Choose file…"));
        else if (const QVariant value = supportedValue(d, v.fieldPath); value.isValid())
            repairWith(issue, QStringLiteral("use-supported"), value.typeId() == QMetaType::Bool
                           ? tr("Turn it off") : tr("Use %1").arg(optionLabel(v.fieldPath, value.toString())));
    }
    const PageSetup page = c.doc.pageSetupValue();
    bool clipped = false;
    for (const Sheet &sheet : std::as_const(c.sheets)) clipped = clipped || sheet.clipped;
    if (clipped) {
        Issue &issue = c.add(QStringLiteral("output-clipped"), QStringLiteral("warning"), QStringLiteral("Output"),
                             tr("Pages do not fit the paper"),
                             tr("At %1 % the page overruns the sheet and its edges are cut off.").arg(qRound(c.scale * 100)),
                             QStringLiteral("setting:scaleMode"), QStringLiteral("output"));
        issue.fieldPath = QStringLiteral("scaleMode");
        if (d.scaleModes.contains(QStringLiteral("fit"))) repairWith(issue, QStringLiteral("fit-to-paper"), tr("Fit to paper"));
    }
    const QMarginsF edge = d.printableMarginsPt;
    if (!c.sheets.isEmpty() && !edge.isNull()) {
        const Sheet &sheet = c.sheets.first();
        const QRectF content = sheet.trimPt.adjusted(page.marginLeftPt * sheet.scale, page.marginTopPt * sheet.scale,
                                                     -page.marginRightPt * sheet.scale, -page.marginBottomPt * sheet.scale);
        const QRectF printable = QRectF(QPointF(0, 0), sheet.sizePt).marginsRemoved(edge);
        if (!printable.contains(content)) {
            Issue &issue = c.add(QStringLiteral("output-unprintable-edge"), QStringLiteral("warning"), QStringLiteral("Output"),
                                 tr("Text reaches the printer's unprintable edge"),
                                 tr("%1 cannot mark the outer %2 pt of the sheet; widen the page margins or scale the page down.")
                                     .arg(d.label).arg(qRound(qMax(qMax(edge.left(), edge.right()), qMax(edge.top(), edge.bottom())))),
                                 QStringLiteral("setting:scaleMode"), QStringLiteral("output"));
            issue.fieldPath = QStringLiteral("scaleMode");
        }
    }
    if (c.in.ticket.includeBleed && page.bleedPt <= 0) {
        Issue &issue = c.add(QStringLiteral("output-bleed-missing"), QStringLiteral("warning"), QStringLiteral("Output"),
                             tr("Bleed requested but the page has none"),
                             tr("Set a bleed in page setup, or print without bleed."),
                             QStringLiteral("setting:includeBleed"), QStringLiteral("output"));
        issue.fieldPath = QStringLiteral("includeBleed");
        repairWith(issue, QStringLiteral("disable-bleed"), tr("Print without bleed"));
    }
}

void checkFonts(Context &c)
{
    const QStringList missing = c.doc.missingFontFamilies();
    for (const QString &family : missing) {
        QString first;
        int paragraphs = 0;
        for (const Paragraph *p : c.doc.paragraphsInOrder()) {
            bool uses = c.doc.charFormat(p->markFormat).family == family;
            for (const Span &span : p->spans)
                uses = uses || c.doc.effectiveCharFormat(c.doc.charFormat(span.format), p->format).family == family;
            if (!uses) continue;
            ++paragraphs;
            if (first.isEmpty()) first = sid(p->id);
        }
        const QString replacement = replacementFamily(c.doc, family);
        Issue &issue = c.add(QStringLiteral("font-missing:") + family, c.minor(), QStringLiteral("Fonts"),
                             tr("Missing font: %1").arg(family),
                             trn("%1 is not installed; %n paragraph(s) print with a substitute.", paragraphs).arg(family),
                             QStringLiteral("font:") + family, QStringLiteral("document"));
        c.locate(issue, first, 0);
        repairWith(issue, QStringLiteral("replace-font"), tr("Use %1").arg(replacement));
    }
}

void checkImages(Context &c)
{
    for (const Paragraph *p : c.doc.paragraphsInOrder())
        for (int i = 0; i < p->text.size(); ++i) {
            if (p->text.at(i) != QChar(ObjectChar)) continue;
            const RichObject *object = c.doc.object(c.doc.charFormat(Spans::formatAt(p->spans, i)).objectId);
            if (!object || object->kind != QLatin1String("image")) continue;
            const QString name = object->altText.isEmpty() ? object->id : object->altText;
            const QString target = QStringLiteral("object:") + object->id;
            if (object->source.startsWith(QLatin1String("asset:"))) {
                Issue &issue = c.add(QStringLiteral("image-unresolved:") + object->id, QStringLiteral("error"),
                                     QStringLiteral("Assets"), tr("Image is not available for output: %1").arg(name),
                                     tr("Output reads image files directly; this image comes from the asset library. Link a file instead."),
                                     target, QStringLiteral("object"));
                c.locate(issue, sid(p->id), i);
                repairWith(issue, QStringLiteral("relink-image"), tr("Link a file…"));
                continue;
            }
            const QImage image = c.in.images ? c.in.images->image(object->source) : QImage();
            if (image.isNull()) {
                Issue &issue = c.add(QStringLiteral("image-missing:") + object->id, QStringLiteral("error"),
                                     QStringLiteral("Assets"), tr("Missing image: %1").arg(name),
                                     object->source.isEmpty() ? tr("The image has no source; output shows a placeholder.")
                                                              : tr("%1 cannot be read; output shows a placeholder.").arg(object->source),
                                     target, QStringLiteral("object"));
                c.locate(issue, sid(p->id), i);
                repairWith(issue, QStringLiteral("relink-image"), tr("Link a file…"));
                continue;
            }
            // Effective resolution on the sheet: image pixels over printed inches.
            const double widthIn = object->widthPt * c.scale / 72.0;
            const double heightIn = object->heightPt * c.scale / 72.0;
            if (widthIn <= 0 || heightIn <= 0) continue;
            const double dpi = qMin(image.width() / widthIn, image.height() / heightIn);
            if (dpi + 1e-6 >= c.in.profile.minimumDpi) continue;
            Issue &issue = c.add(QStringLiteral("image-resolution:") + object->id, c.minor(), QStringLiteral("Assets"),
                                 tr("Low image resolution: %1").arg(name),
                                 tr("Prints at %1 dpi; the %2 profile asks for %3 dpi.").arg(qFloor(dpi))
                                     .arg(c.in.profile.label).arg(qRound(c.in.profile.minimumDpi)),
                                 target, QStringLiteral("object"));
            c.locate(issue, sid(p->id), i);
        }
}

void checkLayout(Context &c)
{
    if (c.layout.overset()) {
        const QStringList chain = c.doc.frameSet().storyChain();
        const QString last = chain.isEmpty() ? QString() : chain.last();
        Issue &issue = c.add(QStringLiteral("text-overset:") + last, QStringLiteral("error"), QStringLiteral("Layout"),
                             tr("Text does not fit its frames"),
                             trn("%n character(s) after the last frame are not printed.", c.layout.oversetCharacters()),
                             QStringLiteral("frame:") + last, QStringLiteral("page"));
        const TextFrame *frame = c.doc.frameSet().frame(last);
        issue.pageIndex = frame ? frame->page : -1;
    }
    for (int index = 0; index < c.layout.pageCount(); ++index) {
        const Page &page = c.layout.page(index);
        if (!page.overflow) continue;
        const QString block = page.items.isEmpty() ? sid(page.key) : sid(page.items.first().block);
        Issue &issue = c.add(QStringLiteral("page-overflow:") + block, QStringLiteral("warning"), QStringLiteral("Layout"),
                             tr("Content is taller than page %1").arg(index + 1),
                             tr("An item that cannot break is cut off at the bottom of the page."),
                             QStringLiteral("page:%1").arg(index + 1), QStringLiteral("page"));
        c.locate(issue, sid(page.first.paragraph), page.first.offset);
        issue.pageIndex = index;
    }
}

void checkFields(Context &c)
{
    QVariantMap firstStale;
    int stale = 0;
    for (const QVariant &value : c.doc.referenceSnapshots()) {
        const QVariantMap field = value.toMap();
        const QString status = field.value(QStringLiteral("status")).toString();
        const QString id = field.value(QStringLiteral("id")).toString();
        if (status == QLatin1String("broken")) {
            Issue &issue = c.add(QStringLiteral("field-broken:") + id, QStringLiteral("error"), QStringLiteral("Fields"),
                                 tr("Broken reference: %1").arg(field.value(QStringLiteral("label")).toString()),
                                 field.value(QStringLiteral("error")).toString(), QStringLiteral("field:") + id,
                                 QStringLiteral("document"));
            c.locate(issue, field.value(QStringLiteral("location")));
        } else if (status == QLatin1String("stale")) {
            if (stale++ == 0) firstStale = field;
        }
    }
    if (stale > 0) {
        Issue &issue = c.add(QStringLiteral("fields-stale"), QStringLiteral("warning"), QStringLiteral("Fields"),
                             tr("Fields are out of date"),
                             trn("%n field result(s) differ from their numbering or sources.", stale),
                             QStringLiteral("field:") + firstStale.value(QStringLiteral("id")).toString(), QStringLiteral("document"));
        c.locate(issue, firstStale.value(QStringLiteral("location")));
        repairWith(issue, QStringLiteral("update-fields"), tr("Update fields"));
    }
    for (const QVariant &value : c.doc.equationSnapshots()) {
        const QVariantMap equation = value.toMap();
        if (equation.value(QStringLiteral("status")).toString() != QLatin1String("error")) continue;
        const QString id = equation.value(QStringLiteral("id")).toString();
        Issue &issue = c.add(QStringLiteral("equation-invalid:") + id, QStringLiteral("error"), QStringLiteral("Fields"),
                             tr("Equation cannot be typeset"), equation.value(QStringLiteral("error")).toString(),
                             QStringLiteral("equation:") + id, QStringLiteral("object"));
        c.locate(issue, equation.value(QStringLiteral("location")));
    }
}

void checkReview(Context &c)
{
    int changes = 0, comments = 0;
    QVariantMap first;
    for (const QVariant &value : c.doc.reviewItems()) {
        const QVariantMap item = value.toMap();
        if (item.value(QStringLiteral("status")).toString() != QLatin1String("open")) continue;
        (item.value(QStringLiteral("kind")).toString() == QLatin1String("change") ? changes : comments)++;
        if (first.isEmpty()) first = item;
    }
    if (changes + comments == 0) return;
    Issue &issue = c.add(QStringLiteral("review-markup"), c.minor(), QStringLiteral("Review"),
                         tr("Review marks are still open"),
                         tr("%1 tracked change(s) print as underlined or struck-through text; %2 open comment(s) are not printed.")
                             .arg(changes).arg(comments),
                         QStringLiteral("review:") + first.value(QStringLiteral("id")).toString(), QStringLiteral("document"));
    c.locate(issue, first.value(QStringLiteral("location")));
}

} // namespace

QVector<Profile> profiles()
{
    return {{QStringLiteral("print"), tr("Office print"), 150, false},
            {QStringLiteral("press"), tr("Commercial press"), 300, true}};
}

Profile profile(const QString &id)
{
    const QVector<Profile> all = profiles();
    for (const Profile &p : all)
        if (p.id == id) return p;
    return all.first();
}

QVariantMap Issue::toMap() const
{
    return {{QStringLiteral("id"), id},
            {QStringLiteral("severity"), severity},
            {QStringLiteral("category"), category},
            {QStringLiteral("title"), title},
            {QStringLiteral("description"), description},
            {QStringLiteral("targetId"), targetId},
            {QStringLiteral("scope"), scope},
            {QStringLiteral("repairActionIds"), repairActionIds},
            {QStringLiteral("repairLabel"), repairLabel},
            {QStringLiteral("repairAvailable"), repairAvailable},
            {QStringLiteral("paragraphId"), paragraphId},
            {QStringLiteral("offset"), offset},
            {QStringLiteral("pageIndex"), pageIndex},
            {QStringLiteral("fieldPath"), fieldPath}};
}

bool blocksOutput(const Issue &issue)
{
    return issue.severity == QLatin1String("blocker") || issue.severity == QLatin1String("error");
}

QString replacementFamily(const RichTextDocument &document, const QString &missing)
{
    const QString family = document.defaults().family;
    if (family != missing && !document.missingFontFamilies().contains(family)) return family;
    return QStringLiteral("sans-serif");
}

QVector<Issue> preflight(const PreflightInput &input)
{
    if (!input.document || !input.layout) return {};
    Context c{input, *input.document, *input.layout, {}, {}, 1.0};
    const Paper paper = resolvePaper(input.destination, input.ticket.paperId);
    const PageSetup page = c.doc.pageSetupValue();
    c.sheets = planSheets(page, c.layout.pageCount(), input.ticket, paper);
    c.scale = c.sheets.isEmpty() ? sheetFor(page, 0, input.ticket, paper).scale : c.sheets.first().scale;
    checkOutput(c);
    checkFonts(c);
    checkImages(c);
    checkLayout(c);
    checkFields(c);
    checkReview(c);
    return c.issues;
}

} // namespace QindaTK::Print
