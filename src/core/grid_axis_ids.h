// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QHash>
#include <QString>
#include <QStringList>
#include <map>
#include <vector>

namespace QindaTK {

// Stable section identity for one EditableGridModel axis (docs/editable-grid.md).
// A run of generated IDs — a prefix ending in ':' followed by consecutive
// canonical decimals ("r:1", "r:2", ...) — is kept as one segment, so a
// million generated rows cost a few bytes instead of a string and a hash node
// each. Any other provider ID is stored explicitly. Lookups by index and by ID
// are O(log segments). The caller guarantees uniqueness before mutating.
class GridAxisIds {
public:
    int size() const { return m_size; }
    bool isEmpty() const { return m_size == 0; }
    QString at(int index) const;
    int indexOf(const QString &id) const;
    bool contains(const QString &id) const { return indexOf(id) >= 0; }
    QStringList toList() const;
    int segmentCount() const { return int(m_segments.size()); }

    void clear();
    void assign(const QStringList &ids);
    void insert(int index, const QStringList &ids);
    void insertGenerated(int index, const QString &prefix, quint64 first, int count);
    void remove(int index, int count);
    void move(int from, int to);
    // True when any generated ID prefix+[first, first+count) is already used.
    bool overlapsGenerated(const QString &prefix, quint64 first, int count) const;
    // Smallest number >= from starting `count` consecutive unused generated IDs.
    quint64 nextFreeGenerated(const QString &prefix, quint64 from, int count) const;

    // Parses "<prefix:><canonical decimal>"; false for any other form.
    static bool parseGenerated(const QString &id, QString *prefix, quint64 *number);
    // Allocation-free form: `prefix` views into `id`.
    static bool splitGenerated(QStringView id, QStringView *prefix, quint64 *number);

private:
    struct Segment {
        QString prefix;          // generated run when explicit is empty
        quint64 first = 0;
        int count = 0;
        QStringList explicitIds;
        bool generated() const { return explicitIds.isEmpty(); }
    };
    int segmentAt(int index) const;
    // Splits so that a segment boundary falls at `index`; returns the index
    // of the first segment starting at `index` (segments.size() at the end).
    int splitAt(int index);
    void appendRuns(std::vector<Segment> *out, const QStringList &ids) const;
    void rebuild();

    // Generated runs of one prefix by first number; axes use one or two prefixes.
    struct PrefixRuns {
        QString prefix;
        std::map<quint64, int> firsts; // first -> segment
    };
    const PrefixRuns *runsFor(QStringView prefix) const;

    std::vector<Segment> m_segments;
    std::vector<int> m_starts;
    QHash<QString, std::pair<int, int>> m_explicit;            // id -> (segment, offset)
    std::vector<PrefixRuns> m_runs;
    int m_size = 0;
};

} // namespace QindaTK
