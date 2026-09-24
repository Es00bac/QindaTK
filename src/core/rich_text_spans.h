// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "rich_text_types.h"

// Pure helpers over run-length character formats. Every result is
// normalized: no zero-length spans and no two adjacent spans with one format.
namespace QindaTK::RichText::Spans {

inline int totalLength(const QVector<Span> &spans)
{
    int total = 0;
    for (const Span &span : spans) total += span.length;
    return total;
}

inline QVector<Span> normalized(const QVector<Span> &spans)
{
    QVector<Span> result;
    result.reserve(spans.size());
    for (const Span &span : spans) {
        if (span.length <= 0) continue;
        if (!result.isEmpty() && result.last().format == span.format) result.last().length += span.length;
        else result.append(span);
    }
    return result;
}

inline QVector<Span> slice(const QVector<Span> &spans, int offset, int length)
{
    QVector<Span> result;
    if (length <= 0) return result;
    const int end = offset + length;
    int start = 0;
    for (const Span &span : spans) {
        const int spanEnd = start + span.length;
        const int from = qMax(start, offset);
        const int to = qMin(spanEnd, end);
        if (to > from) result.append({to - from, span.format});
        start = spanEnd;
        if (start >= end) break;
    }
    return normalized(result);
}

inline QVector<Span> replaced(const QVector<Span> &spans, int offset, int removed,
                              const QVector<Span> &inserted)
{
    const int total = totalLength(spans);
    QVector<Span> result = slice(spans, 0, offset);
    result += inserted;
    result += slice(spans, offset + removed, total - offset - removed);
    return normalized(result);
}

inline int formatAt(const QVector<Span> &spans, int offset)
{
    int start = 0;
    for (const Span &span : spans) {
        if (offset < start + span.length) return span.format;
        start += span.length;
    }
    return spans.isEmpty() ? 0 : spans.last().format;
}

// Replace each covered span's format through `map`, keeping lengths.
template <typename Map>
QVector<Span> mapped(const QVector<Span> &spans, Map map)
{
    QVector<Span> result;
    result.reserve(spans.size());
    for (const Span &span : spans) result.append({span.length, map(span.format)});
    return normalized(result);
}

} // namespace QindaTK::RichText::Spans
