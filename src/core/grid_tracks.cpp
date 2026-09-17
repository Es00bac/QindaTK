// SPDX-License-Identifier: LGPL-3.0-or-later
#include "grid_tracks.h"

#include <QStringView>

#include <algorithm>
#include <limits>

namespace QindaTK {

namespace {

constexpr qreal kUnbounded = std::numeric_limits<qreal>::infinity();

bool parseLength(QStringView token, GridLength *out)
{
    const QStringView t = token.trimmed();
    if (t.isEmpty()) {
        return false;
    }
    if (t == u"auto") {
        *out = {GridLength::Auto, 0};
        return true;
    }
    if (t == u"min-content" || t == u"max-content") {
        *out = {GridLength::Content, 0};
        return true;
    }
    bool ok = false;
    if (t.endsWith(u"fr")) {
        const qreal v = t.chopped(2).toDouble(&ok);
        if (ok && v >= 0) {
            *out = {GridLength::Fr, v};
        }
        return ok;
    }
    if (t.endsWith(u'%')) {
        const qreal v = t.chopped(1).toDouble(&ok);
        if (ok) {
            *out = {GridLength::Percent, v};
        }
        return ok;
    }
    QStringView number = t;
    if (number.endsWith(u"px")) {
        number = number.chopped(2);
    }
    const qreal v = number.toDouble(&ok);
    if (ok && v >= 0) {
        *out = {GridLength::Fixed, v};
    }
    return ok;
}

// Index of the ')' matching the '(' at `open`, or -1.
qsizetype matchingParen(QStringView s, qsizetype open)
{
    int depth = 0;
    for (qsizetype i = open; i < s.size(); ++i) {
        if (s[i] == u'(') {
            ++depth;
        } else if (s[i] == u')') {
            if (--depth == 0) {
                return i;
            }
        }
    }
    return -1;
}

// Splits at the first top-level comma.
bool splitTopLevel(QStringView s, QStringView *head, QStringView *tail)
{
    int depth = 0;
    for (qsizetype i = 0; i < s.size(); ++i) {
        if (s[i] == u'(') {
            ++depth;
        } else if (s[i] == u')') {
            --depth;
        } else if (s[i] == u',' && depth == 0) {
            *head = s.left(i);
            *tail = s.mid(i + 1);
            return true;
        }
    }
    return false;
}

bool parseList(QStringView spec, QVector<GridTrack> *out, QString *error)
{
    qsizetype i = 0;
    while (i < spec.size()) {
        while (i < spec.size() && spec[i].isSpace()) {
            ++i;
        }
        if (i >= spec.size()) {
            break;
        }
        const QStringView rest = spec.mid(i);
        if (rest.startsWith(u"repeat(") || rest.startsWith(u"minmax(")) {
            const qsizetype open = rest.indexOf(u'(');
            const qsizetype close = matchingParen(rest, open);
            if (close < 0) {
                if (error) *error = QStringLiteral("unbalanced parentheses in track list");
                return false;
            }
            const QStringView inner = rest.mid(open + 1, close - open - 1);
            QStringView head;
            QStringView tail;
            if (!splitTopLevel(inner, &head, &tail)) {
                if (error) *error = QStringLiteral("expected two arguments in %1").arg(rest.left(close + 1));
                return false;
            }
            if (rest.startsWith(u"repeat(")) {
                bool ok = false;
                const int count = head.trimmed().toInt(&ok);
                if (!ok || count < 0) {
                    if (error) *error = QStringLiteral("repeat() count must be an integer");
                    return false;
                }
                QVector<GridTrack> inner_tracks;
                if (!parseList(tail, &inner_tracks, error)) {
                    return false;
                }
                for (int n = 0; n < count; ++n) {
                    out->append(inner_tracks);
                }
            } else {
                GridTrack track;
                if (!parseLength(head, &track.min) || !parseLength(tail, &track.max)) {
                    if (error) *error = QStringLiteral("bad minmax() arguments");
                    return false;
                }
                if (track.min.kind == GridLength::Fr) {
                    if (error) *error = QStringLiteral("minmax() minimum cannot be fr");
                    return false;
                }
                out->append(track);
            }
            i += close + 1;
            continue;
        }
        qsizetype end = i;
        while (end < spec.size() && !spec[end].isSpace()) {
            ++end;
        }
        GridLength length;
        if (!parseLength(spec.mid(i, end - i), &length)) {
            if (error) *error = QStringLiteral("bad track \"%1\"").arg(spec.mid(i, end - i));
            return false;
        }
        GridTrack track;
        if (length.kind == GridLength::Fr) {
            track.min = {GridLength::Fixed, 0};
            track.max = length;
        } else {
            track.min = length;
            track.max = length;
        }
        out->append(track);
        i = end;
    }
    return true;
}

} // namespace

QVector<GridTrack> parseGridTracks(const QString &spec, QString *error)
{
    QVector<GridTrack> tracks;
    if (error) {
        error->clear();
    }
    parseList(spec, &tracks, error);
    return tracks;
}

GridTrackSizes sizeGridTracks(const QVector<GridTrack> &tracks,
                              const QVector<GridContribution> &items, qreal available, qreal gap)
{
    const int n = tracks.size();
    GridTrackSizes result;
    result.sizes = QVector<qreal>(n, 0);
    result.natural = QVector<qreal>(n, 0);
    if (n == 0) {
        return result;
    }
    const bool definite = available > 0;
    QVector<qreal> base(n, 0);
    QVector<qreal> limit(n, kUnbounded);
    QVector<qreal> content(n, 0);
    for (int t = 0; t < n; ++t) {
        const GridTrack &track = tracks[t];
        switch (track.min.kind) {
        case GridLength::Fixed:
            base[t] = track.min.value;
            break;
        case GridLength::Percent:
            base[t] = definite ? track.min.value / 100.0 * available : 0;
            break;
        default:
            break;
        }
        switch (track.max.kind) {
        case GridLength::Fixed:
            limit[t] = track.max.value;
            break;
        case GridLength::Percent:
            limit[t] = definite ? track.max.value / 100.0 * available : kUnbounded;
            break;
        default:
            break;
        }
        limit[t] = std::max(limit[t], base[t]);
    }

    // Intrinsic contributions, single-span first.
    QVector<GridContribution> sorted = items;
    std::stable_sort(sorted.begin(), sorted.end(), [](const GridContribution &a, const GridContribution &b) {
        return a.span < b.span;
    });
    for (const GridContribution &item : sorted) {
        if (item.start < 0 || item.start >= n || item.size <= 0) {
            continue;
        }
        const int span = std::max(1, std::min(item.span, n - item.start));
        if (span == 1) {
            const int t = item.start;
            const GridTrack &track = tracks[t];
            content[t] = std::max(content[t], item.size);
            const bool contentSized = track.min.contentSized() || track.max.contentSized()
                                      || (!definite && track.max.kind == GridLength::Percent);
            if (contentSized) {
                base[t] = std::max(base[t], std::min(item.size, limit[t] == kUnbounded ? item.size : limit[t]));
                if (track.max.contentSized()) {
                    base[t] = std::max(base[t], item.size);
                }
            }
            continue;
        }
        bool crossesFlexible = false;
        qreal spanned = gap * (span - 1);
        QVector<int> growable;
        QVector<int> fallback;
        for (int t = item.start; t < item.start + span; ++t) {
            if (tracks[t].flexible()) {
                crossesFlexible = true;
            }
            spanned += base[t];
            if (tracks[t].max.contentSized() || tracks[t].min.contentSized()) {
                growable.append(t);
            } else if (tracks[t].max.kind != GridLength::Fixed) {
                fallback.append(t);
            }
        }
        if (crossesFlexible) {
            continue;
        }
        const qreal extra = item.size - spanned;
        if (extra <= 0) {
            continue;
        }
        const QVector<int> &targets = growable.isEmpty() ? fallback : growable;
        if (targets.isEmpty()) {
            continue;
        }
        for (int t : targets) {
            base[t] += extra / targets.size();
        }
    }

    // Free space and flexible tracks.
    const qreal gaps = gap * (n - 1);
    QVector<qreal> sizes = base;
    if (definite) {
        qreal free = available - gaps;
        QVector<bool> flexing(n, false);
        bool anyFlex = false;
        for (int t = 0; t < n; ++t) {
            if (tracks[t].flexible()) {
                flexing[t] = true;
                anyFlex = true;
            } else {
                free -= base[t];
            }
        }
        if (anyFlex) {
            bool changed = true;
            qreal hypothetical = 0;
            while (changed) {
                changed = false;
                qreal sumFr = 0;
                for (int t = 0; t < n; ++t) {
                    if (flexing[t]) {
                        sumFr += tracks[t].max.value;
                    }
                }
                hypothetical = sumFr > 0 ? std::max<qreal>(free, 0) / sumFr : 0;
                for (int t = 0; t < n; ++t) {
                    if (flexing[t] && base[t] > hypothetical * tracks[t].max.value) {
                        flexing[t] = false;
                        free -= base[t];
                        changed = true;
                    }
                }
            }
            for (int t = 0; t < n; ++t) {
                if (tracks[t].flexible()) {
                    sizes[t] = flexing[t] ? hypothetical * tracks[t].max.value : base[t];
                }
            }
        } else if (free > 0) {
            // No fr track: stretch the content-sized tracks (CSS 12.8).
            QVector<int> autos;
            for (int t = 0; t < n; ++t) {
                if (tracks[t].max.contentSized()) {
                    autos.append(t);
                }
            }
            for (int t : autos) {
                sizes[t] += free / autos.size();
            }
        }
    } else {
        for (int t = 0; t < n; ++t) {
            if (tracks[t].flexible()) {
                sizes[t] = std::max(base[t], content[t]);
            }
        }
    }
    for (int t = 0; t < n; ++t) {
        result.natural[t] = tracks[t].flexible() ? std::max(base[t], content[t])
                          : (tracks[t].max.kind == GridLength::Percent && !definite) ? content[t]
                          : base[t];
        result.sizes[t] = std::max<qreal>(sizes[t], 0);
    }
    return result;
}

} // namespace QindaTK
