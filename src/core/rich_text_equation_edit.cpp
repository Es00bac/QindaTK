// SPDX-License-Identifier: LGPL-3.0-or-later
// Equations in the document (WP-052, D-114): inline objects of kind
// "equation" whose `source` is the canonical TeX-subset source and whose
// `altText` is the linear text. Their size comes from the equation layout,
// so they take their place in lines, pages, PDF output and undo like any
// inline object (docs/references-review.md#equations).
#include "rich_text_document.h"
#include "rich_text_equation.h"
#include "rich_text_ops_p.h"
#include "rich_text_spans.h"

#include <cmath>

namespace QindaTK {
using namespace RichText;
using namespace RichText::Detail;

namespace {

bool isEquation(const RichObject *object) { return object && object->kind == u"equation"; }

QString codeName(ErrorCode code)
{
    return code == ErrorCode::Unsupported ? QStringLiteral("unsupported") : QStringLiteral("invalidArgument");
}

QString problemText(const Equation::ParseError &error)
{
    return RichTextDocument::tr("%1 (at character %2)").arg(error.message).arg(error.position + 1);
}

// The object an equation source lays out to, or the reason it cannot.
std::optional<RichObject> equationObject(const QString &source, double sizePt, Equation::ParseError *error)
{
    const Equation::Parsed parsed = Equation::parse(source);
    if (!parsed.root) {
        *error = parsed.error;
        return std::nullopt;
    }
    const Equation::Box box = Equation::layout(*parsed.root, sizePt);
    if (!(box.width > 0) || !(box.height() > 0)) {
        *error = {ErrorCode::InvalidArgument, QStringLiteral("An equation needs content"), 0};
        return std::nullopt;
    }
    RichObject object;
    object.kind = QStringLiteral("equation");
    object.placement = Placement::Inline;
    object.source = Equation::canonicalSource(*parsed.root);
    object.altText = Equation::linearText(*parsed.root);
    // Hundredths of a point keep the saved numbers short; painting scales.
    object.widthPt = std::ceil(box.width * 100) / 100;
    object.heightPt = std::ceil(box.height() * 100) / 100;
    return object;
}

// Anchor positions of equation objects, in document order.
QVector<std::pair<QString, Position>> equationAnchors(const RichTextDocument &doc)
{
    QVector<std::pair<QString, Position>> anchors;
    for (const Paragraph *p : doc.paragraphsInOrder())
        for (int i = 0; i < p->text.size(); ++i) {
            if (p->text.at(i) != QChar(ObjectChar)) continue;
            const QString id = doc.charFormat(Spans::formatAt(p->spans, i)).objectId;
            if (isEquation(doc.object(id))) anchors.append({id, Position{p->id, i}});
        }
    return anchors;
}

QVariantMap snapshot(const RichObject &object, const Position &anchor)
{
    const Equation::Parsed parsed = Equation::parse(object.source);
    QStringList symbols;
    for (const Equation::Symbol &symbol : Equation::palette()) symbols.append(symbol.id);
    QVariantMap map{{QStringLiteral("id"), object.id},
                    {QStringLiteral("source"), object.source},
                    {QStringLiteral("linearText"), parsed.root ? Equation::linearText(*parsed.root) : object.altText},
                    {QStringLiteral("status"), parsed.root ? QStringLiteral("ready") : QStringLiteral("error")},
                    {QStringLiteral("supported"), parsed.error.code != ErrorCode::Unsupported},
                    {QStringLiteral("supportedSymbols"), symbols},
                    {QStringLiteral("widthPt"), object.widthPt},
                    {QStringLiteral("heightPt"), object.heightPt},
                    {QStringLiteral("location"), QVariantMap{{QStringLiteral("paragraphId"), anchor.paragraph.toString(QUuid::WithoutBraces)},
                                                             {QStringLiteral("offset"), anchor.offset}}}};
    if (!parsed.root) {
        map.insert(QStringLiteral("error"), problemText(parsed.error));
        map.insert(QStringLiteral("errorCode"), codeName(parsed.error.code));
        map.insert(QStringLiteral("errorPosition"), parsed.error.position);
    }
    return map;
}

} // namespace

QVariantList RichTextDocument::equationSnapshots() const
{
    QVariantList list;
    for (const auto &[id, anchor] : equationAnchors(*this)) list.append(snapshot(*object(id), anchor));
    return list;
}

QVariantMap RichTextDocument::equationSnapshot(const QString &equationId) const
{
    for (const auto &[id, anchor] : equationAnchors(*this))
        if (id == equationId) return snapshot(*object(id), anchor);
    return {};
}

QVariantList RichTextDocument::equationSymbols() const
{
    QVariantList list;
    for (const Equation::Symbol &symbol : Equation::palette())
        list.append(QVariantMap{{QStringLiteral("id"), symbol.id},
                                {QStringLiteral("label"), symbol.label},
                                {QStringLiteral("source"), symbol.source},
                                {QStringLiteral("tooltip"), tr("Insert %1").arg(symbol.label)}});
    return list;
}

QVariantMap RichTextDocument::validateEquation(const QString &source) const
{
    const Equation::Parsed parsed = Equation::parse(source);
    if (!parsed.root)
        return {{QStringLiteral("valid"), false},
                {QStringLiteral("code"), codeName(parsed.error.code)},
                {QStringLiteral("message"), problemText(parsed.error)},
                {QStringLiteral("position"), parsed.error.position}};
    return {{QStringLiteral("valid"), !parsed.root->children.empty()},
            {QStringLiteral("code"), parsed.root->children.empty() ? codeName(ErrorCode::InvalidArgument) : QString()},
            {QStringLiteral("message"), parsed.root->children.empty() ? tr("An equation needs content") : QString()},
            {QStringLiteral("position"), -1},
            {QStringLiteral("canonical"), Equation::canonicalSource(*parsed.root)},
            {QStringLiteral("linearText"), Equation::linearText(*parsed.root)}};
}

QString RichTextDocument::equationSourceWithSymbol(const QString &source, int cursor, const QString &symbolId) const
{
    return Equation::withSymbol(source, cursor, symbolId);
}

std::optional<Position> RichTextDocument::insertEquationAt(const Position &at, const QString &source, QString *equationId)
{
    Equation::ParseError problem;
    std::optional<RichObject> equation = equationObject(source, m_defaults.sizePt, &problem);
    if (!equation) {
        reject(problem.code, problemText(problem));
        return std::nullopt;
    }
    equation->id = newAnnotationId();
    if (equationId) *equationId = equation->id;
    AutoTransaction transaction(this, tr("Insert equation"));
    if (!transaction.valid) return std::nullopt;
    const std::optional<Position> caret = insertObject(at, *equation);
    if (!transaction.finish(caret.has_value(), caret.value_or(at))) return std::nullopt;
    return caret;
}

QString RichTextDocument::insertEquation(const QString &paragraphId, int offset, const QString &source)
{
    QString id;
    return insertEquationAt({QUuid(paragraphId), offset}, source, &id) ? id : QString();
}

bool RichTextDocument::setEquationSource(const QString &equationId, const QString &source)
{
    const RichObject *current = object(equationId);
    if (!isEquation(current)) return !reject(ErrorCode::InvalidArgument, QStringLiteral("Unknown equation"));
    Equation::ParseError problem;
    std::optional<RichObject> next = equationObject(source, m_defaults.sizePt, &problem);
    if (!next) return !reject(problem.code, problemText(problem));
    next->id = current->id;
    next->placement = current->placement;
    next->offsetXPt = current->offsetXPt;
    next->offsetYPt = current->offsetYPt;
    next->gapPt = current->gapPt;
    if (*next == *current) return true;
    AutoTransaction transaction(this, tr("Edit equation"));
    if (!transaction.valid) return false;
    // One StepObject: the new size re-lays the anchor's paragraph, and undo
    // restores the old source and size together.
    return transaction.finish(!apply(StepObject{equationId, *current, *next}), {});
}

} // namespace QindaTK
