// SPDX-License-Identifier: LGPL-3.0-or-later
#include "grid_tracks.h"

#include <QStringView>

#include <algorithm>
#include <limits>
#include <cmath>

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
        if (ok && std::isfinite(v) && v >= 0) {
            *out = {GridLength::Fr, v};
        }
        return ok && std::isfinite(v) && v >= 0;
    }
    if (t.endsWith(u'%')) {
        const qreal v = t.chopped(1).toDouble(&ok);
        if (ok && std::isfinite(v) && v >= 0) {
            *out = {GridLength::Percent, v};
        }
        return ok && std::isfinite(v) && v >= 0;
    }
    QStringView number = t;
    if (number.endsWith(u"px")) {
        number = number.chopped(2);
    }
    const qreal v = number.toDouble(&ok);
    if (ok && std::isfinite(v) && v >= 0) {
        *out = {GridLength::Fixed, v};
    }
    return ok && std::isfinite(v) && v >= 0;
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

bool parseList(QStringView spec, QVector<GridTrack> *out, QString *error, int depth = 0)
{
    if (depth > 16) {
        if (error) *error = QStringLiteral("repeat() nesting exceeds 16");
        return false;
    }
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
                const bool automatic = head.trimmed() == u"auto-fill" || head.trimmed() == u"auto-fit";
                if ((!ok || count < 0 || count > 4096) && !automatic) {
                    if (error) *error = QStringLiteral("repeat() count must be an integer");
                    return false;
                }
                QVector<GridTrack> inner_tracks;
                if (!parseList(tail, &inner_tracks, error, depth + 1)) {
                    return false;
                }
                if (automatic) {
                    // AGENT-CONTRACT: D-020 bounds responsive repeats to a sole
                    // minmax track with a definite positive minimum.
                    if (depth != 0 || !out->isEmpty() || !rest.mid(close + 1).trimmed().isEmpty()
                        || inner_tracks.size() != 1 || !tail.trimmed().startsWith(u"minmax(")) {
                        if (error) *error = QStringLiteral("auto-repeat must be the sole track and contain one minmax()");
                        return false;
                    }
                    GridTrack track = inner_tracks.front();
                    if ((track.min.kind != GridLength::Fixed && track.min.kind != GridLength::Percent)
                        || track.min.value <= 0 || (track.max.kind != GridLength::Fr
                        && track.max.kind != GridLength::Fixed && track.max.kind != GridLength::Percent)) {
                        if (error) *error = QStringLiteral("auto-repeat needs a positive definite minimum and definite or fr maximum");
                        return false;
                    }
                    track.repeat = head.trimmed() == u"auto-fit" ? GridTrack::AutoFit : GridTrack::AutoFill;
                    out->append(track);
                    return true;
                }
                if (out->size() + qint64(count) * inner_tracks.size() > 4096) {
                    if (error) *error = QStringLiteral("track list exceeds 4096 tracks");
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

QVector<GridTrack> expandGridTracks(const QVector<GridTrack> &tracks, qreal available, qreal gap)
{
    if (tracks.size() != 1 || tracks.front().repeat == GridTrack::None)
        return tracks;
    const auto &track = tracks.front();
    const auto resolve = [available](GridLength length) {
        return length.kind == GridLength::Percent ? available * length.value / 100 : length.value;
    };
    const qreal minimum = resolve(track.min);
    const qreal breadth = track.flexible() ? minimum : std::max(minimum, resolve(track.max));
    const qreal stride = std::max<qreal>(1, breadth) + std::max<qreal>(0, gap);
    const int count = available > 0 ? int(std::clamp(std::floor((available + std::max<qreal>(0, gap)) / stride), 1.0, 4096.0)) : 1;
    return QVector<GridTrack>(count, track);
}

GridTrackSizes sizeGridTracks(const QVector<GridTrack> &tracks,
                              const QVector<GridContribution> &items, qreal available, qreal gap)
{
    const int n = tracks.size();
    GridTrackSizes result;
    result.gaps = QVector<qreal>(n, gap);
    if (n > 0) result.gaps[n - 1] = 0;
    result.sizes = QVector<qreal>(n, 0);
    result.natural = QVector<qreal>(n, 0);
    if (n == 0) {
        return result;
    }
    QVector<bool> occupied(n, false);
    for (const auto &item : items)
        for (int t = std::max(0, item.start); t < std::min(n, item.start + item.span); ++t)
            occupied[t] = true;
    QVector<GridTrack> active;
    QVector<int> mapping(n, -1);
    for (int t = 0; t < n; ++t) {
        if (tracks[t].repeat == GridTrack::AutoFit && !occupied[t]) continue;
        mapping[t] = active.size();
        auto track = tracks[t];
        track.repeat = GridTrack::None;
        active.append(track);
    }
    if (active.size() != n) {
        QVector<GridContribution> remapped;
        for (const auto &item : items) {
            if (item.start < 0 || item.start >= n || mapping[item.start] < 0) continue;
            int span = 0;
            for (int t = item.start; t < std::min(n, item.start + item.span); ++t)
                if (mapping[t] >= 0) ++span;
            remapped.append({mapping[item.start], span, item.size});
        }
        const auto compact = sizeGridTracks(active, remapped, available, gap);
        result.gaps.fill(0);
        for (int t = 0; t < n; ++t) {
            if (mapping[t] < 0) continue;
            result.sizes[t] = compact.sizes[mapping[t]];
            result.natural[t] = compact.natural[mapping[t]];
            result.gaps[t] = compact.gaps[mapping[t]];
        }
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
            base[t] = std::min(limit[t], base[t] + extra / targets.size());
        }
    }

    // Grow finite minmax ranges before handing remaining space to fr/auto.
    // A fixed upper bound never becomes an unbounded stretch target.
    if (definite) {
        qreal remaining = available - gap * (n - 1);
        for (qreal value : base) remaining -= value;
        while (remaining > 0) {
            QVector<int> growable;
            for (int t = 0; t < n; ++t)
                if (!tracks[t].flexible() && std::isfinite(limit[t]) && base[t] < limit[t])
                    growable.append(t);
            if (growable.isEmpty()) break;
            const qreal share = remaining / growable.size();
            qreal grown = 0;
            for (int t : growable) {
                const qreal delta = std::min(share, limit[t] - base[t]);
                base[t] += delta;
                grown += delta;
            }
            if (grown <= 0) break;
            remaining -= grown;
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
