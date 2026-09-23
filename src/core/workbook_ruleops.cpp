// SPDX-License-Identifier: LGPL-3.0-or-later
#include "workbook_p.h"

#include <algorithm>

namespace QindaTK {
using namespace Calc;
namespace {
// Rule ids are "<prefix><n>", unique within their sheet and never reused
// while the rule list holds a higher number.
template <typename Rule>
QString nextRuleId(const std::vector<Rule> &rules, const QString &prefix)
{
    qint64 highest = 0;
    for (const Rule &rule : rules)
        if (rule.id.startsWith(prefix)) highest = std::max(highest, rule.id.mid(prefix.size()).toLongLong());
    return prefix + QString::number(highest + 1);
}
template <typename Rule>
qsizetype indexOfRule(const std::vector<Rule> &rules, const QString &id)
{
    for (size_t i = 0; i < rules.size(); ++i)
        if (rules[i].id == id) return qsizetype(i);
    return -1;
}
QVariantMap checkResult(bool ok, const QString &message)
{
    return {{"ok", ok}, {"message", message}};
}
} // namespace

QVariantList Workbook::conditionalFormats(const QString &sheetId) const
{
    const Sheet *s = d->sheet(sheetId);
    QVariantList result;
    if (!s) return result;
    const SheetNameLookup names = [this](const QString &id) { return d->nameForId(id); };
    for (const ConditionalRule &rule : s->features->conditional) result.append(conditionalToMap(rule, names, false));
    return result;
}

QVariantMap Workbook::checkConditionalFormat(const QString &sheetId, const QVariantMap &rule) const
{
    const Sheet *s = d->sheet(sheetId);
    if (!s) return checkResult(false, QStringLiteral("Unknown sheet"));
    ConditionalRule parsed;
    QString error;
    return checkResult(conditionalFromMap(rule, d->lookups(*s), &parsed, &error), error);
}

QString Workbook::setConditionalFormat(const QString &sheetId, const QVariantMap &rule, int priority)
{
    QString id;
    const bool ok = d->mutate(QStringLiteral("Conditional format"), [&](QString *error, QVariantMap *detail) {
        Sheet *s = d->sheet(sheetId);
        if (!s) { *error = QStringLiteral("Unknown sheet"); return false; }
        if (!d->sheetEditable(*s, error)) return false;
        ConditionalRule parsed;
        if (!conditionalFromMap(rule, d->lookups(*s), &parsed, error)) {
            *detail = {{"sheetId", sheetId}, {"rule", true}};
            return false;
        }
        auto next = std::make_shared<SheetFeatures>(*s->features);
        auto &rules = next->conditional;
        const qsizetype existing = parsed.id.isEmpty() ? -1 : indexOfRule(rules, parsed.id);
        if (parsed.id.isEmpty()) parsed.id = nextRuleId(rules, QStringLiteral("cf"));
        qsizetype at = priority < 0 ? (existing >= 0 ? existing : 0) : priority;
        if (existing >= 0) rules.erase(rules.begin() + existing);
        if (rules.size() >= size_t(MaxSheetRules)) {
            *error = QStringLiteral("A sheet holds at most %1 conditional formats").arg(MaxSheetRules);
            return false;
        }
        at = std::clamp<qsizetype>(at, 0, qsizetype(rules.size()));
        id = parsed.id;
        rules.insert(rules.begin() + at, std::move(parsed));
        return d->recordFeatures(*s, std::move(next), error);
    });
    return ok ? id : QString();
}

bool Workbook::removeConditionalFormat(const QString &sheetId, const QString &ruleId)
{
    return d->mutate(QStringLiteral("Remove conditional format"), [&](QString *error, QVariantMap *) {
        Sheet *s = d->sheet(sheetId);
        if (!s) { *error = QStringLiteral("Unknown sheet"); return false; }
        if (!d->sheetEditable(*s, error)) return false;
        auto next = std::make_shared<SheetFeatures>(*s->features);
        const qsizetype index = indexOfRule(next->conditional, ruleId);
        if (index < 0) { *error = QStringLiteral("No conditional format '%1'").arg(ruleId); return false; }
        next->conditional.erase(next->conditional.begin() + index);
        return d->recordFeatures(*s, std::move(next), error);
    });
}

bool Workbook::moveConditionalFormat(const QString &sheetId, const QString &ruleId, int priority)
{
    return d->mutate(QStringLiteral("Reorder conditional formats"), [&](QString *error, QVariantMap *) {
        Sheet *s = d->sheet(sheetId);
        if (!s) { *error = QStringLiteral("Unknown sheet"); return false; }
        if (!d->sheetEditable(*s, error)) return false;
        auto next = std::make_shared<SheetFeatures>(*s->features);
        auto &rules = next->conditional;
        const qsizetype index = indexOfRule(rules, ruleId);
        if (index < 0 || priority < 0 || priority >= qsizetype(rules.size())) {
            *error = QStringLiteral("Invalid conditional format or priority");
            return false;
        }
        if (index == priority) return true;
        ConditionalRule moved = std::move(rules[size_t(index)]);
        rules.erase(rules.begin() + index);
        rules.insert(rules.begin() + priority, std::move(moved));
        return d->recordFeatures(*s, std::move(next), error);
    });
}

// AGENT-CONTRACT (OFF-17): the preview runs the same presentation code on a
// candidate copy of the sheet's features, so what the editor shows is what
// setConditionalFormat commits and what a reopened file draws.
QVariantList Workbook::previewConditionalFormat(const QString &sheetId, const QVariantMap &rule, int firstRow,
                                                int firstColumn, int rowCount, int columnCount) const
{
    const Sheet *s = d->sheet(sheetId);
    if (!s || firstRow < 0 || firstColumn < 0 || rowCount < 1 || columnCount < 1
        || qint64(firstRow) + rowCount > s->rows || qint64(firstColumn) + columnCount > s->columns
        || qint64(rowCount) * columnCount > MaxVisibleCells) return {};
    ConditionalRule parsed;
    QString error;
    if (!conditionalFromMap(rule, d->lookups(*s), &parsed, &error)) return {};
    SheetFeatures candidate = *s->features;
    const qsizetype existing = parsed.id.isEmpty() ? -1 : indexOfRule(candidate.conditional, parsed.id);
    if (existing >= 0) candidate.conditional[size_t(existing)] = parsed;
    else candidate.conditional.insert(candidate.conditional.begin(), parsed);
    QVariantList result;
    for (int r = firstRow; r < firstRow + rowCount; ++r)
        for (int c = firstColumn; c < firstColumn + columnCount; ++c) {
            QVariantMap cell = d->presentation(*s, r, c, &candidate);
            cell.insert({{"row", r}, {"column", c}, {"display", cellDisplay(sheetId, r, c)}});
            result.append(cell);
        }
    return result;
}

QVariantList Workbook::validations(const QString &sheetId) const
{
    const Sheet *s = d->sheet(sheetId);
    QVariantList result;
    if (!s) return result;
    const SheetNameLookup names = [this](const QString &id) { return d->nameForId(id); };
    for (const ValidationRule &rule : s->features->validations) result.append(validationToMap(rule, names, false));
    return result;
}

QVariantMap Workbook::checkValidation(const QString &sheetId, const QVariantMap &rule) const
{
    const Sheet *s = d->sheet(sheetId);
    if (!s) return checkResult(false, QStringLiteral("Unknown sheet"));
    ValidationRule parsed;
    QString error;
    return checkResult(validationFromMap(rule, d->lookups(*s), &parsed, &error), error);
}

QString Workbook::setValidation(const QString &sheetId, const QVariantMap &rule)
{
    QString id;
    const bool ok = d->mutate(QStringLiteral("Data validation"), [&](QString *error, QVariantMap *detail) {
        Sheet *s = d->sheet(sheetId);
        if (!s) { *error = QStringLiteral("Unknown sheet"); return false; }
        if (!d->sheetEditable(*s, error)) return false;
        ValidationRule parsed;
        if (!validationFromMap(rule, d->lookups(*s), &parsed, error)) {
            *detail = {{"sheetId", sheetId}, {"rule", true}};
            return false;
        }
        auto next = std::make_shared<SheetFeatures>(*s->features);
        auto &rules = next->validations;
        const qsizetype existing = parsed.id.isEmpty() ? -1 : indexOfRule(rules, parsed.id);
        if (parsed.id.isEmpty()) parsed.id = nextRuleId(rules, QStringLiteral("dv"));
        if (existing >= 0) rules.erase(rules.begin() + existing);
        // A cell has one rule: the new rule takes its cells from the others.
        for (ValidationRule &other : rules) {
            std::vector<Area> kept;
            for (const Area &area : other.areas) {
                std::vector<Area> pieces{area};
                for (const Area &cut : parsed.areas) {
                    std::vector<Area> remaining;
                    for (const Area &piece : pieces)
                        for (const Area &rest : subtractArea(piece, cut)) remaining.push_back(rest);
                    pieces = std::move(remaining);
                }
                kept.insert(kept.end(), pieces.begin(), pieces.end());
            }
            if (kept.size() > size_t(MaxRuleAreas)) {
                *error = QStringLiteral("This would split rule %1 into more than %2 ranges").arg(other.id).arg(MaxRuleAreas);
                return false;
            }
            other.areas = std::move(kept);
        }
        std::erase_if(rules, [](const ValidationRule &other) { return other.areas.empty(); });
        if (rules.size() >= size_t(MaxSheetRules)) {
            *error = QStringLiteral("A sheet holds at most %1 validation rules").arg(MaxSheetRules);
            return false;
        }
        id = parsed.id;
        const qsizetype at = existing >= 0 ? std::min(existing, qsizetype(rules.size())) : qsizetype(rules.size());
        rules.insert(rules.begin() + at, std::move(parsed));
        return d->recordFeatures(*s, std::move(next), error);
    });
    return ok ? id : QString();
}

bool Workbook::removeValidation(const QString &sheetId, const QString &ruleId)
{
    return d->mutate(QStringLiteral("Remove data validation"), [&](QString *error, QVariantMap *) {
        Sheet *s = d->sheet(sheetId);
        if (!s) { *error = QStringLiteral("Unknown sheet"); return false; }
        if (!d->sheetEditable(*s, error)) return false;
        auto next = std::make_shared<SheetFeatures>(*s->features);
        const qsizetype index = indexOfRule(next->validations, ruleId);
        if (index < 0) { *error = QStringLiteral("No validation rule '%1'").arg(ruleId); return false; }
        next->validations.erase(next->validations.begin() + index);
        return d->recordFeatures(*s, std::move(next), error);
    });
}

} // namespace QindaTK
