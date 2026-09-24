// SPDX-License-Identifier: LGPL-3.0-or-later
#include "grid_axis_ids.h"

#include <algorithm>

namespace QindaTK {

bool GridAxisIds::splitGenerated(QStringView id, QStringView *prefix, quint64 *number)
{
    const qsizetype colon = id.lastIndexOf(u':');
    if (colon < 0 || colon + 1 >= id.size()) return false;
    const QStringView digits = id.mid(colon + 1);
    if (digits.size() > 19 || (digits.size() > 1 && digits.front() == u'0')) return false;
    quint64 value = 0;
    for (QChar c : digits) {
        if (c < u'0' || c > u'9') return false;
        value = value * 10 + quint64(c.unicode() - u'0');
    }
    if (prefix) *prefix = id.left(colon + 1);
    if (number) *number = value;
    return true;
}

bool GridAxisIds::parseGenerated(const QString &id, QString *prefix, quint64 *number)
{
    QStringView view;
    if (!splitGenerated(id, &view, number)) return false;
    if (prefix) *prefix = view.toString();
    return true;
}

const GridAxisIds::PrefixRuns *GridAxisIds::runsFor(QStringView prefix) const
{
    for (const PrefixRuns &runs : m_runs)
        if (runs.prefix == prefix) return &runs;
    return nullptr;
}

int GridAxisIds::segmentAt(int index) const
{
    const auto it = std::upper_bound(m_starts.begin(), m_starts.end(), index);
    return int(it - m_starts.begin()) - 1;
}

QString GridAxisIds::at(int index) const
{
    if (index < 0 || index >= m_size) return {};
    const int seg = segmentAt(index);
    const Segment &s = m_segments[size_t(seg)];
    const int offset = index - m_starts[size_t(seg)];
    return s.generated() ? s.prefix + QString::number(s.first + quint64(offset)) : s.explicitIds.at(offset);
}

int GridAxisIds::indexOf(const QString &id) const
{
    if (const auto it = m_explicit.constFind(id); it != m_explicit.cend())
        return m_starts[size_t(it->first)] + it->second;
    QStringView prefix;
    quint64 number = 0;
    if (!splitGenerated(id, &prefix, &number)) return -1;
    const PrefixRuns *runs = runsFor(prefix);
    if (!runs) return -1;
    auto it = runs->firsts.upper_bound(number);
    if (it == runs->firsts.begin()) return -1;
    --it;
    const Segment &s = m_segments[size_t(it->second)];
    if (number >= s.first + quint64(s.count)) return -1;
    return m_starts[size_t(it->second)] + int(number - s.first);
}

QStringList GridAxisIds::toList() const
{
    QStringList result;
    result.reserve(m_size);
    for (const Segment &s : m_segments) {
        if (!s.generated()) { result.append(s.explicitIds); continue; }
        for (int i = 0; i < s.count; ++i) result.append(s.prefix + QString::number(s.first + quint64(i)));
    }
    return result;
}

bool GridAxisIds::overlapsGenerated(const QString &prefix, quint64 first, int count) const
{
    if (count <= 0) return false;
    const PrefixRuns *runs = runsFor(prefix);
    if (!runs) return false;
    const quint64 last = first + quint64(count) - 1;
    auto it = runs->firsts.upper_bound(last);
    if (it == runs->firsts.begin()) return false;
    --it;
    const Segment &s = m_segments[size_t(it->second)];
    return s.first + quint64(s.count) > first;
}

quint64 GridAxisIds::nextFreeGenerated(const QString &prefix, quint64 from, int count) const
{
    if (count <= 0) return from;
    const PrefixRuns *runs = runsFor(prefix);
    if (!runs) return from;
    for (;;) {
        auto it = runs->firsts.upper_bound(from + quint64(count) - 1);
        if (it == runs->firsts.begin()) return from;
        --it;
        const Segment &s = m_segments[size_t(it->second)];
        if (s.first + quint64(s.count) <= from) return from;
        from = s.first + quint64(s.count); // jump past the overlapping run
    }
}

void GridAxisIds::clear()
{
    m_segments.clear();
    rebuild();
}

void GridAxisIds::appendRuns(std::vector<Segment> *out, const QStringList &ids) const
{
    for (const QString &id : ids) {
        QString prefix;
        quint64 number = 0;
        if (parseGenerated(id, &prefix, &number)) {
            if (!out->empty() && out->back().generated() && out->back().prefix == prefix
                && out->back().first + quint64(out->back().count) == number) {
                ++out->back().count;
                continue;
            }
            out->push_back({prefix, number, 1, {}});
            continue;
        }
        if (!out->empty() && !out->back().generated()) {
            out->back().explicitIds.append(id);
            ++out->back().count;
            continue;
        }
        out->push_back({{}, 0, 1, {id}});
    }
}

void GridAxisIds::assign(const QStringList &ids)
{
    m_segments.clear();
    appendRuns(&m_segments, ids);
    rebuild();
}

int GridAxisIds::splitAt(int index)
{
    if (index >= m_size) return int(m_segments.size());
    const int seg = segmentAt(index);
    const int offset = index - m_starts[size_t(seg)];
    if (offset == 0) return seg;
    Segment &s = m_segments[size_t(seg)];
    Segment tail;
    tail.prefix = s.prefix;
    tail.count = s.count - offset;
    if (s.generated()) {
        tail.first = s.first + quint64(offset);
    } else {
        tail.explicitIds = s.explicitIds.mid(offset);
        s.explicitIds = s.explicitIds.mid(0, offset);
    }
    s.count = offset;
    m_segments.insert(m_segments.begin() + seg + 1, std::move(tail));
    m_starts.insert(m_starts.begin() + seg + 1, index);
    return seg + 1;
}

void GridAxisIds::insert(int index, const QStringList &ids)
{
    if (ids.isEmpty()) return;
    const int at = splitAt(std::clamp(index, 0, m_size));
    std::vector<Segment> added;
    appendRuns(&added, ids);
    m_segments.insert(m_segments.begin() + at, added.begin(), added.end());
    rebuild();
}

void GridAxisIds::insertGenerated(int index, const QString &prefix, quint64 first, int count)
{
    if (count <= 0) return;
    const int at = splitAt(std::clamp(index, 0, m_size));
    m_segments.insert(m_segments.begin() + at, Segment{prefix, first, count, {}});
    rebuild();
}

void GridAxisIds::remove(int index, int count)
{
    if (count <= 0 || index < 0 || index >= m_size) return;
    const int end = std::min(m_size, index + count);
    const int a = splitAt(index);
    const int b = splitAt(end);
    m_segments.erase(m_segments.begin() + a, m_segments.begin() + b);
    rebuild();
}

void GridAxisIds::move(int from, int to)
{
    if (from == to || from < 0 || from >= m_size || to < 0 || to >= m_size) return;
    const QString id = at(from);
    remove(from, 1);
    insert(to, {id});
}

// Merges adjacent compatible segments and recomputes prefix sums and lookups.
void GridAxisIds::rebuild()
{
    std::vector<Segment> merged;
    merged.reserve(m_segments.size());
    for (Segment &s : m_segments) {
        if (s.count <= 0) continue;
        if (!merged.empty()) {
            Segment &last = merged.back();
            if (s.generated() && last.generated() && last.prefix == s.prefix
                && last.first + quint64(last.count) == s.first) {
                last.count += s.count;
                continue;
            }
            if (!s.generated() && !last.generated()) {
                last.explicitIds.append(s.explicitIds);
                last.count += s.count;
                continue;
            }
        }
        merged.push_back(std::move(s));
    }
    m_segments = std::move(merged);
    m_starts.assign(m_segments.size(), 0);
    m_explicit.clear();
    m_runs.clear();
    int start = 0;
    for (size_t i = 0; i < m_segments.size(); ++i) {
        m_starts[i] = start;
        const Segment &s = m_segments[i];
        if (s.generated()) {
            auto runs = std::find_if(m_runs.begin(), m_runs.end(), [&](const PrefixRuns &r) { return r.prefix == s.prefix; });
            if (runs == m_runs.end()) runs = m_runs.insert(m_runs.end(), PrefixRuns{s.prefix, {}});
            runs->firsts.emplace(s.first, int(i));
        }
        else for (int k = 0; k < s.count; ++k) m_explicit.insert(s.explicitIds.at(k), {int(i), k});
        start += s.count;
    }
    m_size = start;
}

} // namespace QindaTK
