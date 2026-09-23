// SPDX-License-Identifier: LGPL-3.0-or-later
#include "query_core.h"
#include <cmath>

namespace QindaTK {
namespace {
int compareValues(const QueryValue &left, const QueryValue &right, bool *ok)
{
    *ok = true;
    const bool leftNumber = left.type == QueryType::Integer || left.type == QueryType::Number;
    const bool rightNumber = right.type == QueryType::Integer || right.type == QueryType::Number;
    if (leftNumber && rightNumber) {
        const long double a = left.type == QueryType::Integer
                                  ? static_cast<long double>(left.data.toLongLong())
                                  : left.data.toDouble();
        const long double b = right.type == QueryType::Integer
                                  ? static_cast<long double>(right.data.toLongLong())
                                  : right.data.toDouble();
        if (!std::isfinite(a) || !std::isfinite(b)) { *ok = false; return 0; }
        return a < b ? -1 : a > b ? 1 : 0;
    }
    if (left.type == QueryType::Text && right.type == QueryType::Text)
        return QString::compare(left.data.toString(), right.data.toString(), Qt::CaseSensitive);
    if (left.type == QueryType::Boolean && right.type == QueryType::Boolean) {
        const int a = left.data.toBool(), b = right.data.toBool();
        return a < b ? -1 : a > b ? 1 : 0;
    }
    *ok = false;
    return 0;
}
} // namespace

QueryTruth QueryEngine::evaluate(const QueryPredicate &predicate, const QueryRow &row,
                                 const QuerySpec &spec)
{
    using Kind = QueryPredicate::Kind;
    if (predicate.kind == Kind::Constant)
        return predicate.constant ? QueryTruth::True : QueryTruth::False;
    if (predicate.kind == Kind::And) {
        bool unknown = false;
        for (const auto &child : predicate.children) {
            const auto truth = evaluate(child, row, spec);
            if (truth == QueryTruth::Error) return truth;
            if (truth == QueryTruth::False) return truth;
            unknown |= truth == QueryTruth::Unknown;
        }
        return unknown ? QueryTruth::Unknown : QueryTruth::True;
    }
    if (predicate.kind == Kind::Or) {
        bool unknown = false;
        for (const auto &child : predicate.children) {
            const auto truth = evaluate(child, row, spec);
            if (truth == QueryTruth::Error) return truth;
            if (truth == QueryTruth::True) return truth;
            unknown |= truth == QueryTruth::Unknown;
        }
        return unknown ? QueryTruth::Unknown : QueryTruth::False;
    }
    if (predicate.kind == Kind::Not) {
        const auto truth = evaluate(predicate.children.first(), row, spec);
        if (truth == QueryTruth::True) return QueryTruth::False;
        if (truth == QueryTruth::False) return QueryTruth::True;
        return truth;
    }
    if (!row.values.contains(predicate.fieldId)) return QueryTruth::Error;
    const QueryValue left = row.values.value(predicate.fieldId);
    if (left.type == QueryType::Error) return QueryTruth::Error;
    if (predicate.kind == Kind::IsNull)
        return left.type == QueryType::Null ? QueryTruth::True : QueryTruth::False;
    if (predicate.kind == Kind::IsNotNull)
        return left.type == QueryType::Null ? QueryTruth::False : QueryTruth::True;
    const QueryValue right = predicate.parameterId.isEmpty()
                                 ? predicate.literal : spec.parameters.value(predicate.parameterId);
    if (right.type == QueryType::Error) return QueryTruth::Error;
    if (left.type == QueryType::Null || right.type == QueryType::Null)
        return QueryTruth::Unknown;
    if (predicate.comparison == QueryPredicate::Compare::Contains) {
        if (left.type != QueryType::Text || right.type != QueryType::Text)
            return QueryTruth::Error;
        return left.data.toString().contains(right.data.toString(), Qt::CaseSensitive)
                   ? QueryTruth::True : QueryTruth::False;
    }
    bool comparable = false;
    const int order = compareValues(left, right, &comparable);
    if (!comparable) return QueryTruth::Error;
    bool yes = false;
    switch (predicate.comparison) {
    case QueryPredicate::Compare::Equal: yes = order == 0; break;
    case QueryPredicate::Compare::NotEqual: yes = order != 0; break;
    case QueryPredicate::Compare::Less: yes = order < 0; break;
    case QueryPredicate::Compare::LessEqual: yes = order <= 0; break;
    case QueryPredicate::Compare::Greater: yes = order > 0; break;
    case QueryPredicate::Compare::GreaterEqual: yes = order >= 0; break;
    case QueryPredicate::Compare::Contains: break;
    }
    return yes ? QueryTruth::True : QueryTruth::False;
}

// Shared with the pivot accumulator so minimum/maximum do not compare display
// strings or silently coerce a failed numeric conversion.
int queryCompareValues(const QueryValue &a, const QueryValue &b, bool *ok)
{ return compareValues(a, b, ok); }
} // namespace QindaTK
