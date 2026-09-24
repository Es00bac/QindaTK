// SPDX-License-Identifier: LGPL-3.0-or-later
// Named styles of RichTextDocument (WP-051): resolution cache, StepStyle,
// create/update/apply, direct-override reports and resets.
#include "rich_text_codec_p.h"
#include "rich_text_document.h"
#include "rich_text_ops_p.h"
#include "rich_text_spans.h"

#include <QJsonArray>
#include <algorithm>

namespace QindaTK {
using namespace RichText;
using namespace RichText::Detail;

namespace {

QString number(double value, const char *unit = "pt")
{
    return QString::number(value, 'g', 6) + u' ' + QLatin1String(unit);
}

// Display value of one StyleProperty member (bit index) of a paragraph format.
QString paragraphValue(const ParagraphFormat &f, int bit)
{
    switch (bit) {
    case 0: return Codec::alignmentNames().at(int(f.alignment));
    case 1: return Codec::directionNames().at(int(f.direction));
    case 2: return number(f.indentStartPt);
    case 3: return number(f.indentEndPt);
    case 4: return number(f.firstLineIndentPt);
    case 5: return number(f.spaceBeforePt);
    case 6: return number(f.spaceAfterPt);
    case 7: return QString::number(f.lineHeight, 'g', 6) + QStringLiteral("×");
    case 8: return number(f.lineHeightPt);
    case 9: return QString::number(f.headingLevel);
    case 10: return f.keepWithNext ? QStringLiteral("on") : QStringLiteral("off");
    case 11: return f.keepTogether ? QStringLiteral("on") : QStringLiteral("off");
    default: return {};
    }
}

// Display value of a character member set directly on `f`, or "" when it inherits.
QString characterValue(const CharFormat &f, int index)
{
    switch (index) {
    case 0: return f.family;
    case 1: return f.sizePt > 0 ? number(f.sizePt) : QString();
    case 2: return f.weight > 0 ? QString::number(f.weight) : QString();
    case 3: return f.italic < 0 ? QString() : f.italic ? QStringLiteral("on") : QStringLiteral("off");
    case 4: return f.underline < 0 ? QString() : f.underline ? QStringLiteral("on") : QStringLiteral("off");
    case 5: return f.strike < 0 ? QString() : f.strike ? QStringLiteral("on") : QStringLiteral("off");
    case 6: return f.color.isValid() ? Codec::colorName(f.color) : QString();
    case 7: return f.background.isValid() ? Codec::colorName(f.background) : QString();
    case 8: return f.verticalAlign == VerticalAlign::Normal ? QString() : Codec::verticalAlignNames().at(int(f.verticalAlign));
    default: return {};
    }
}

CharFormat withoutCharacter(CharFormat f, const QSet<QString> &names)
{
    const QStringList &all = Codec::characterPropertyNames();
    if (names.contains(all.at(0))) f.family.clear();
    if (names.contains(all.at(1))) f.sizePt = 0;
    if (names.contains(all.at(2))) f.weight = 0;
    if (names.contains(all.at(3))) f.italic = -1;
    if (names.contains(all.at(4))) f.underline = -1;
    if (names.contains(all.at(5))) f.strike = -1;
    if (names.contains(all.at(6))) f.color = QColor();
    if (names.contains(all.at(7))) f.background = QColor();
    if (names.contains(all.at(8))) f.verticalAlign = VerticalAlign::Normal;
    return f;
}

// "sizePt 18 · weight 700": the resolved members of a style, for a preview line.
QString describe(const QJsonObject &members)
{
    QStringList parts;
    for (auto it = members.begin(); it != members.end(); ++it)
        parts.append(it.key() + u' ' + (it.value().isBool() ? (it.value().toBool() ? QStringLiteral("on") : QStringLiteral("off"))
                                                            : it.value().toVariant().toString()));
    return parts.join(QStringLiteral(" · "));
}

} // namespace

const NamedStyle *RichTextDocument::style(const QString &id) const
{
    const auto found = m_styles.constFind(id);
    return found == m_styles.constEnd() ? nullptr : &found.value();
}

CharFormat RichTextDocument::effectiveCharFormat(const CharFormat &direct, const ParagraphFormat &paragraph) const
{
    if (m_styles.isEmpty() || (direct.styleId.isEmpty() && paragraph.styleId.isEmpty())) return direct;
    auto resolved = [this](const QString &id) {
        auto found = m_resolvedCharacter.constFind(id);
        if (found == m_resolvedCharacter.constEnd())
            found = m_resolvedCharacter.insert(id, resolvedCharacter(m_styles, id));
        return found.value();
    };
    CharFormat format = direct;
    if (!direct.styleId.isEmpty()) format = inheritCharacter(format, resolved(direct.styleId));
    if (!paragraph.styleId.isEmpty()) format = inheritCharacter(format, resolved(paragraph.styleId));
    return format;
}

TableStyle RichTextDocument::tableStyle(const Table &table) const
{
    return table.styleId.isEmpty() ? TableStyle{} : resolvedTable(m_styles, table.styleId);
}

OperationError RichTextDocument::applyStyleStep(const StepStyle &step, bool forward, ChangeSet &change)
{
    const std::optional<NamedStyle> &from = forward ? step.before : step.after;
    const std::optional<NamedStyle> &to = forward ? step.after : step.before;
    const NamedStyle *current = style(step.styleId);
    if (bool(current) != bool(from) || (current && !(*current == *from)))
        return reject(ErrorCode::Conflict, QStringLiteral("Style changed since the edit"));
    StyleSheet next = m_styles;
    if (to) next.insert(step.styleId, *to);
    else next.remove(step.styleId);
    if (const QString problem = styleSheetProblem(next); !problem.isEmpty())
        return reject(ErrorCode::Validation, problem);
    // A style leaves the sheet only while nothing refers to it (linear
    // history removes it by undoing its creation after its uses).
    if (from && (!to || to->kind != from->kind)) {
        bool used = false;
        for (const Table *t : std::as_const(m_tables)) used = used || t->styleId == step.styleId;
        for (const Paragraph *p : paragraphsInOrder()) {
            used = used || p->format.styleId == step.styleId || charFormat(p->markFormat).styleId == step.styleId;
            for (int i = 0; !used && i < p->spans.size(); ++i) used = charFormat(p->spans.at(i).format).styleId == step.styleId;
            if (used) break;
        }
        if (used) return reject(ErrorCode::Conflict, QStringLiteral("The style is still in use"));
    }
    const bool renamed = from && to && from->kind == to->kind && from->parentId == to->parentId
        && from->character == to->character && from->paragraph == to->paragraph && from->table == to->table;
    QSet<QString> affected = styleFamily(m_styles, step.styleId);
    affected.unite(styleFamily(next, step.styleId));
    m_styles = std::move(next);
    m_resolvedCharacter.clear();
    if (renamed) return {};
    // Text and tables that take their look from the style (or a style that
    // inherits from it) re-lay out: their revisions change.
    QSet<int> formats;
    for (int i = 0; i < m_formats.size(); ++i)
        if (!m_formats.at(i).styleId.isEmpty() && affected.contains(m_formats.at(i).styleId)) formats.insert(i);
    for (Paragraph *p : paragraphsInOrder()) {
        bool uses = (!p->format.styleId.isEmpty() && affected.contains(p->format.styleId)) || formats.contains(p->markFormat);
        for (int i = 0; !uses && i < p->spans.size(); ++i) uses = formats.contains(p->spans.at(i).format);
        if (uses) touch(p, change);
    }
    for (Table *t : std::as_const(m_tables))
        if (!t->styleId.isEmpty() && affected.contains(t->styleId)) {
            t->revision = ++m_contentCounter;
            change.tables.insert(t->id);
        }
    return {};
}

bool RichTextDocument::commitStyle(const QString &label, std::optional<NamedStyle> after, const QString &styleId)
{
    const NamedStyle *current = style(styleId);
    const std::optional<NamedStyle> before = current ? std::optional<NamedStyle>(*current) : std::nullopt;
    if (before == after) return true;
    AutoTransaction transaction(this, label);
    if (!transaction.valid) return false;
    bool ok = !apply(StepStyle{styleId, before, after});
    // Paragraph values follow their style except where set directly.
    if (ok) {
        const QSet<QString> family = styleFamily(m_styles, styleId);
        const QVector<Paragraph *> flat = paragraphsInOrder();
        for (Paragraph *p : flat) {
            if (!ok) break;
            if (p->format.styleId.isEmpty() || !family.contains(p->format.styleId)) continue;
            const ParagraphFormat resolved = materializedFormat(m_styles, p->format);
            if (!(resolved == p->format)) ok = !apply(StepParagraphFormat{p->id, p->format, resolved});
        }
    }
    return transaction.finish(ok, {});
}

QStringList RichTextDocument::styleKinds() const
{
    return {styleKindName(StyleKind::Paragraph), styleKindName(StyleKind::Character), styleKindName(StyleKind::Table)};
}

QVariantList RichTextDocument::styleSnapshots() const
{
    QList<const NamedStyle *> sorted;
    for (const NamedStyle &named : m_styles) sorted.append(&named);
    std::sort(sorted.begin(), sorted.end(), [](const NamedStyle *a, const NamedStyle *b) {
        const int order = a->name.localeAwareCompare(b->name);
        return order != 0 ? order < 0 : a->id < b->id;
    });
    QVariantList result;
    for (const NamedStyle *named : std::as_const(sorted)) {
        const NamedStyle *parent = style(named->parentId);
        const QString kind = styleKindName(named->kind);
        QJsonObject resolved = Codec::characterPatchJson(resolvedCharacter(m_styles, named->id));
        const QJsonObject paragraph = Codec::paragraphPatchJson(resolvedParagraph(m_styles, named->id));
        const QJsonObject table = Codec::tableStyleJson(resolvedTable(m_styles, named->id));
        for (auto it = paragraph.begin(); it != paragraph.end(); ++it) resolved.insert(it.key(), it.value());
        for (auto it = table.begin(); it != table.end(); ++it) resolved.insert(it.key(), it.value());
        result.append(QVariantMap{
            {QStringLiteral("id"), named->id}, {QStringLiteral("name"), named->name},
            {QStringLiteral("kind"), kind}, {QStringLiteral("parentId"), named->parentId},
            {QStringLiteral("parentName"), parent ? parent->name : QString()},
            {QStringLiteral("builtIn"), false},
            {QStringLiteral("summary"), parent ? tr("%1 style · inherits %2").arg(kind, parent->name) : tr("%1 style").arg(kind)},
            {QStringLiteral("preview"), describe(resolved)},
            {QStringLiteral("properties"), Codec::styleJson(*named).toVariantMap()}});
    }
    return result;
}

QString RichTextDocument::createStyle(const QString &name, const QString &kind, const QString &parentId)
{
    const std::optional<StyleKind> styleKind = styleKindFromName(kind);
    if (!styleKind) {
        reject(ErrorCode::Unsupported, QStringLiteral("Unsupported style kind: %1").arg(kind));
        return {};
    }
    NamedStyle created;
    created.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    created.name = name.trimmed();
    created.kind = *styleKind;
    created.parentId = parentId;
    StyleSheet next = m_styles;
    next.insert(created.id, created);
    if (const QString problem = styleSheetProblem(next); !problem.isEmpty()) {
        reject(ErrorCode::Validation, problem);
        return {};
    }
    return commitStyle(tr("Create style"), created, created.id) ? created.id : QString();
}

bool RichTextDocument::updateStyle(const QString &styleId, const QVariantMap &patch)
{
    const NamedStyle *current = style(styleId);
    if (!current) return !reject(ErrorCode::InvalidArgument, QStringLiteral("Unknown style"));
    if (patch.contains(QStringLiteral("kind")) && patch.value(QStringLiteral("kind")).toString() != styleKindName(current->kind))
        return !reject(ErrorCode::Unsupported, QStringLiteral("A style's kind cannot change"));
    // Patch the persisted form, then read it back through the codec's checks.
    QJsonObject json = Codec::styleJson(*current);
    if (patch.contains(QStringLiteral("name"))) json.insert(QStringLiteral("name"), patch.value(QStringLiteral("name")).toString().trimmed());
    if (patch.contains(QStringLiteral("parentId"))) {
        const QString parent = patch.value(QStringLiteral("parentId")).toString();
        if (parent.isEmpty()) json.remove(QStringLiteral("parent"));
        else json.insert(QStringLiteral("parent"), parent);
    }
    for (const char *group : {"character", "paragraph", "table"}) {
        const QString key = QLatin1String(group);
        if (!patch.contains(key)) continue;
        QJsonObject members = json.value(key).toObject();
        const QVariantMap changes = patch.value(key).toMap();
        for (auto it = changes.cbegin(); it != changes.cend(); ++it) {
            if (!it.value().isValid() || it.value().isNull()) members.remove(it.key());
            else members.insert(it.key(), QJsonValue::fromVariant(it.value()));
        }
        if (members.isEmpty()) json.remove(key);
        else json.insert(key, members);
    }
    NamedStyle updated;
    Codec::PublishingError error;
    QStringList unknown;
    if (!Codec::styleFromJson(json, &updated, &error, &unknown))
        return !reject(ErrorCode::Validation, error.message);
    if (!unknown.isEmpty()) return !reject(ErrorCode::InvalidArgument, QStringLiteral("Unknown style property: %1").arg(unknown.first()));
    StyleSheet next = m_styles;
    next.insert(styleId, updated);
    if (const QString problem = styleSheetProblem(next); !problem.isEmpty())
        return !reject(ErrorCode::Validation, problem);
    return commitStyle(tr("Update style"), updated, styleId);
}

bool RichTextDocument::applyParagraphStyle(const Position &a, const Position &b, const QString &styleId)
{
    const NamedStyle *named = style(styleId);
    if (!isValidPosition(a) || !isValidPosition(b) || (!styleId.isEmpty() && (!named || named->kind != StyleKind::Paragraph)))
        return !reject(ErrorCode::InvalidArgument, QStringLiteral("Not a paragraph style"));
    const bool ordered = compare(a, b) <= 0;
    const Paragraph *first = paragraph((ordered ? a : b).paragraph);
    const Paragraph *last = paragraph((ordered ? b : a).paragraph);
    AutoTransaction transaction(this, tr("Apply style"));
    if (!transaction.valid) return false;
    const QVector<Paragraph *> flat = paragraphsInOrder();
    bool ok = true;
    // Applying a style clears the paragraph's direct paragraph formatting;
    // removing one keeps the values it gave as direct formatting.
    for (int i = first->flatIndex; ok && i <= last->flatIndex; ++i) {
        Paragraph *p = flat.at(i);
        ParagraphFormat next = p->format;
        next.styleId = styleId;
        next.overrides = 0;
        next = materializedFormat(m_styles, next);
        if (!(next == p->format)) ok = !apply(StepParagraphFormat{p->id, p->format, next});
    }
    return transaction.finish(ok, ordered ? b : a);
}

bool RichTextDocument::applyCharacterStyle(const Position &a, const Position &b, const QString &styleId)
{
    const NamedStyle *named = style(styleId);
    if (!styleId.isEmpty() && (!named || named->kind != StyleKind::Character))
        return !reject(ErrorCode::InvalidArgument, QStringLiteral("Not a character style"));
    AutoTransaction transaction(this, tr("Apply style"));
    if (!transaction.valid) return false;
    CharFormatPatch patch;
    patch.styleId = styleId;
    return transaction.finish(applyCharFormat(a, b, patch), compare(a, b) <= 0 ? b : a);
}

bool RichTextDocument::applyStyle(const QString &styleId, const QStringList &targetIds, const QString &kindName)
{
    const NamedStyle *named = style(styleId);
    const std::optional<StyleKind> kind = named ? std::optional<StyleKind>(named->kind) : styleKindFromName(kindName);
    if ((!styleId.isEmpty() && !named) || !kind || targetIds.isEmpty())
        return !reject(ErrorCode::InvalidArgument, QStringLiteral("Unknown style or no targets"));
    AutoTransaction transaction(this, tr("Apply style"));
    if (!transaction.valid) return false;
    bool ok = true;
    for (const QString &target : targetIds) {
        if (!ok) break;
        if (*kind == StyleKind::Table) {
            ok = setTableStyle(target, styleId);
            continue;
        }
        const Paragraph *p = paragraph(QUuid(target));
        if (!p) {
            ok = !reject(ErrorCode::InvalidArgument, QStringLiteral("Unknown paragraph %1").arg(target));
            continue;
        }
        const Position start{p->id, 0};
        const Position end{p->id, int(p->text.size())};
        ok = *kind == StyleKind::Paragraph ? applyParagraphStyle(start, end, styleId)
                                          : applyCharacterStyle(start, end, styleId);
    }
    return transaction.finish(ok, {});
}

QVariantList RichTextDocument::directOverrides(const QStringList &paragraphIds) const
{
    static const char *paragraphLabels[] = {
        QT_TR_NOOP("Alignment"), QT_TR_NOOP("Direction"), QT_TR_NOOP("Start indent"), QT_TR_NOOP("End indent"),
        QT_TR_NOOP("First-line indent"), QT_TR_NOOP("Space before"), QT_TR_NOOP("Space after"),
        QT_TR_NOOP("Line height"), QT_TR_NOOP("Exact line height"), QT_TR_NOOP("Heading level"),
        QT_TR_NOOP("Keep with next"), QT_TR_NOOP("Keep together")};
    static const char *characterLabels[] = {
        QT_TR_NOOP("Font"), QT_TR_NOOP("Font size"), QT_TR_NOOP("Weight"), QT_TR_NOOP("Italic"),
        QT_TR_NOOP("Underline"), QT_TR_NOOP("Strikethrough"), QT_TR_NOOP("Text colour"),
        QT_TR_NOOP("Highlight"), QT_TR_NOOP("Baseline")};
    QVector<QStringList> paragraphValues(12), characterValues(9);
    auto note = [](QStringList &values, const QString &value) { if (!value.isEmpty() && !values.contains(value)) values.append(value); };
    for (const QString &id : paragraphIds) {
        const Paragraph *p = paragraph(QUuid(id));
        if (!p) continue;
        if (!p->format.styleId.isEmpty())
            for (int bit = 0; bit < 12; ++bit)
                if (p->format.overrides & (1u << bit)) note(paragraphValues[bit], paragraphValue(p->format, bit));
        QVector<int> formats{p->markFormat};
        for (const Span &span : p->spans) formats.append(span.format);
        for (int index : std::as_const(formats)) {
            const CharFormat &f = charFormat(index);
            if (!f.objectId.isEmpty()) continue;
            for (int i = 0; i < 9; ++i) note(characterValues[i], characterValue(f, i));
        }
    }
    QVariantList rows;
    auto add = [&rows](const QString &key, const char *label, const QStringList &values) {
        if (values.isEmpty()) return;
        rows.append(QVariantMap{{QStringLiteral("key"), key}, {QStringLiteral("label"), tr(label)},
                                {QStringLiteral("value"), values.size() == 1 ? values.first() : tr("mixed")}});
    };
    for (int bit = 0; bit < 12; ++bit)
        add(QStringLiteral("paragraph.") + Codec::stylePropertyNames().at(bit), paragraphLabels[bit], paragraphValues.at(bit));
    for (int i = 0; i < 9; ++i)
        add(QStringLiteral("character.") + Codec::characterPropertyNames().at(i), characterLabels[i], characterValues.at(i));
    return rows;
}

bool RichTextDocument::resetDirectFormatting(const QStringList &paragraphIds, const QStringList &keys)
{
    quint32 paragraphBits = keys.isEmpty() ? AllStyleProperties : 0;
    QSet<QString> character;
    if (keys.isEmpty())
        for (const QString &name : Codec::characterPropertyNames()) character.insert(name);
    for (const QString &key : keys) {
        const qsizetype bit = key.startsWith(QStringLiteral("paragraph."))
            ? Codec::stylePropertyNames().indexOf(key.mid(10)) : -1;
        const bool characterKey = key.startsWith(QStringLiteral("character."))
            && Codec::characterPropertyNames().contains(key.mid(10));
        if (bit >= 0) paragraphBits |= 1u << bit;
        else if (characterKey) character.insert(key.mid(10));
        else return !reject(ErrorCode::InvalidArgument, QStringLiteral("Unknown formatting key: %1").arg(key));
    }
    AutoTransaction transaction(this, tr("Reset direct formatting"));
    if (!transaction.valid) return false;
    bool ok = true;
    for (const QString &id : paragraphIds) {
        Paragraph *p = paragraph(QUuid(id));
        if (!p) {
            ok = !reject(ErrorCode::InvalidArgument, QStringLiteral("Unknown paragraph %1").arg(id));
            break;
        }
        if (!p->format.styleId.isEmpty() && (p->format.overrides & paragraphBits)) {
            ParagraphFormat next = p->format;
            next.overrides &= ~paragraphBits;
            next = materializedFormat(m_styles, next);
            ok = !apply(StepParagraphFormat{p->id, p->format, next});
        }
        if (!ok || character.isEmpty()) continue;
        // Links, objects and character styles stay; the named members inherit again.
        auto cleared = [&](int index) {
            const CharFormat &f = charFormat(index);
            return f.objectId.isEmpty() ? internFormat(withoutCharacter(f, character)) : index;
        };
        StepSpans step;
        step.paragraph = p->id;
        step.before = p->spans;
        step.after = Spans::mapped(p->spans, cleared);
        const int mark = cleared(p->markFormat);
        if (mark != p->markFormat) {
            step.markBefore = p->markFormat;
            step.markAfter = mark;
        }
        if (!(step.before == step.after) || step.markBefore >= 0) ok = !apply(step);
    }
    return transaction.finish(ok, {});
}

QVariantMap RichTextDocument::stylePatchFromParagraph(const QString &paragraphId) const
{
    const Paragraph *p = paragraph(QUuid(paragraphId));
    if (!p) return {};
    ParagraphFormatPatch look;
    look.alignment = p->format.alignment;
    look.direction = p->format.direction;
    look.indentStartPt = p->format.indentStartPt;
    look.indentEndPt = p->format.indentEndPt;
    look.firstLineIndentPt = p->format.firstLineIndentPt;
    look.spaceBeforePt = p->format.spaceBeforePt;
    look.spaceAfterPt = p->format.spaceAfterPt;
    look.lineHeight = p->format.lineHeight;
    look.lineHeightPt = p->format.lineHeightPt;
    look.headingLevel = p->format.headingLevel;
    look.keepWithNext = p->format.keepWithNext;
    look.keepTogether = p->format.keepTogether;
    // The first text run's direct character members (an empty paragraph: its mark).
    int index = p->markFormat;
    for (int i = 0, offset = 0; i < p->spans.size(); offset += p->spans.at(i).length, ++i)
        if (p->text.at(offset) != QChar(ObjectChar)) {
            index = p->spans.at(i).format;
            break;
        }
    const CharFormat &f = charFormat(index);
    CharFormatPatch character;
    if (!f.family.isEmpty()) character.family = f.family;
    if (f.sizePt > 0) character.sizePt = f.sizePt;
    if (f.weight > 0) character.weight = f.weight;
    if (f.italic >= 0) character.italic = f.italic;
    if (f.underline >= 0) character.underline = f.underline;
    if (f.strike >= 0) character.strike = f.strike;
    if (f.color.isValid()) character.color = f.color;
    if (f.background.isValid()) character.background = f.background;
    if (f.verticalAlign != VerticalAlign::Normal) character.verticalAlign = f.verticalAlign;
    return {{QStringLiteral("paragraph"), Codec::paragraphPatchJson(look).toVariantMap()},
            {QStringLiteral("character"), Codec::characterPatchJson(character).toVariantMap()}};
}

} // namespace QindaTK
