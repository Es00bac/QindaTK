// SPDX-License-Identifier: LGPL-3.0-or-later
// CanvasPathEditing: node insert/type/split/join on contours that carry
// stable node ids, and id carry-over across edits made elsewhere.
#include "canvas_path_edit_model.h"

#include <QLineF>
#include <cmath>

namespace QindaTK::CanvasPathEditing {
namespace {
QPointF lerp(QPointF a, QPointF b, qreal t) { return a + (b - a) * t; }
qreal length(QPointF p) { return std::hypot(p.x(), p.y()); }

// A node's neighbours along its contour (itself when it has none).
QPointF previousAnchor(const CanvasPathContour &c, int i)
{
    if (i > 0) return c.nodes.at(i - 1).anchor;
    return c.closed ? c.nodes.constLast().anchor : c.nodes.at(i).anchor;
}
QPointF nextAnchor(const CanvasPathContour &c, int i)
{
    if (i + 1 < c.nodes.size()) return c.nodes.at(i + 1).anchor;
    return c.closed ? c.nodes.constFirst().anchor : c.nodes.at(i).anchor;
}

void reverse(CanvasPathEditContour &c)
{
    std::reverse(c.contour.nodes.begin(), c.contour.nodes.end());
    std::reverse(c.ids.begin(), c.ids.end());
    for (CanvasPathNode &node : c.contour.nodes) std::swap(node.in, node.out);
}

bool valid(const QVector<CanvasPathEditContour> &path, int contour, int index)
{
    return contour >= 0 && contour < path.size() && index >= 0
        && index < path.at(contour).contour.nodes.size();
}

bool refuse(QString *error, const QString &message)
{
    if (error) *error = message;
    return false;
}
} // namespace

QString nodeType(const CanvasPathContour &c, int i)
{
    const CanvasPathNode &n = c.nodes.at(i);
    const QPointF in = n.anchor - n.in;
    const QPointF out = n.out - n.anchor;
    const qreal a = length(in), b = length(out);
    if (a == 0 || b == 0) return QStringLiteral("corner");
    const qreal scale = std::max(a, b);
    const qreal cross = (in.x() * out.y() - in.y() * out.x()) / (a * b);
    const qreal dot = in.x() * out.x() + in.y() * out.y();
    if (std::abs(cross) > 1e-6 || dot <= 0) return QStringLiteral("corner");
    return std::abs(a - b) <= 1e-9 * scale ? QStringLiteral("symmetric") : QStringLiteral("smooth");
}

bool insert(QVector<CanvasPathEditContour> &path, int contour, int index, qreal t, quint64 newId,
            QString *error)
{
    if (!valid(path, contour, index)) return refuse(error, QStringLiteral("Unknown segment"));
    if (!(t > 0 && t < 1)) return refuse(error, QStringLiteral("A node is inserted strictly inside a segment"));
    CanvasPathEditContour &c = path[contour];
    const int count = c.contour.nodes.size();
    const bool closing = index == count - 1;
    if (closing && !c.contour.closed) return refuse(error, QStringLiteral("The last node of an open contour starts no segment"));
    CanvasPathNode &a = c.contour.nodes[index];
    CanvasPathNode &b = c.contour.nodes[closing ? 0 : index + 1];
    // de Casteljau: the two halves trace the original cubic exactly.
    const QPointF p01 = lerp(a.anchor, a.out, t), p12 = lerp(a.out, b.in, t), p23 = lerp(b.in, b.anchor, t);
    const QPointF p012 = lerp(p01, p12, t), p123 = lerp(p12, p23, t);
    const CanvasPathNode inserted{lerp(p012, p123, t), p012, p123};
    a.out = p01;
    b.in = p23;
    c.contour.nodes.insert(index + 1, inserted);
    c.ids.insert(index + 1, newId);
    return true;
}

bool setType(QVector<CanvasPathEditContour> &path, int contour, int index, const QString &type,
             QString *error)
{
    if (!valid(path, contour, index)) return refuse(error, QStringLiteral("Unknown node"));
    CanvasPathContour &c = path[contour].contour;
    CanvasPathNode &n = c.nodes[index];
    if (type == QLatin1String("corner")) {
        n.in = n.anchor;
        n.out = n.anchor;
        return true;
    }
    if (type != QLatin1String("smooth") && type != QLatin1String("symmetric"))
        return refuse(error, QStringLiteral("Unknown node type: %1").arg(type));
    const QPointF previous = previousAnchor(c, index), next = nextAnchor(c, index);
    QPointF direction = next - previous;
    if (length(direction) == 0) direction = n.out - n.in;
    if (length(direction) == 0) return refuse(error, QStringLiteral("This node has no tangent to align its handles to"));
    direction /= length(direction);
    // Retracted handles take a third of the distance to their neighbour.
    qreal before = length(n.anchor - n.in), after = length(n.out - n.anchor);
    if (before == 0) before = length(n.anchor - previous) / 3;
    if (after == 0) after = length(next - n.anchor) / 3;
    if (type == QLatin1String("symmetric")) before = after = (before + after) / 2;
    n.in = n.anchor - direction * before;
    n.out = n.anchor + direction * after;
    return true;
}

bool split(QVector<CanvasPathEditContour> &path, int contour, int index, quint64 newId, QString *error)
{
    if (!valid(path, contour, index)) return refuse(error, QStringLiteral("Unknown node"));
    CanvasPathEditContour &c = path[contour];
    const int count = c.contour.nodes.size();
    CanvasPathNode copy = c.contour.nodes.at(index);
    if (c.contour.closed) {
        // Rotate so the contour starts at the node and ends at its copy.
        std::rotate(c.contour.nodes.begin(), c.contour.nodes.begin() + index, c.contour.nodes.end());
        std::rotate(c.ids.begin(), c.ids.begin() + index, c.ids.end());
        c.contour.nodes.first().in = c.contour.nodes.first().anchor;
        copy.out = copy.anchor;
        c.contour.nodes.append(copy);
        c.ids.append(newId);
        c.contour.closed = false;
        return true;
    }
    if (index == 0 || index == count - 1)
        return refuse(error, QStringLiteral("This node already ends an open contour"));
    CanvasPathEditContour tail;
    tail.contour.closed = false;
    tail.contour.nodes = c.contour.nodes.mid(index);
    tail.ids = c.ids.mid(index);
    tail.contour.nodes.first().in = tail.contour.nodes.first().anchor;
    tail.ids.first() = newId;
    c.contour.nodes.resize(index + 1);
    c.ids.resize(index + 1);
    c.contour.nodes.last().out = c.contour.nodes.last().anchor;
    path.append(tail);
    return true;
}

bool join(QVector<CanvasPathEditContour> &path, int contourA, int indexA, int contourB, int indexB,
          QString *error)
{
    if (!valid(path, contourA, indexA) || !valid(path, contourB, indexB))
        return refuse(error, QStringLiteral("Unknown node"));
    const auto isEnd = [&path](int contour, int index) {
        const CanvasPathContour &c = path.at(contour).contour;
        return !c.closed && (index == 0 || index == c.nodes.size() - 1);
    };
    if (!isEnd(contourA, indexA) || !isEnd(contourB, indexB))
        return refuse(error, QStringLiteral("Only the end nodes of open contours can be joined"));
    if (contourA == contourB) {
        CanvasPathEditContour &c = path[contourA];
        if (indexA == indexB) return refuse(error, QStringLiteral("Choose two different end nodes"));
        if (c.contour.nodes.first().anchor == c.contour.nodes.last().anchor) {
            if (c.contour.nodes.size() < 4)
                return refuse(error, QStringLiteral("Closing would leave fewer than three nodes"));
            c.contour.nodes.first().in = c.contour.nodes.last().in;
            c.contour.nodes.removeLast();
            c.ids.removeLast();
        }
        c.contour.closed = true;
        return true;
    }
    CanvasPathEditContour a = path.at(contourA);
    CanvasPathEditContour b = path.at(contourB);
    if (indexA == 0 && a.contour.nodes.size() > 1) reverse(a); // a ends at the joined node
    if (indexB != 0) reverse(b);                               // b starts at it
    if (a.contour.nodes.last().anchor == b.contour.nodes.first().anchor) {
        a.contour.nodes.last().out = b.contour.nodes.first().out;
        b.contour.nodes.removeFirst();
        b.ids.removeFirst();
    }
    a.contour.nodes += b.contour.nodes;
    a.ids += b.ids;
    path[contourA] = a;
    path.removeAt(contourB);
    return true;
}

QVector<CanvasPathEditContour> carryIds(const QVector<CanvasPathEditContour> &previous,
                                        const QVector<CanvasPathContour> &next,
                                        const std::function<quint64()> &newId)
{
    QVector<CanvasPathEditContour> result;
    for (int c = 0; c < next.size(); ++c) {
        CanvasPathEditContour out{next.at(c), QVector<quint64>(next.at(c).nodes.size(), 0)};
        if (c < previous.size()) {
            const auto &old = previous.at(c).contour.nodes;
            const auto &oldIds = previous.at(c).ids;
            const auto &now = next.at(c).nodes;
            const auto same = [&](int i, int j) { return old.at(i).anchor == now.at(j).anchor; };
            int n = old.size(), m = now.size(), p = 0;
            for (; p < n && p < m && same(p, p); ++p) out.ids[p] = oldIds.at(p);
            for (; n > p && m > p && same(n - 1, m - 1); --n, --m) out.ids[m - 1] = oldIds.at(n - 1);
            // An equal-length run of changed nodes between matches keeps its
            // ids by position (a moved node or handle).
            int gapI = p, gapJ = p;
            const auto closeGap = [&](int endI, int endJ) {
                if (endI - gapI == endJ - gapJ)
                    for (int k = 0; k < endJ - gapJ; ++k) out.ids[gapJ + k] = oldIds.at(gapI + k);
            };
            const qsizetype rows = n - p, columns = m - p;
            // AGENT-GUARD: the LCS table is rows x columns; beyond this bound
            // (a mass edit of a huge contour) only the positional rule runs.
            if (rows > 0 && columns > 0 && rows * columns <= (1 << 18)) {
                QVector<int> lcs((rows + 1) * (columns + 1), 0);
                const auto at = [&](qsizetype i, qsizetype j) -> int & { return lcs[i * (columns + 1) + j]; };
                for (qsizetype i = rows - 1; i >= 0; --i)
                    for (qsizetype j = columns - 1; j >= 0; --j)
                        at(i, j) = same(int(p + i), int(p + j)) ? at(i + 1, j + 1) + 1
                                                                 : std::max(at(i + 1, j), at(i, j + 1));
                qsizetype i = 0, j = 0;
                while (i < rows && j < columns) {
                    if (same(int(p + i), int(p + j))) {
                        closeGap(int(p + i), int(p + j));
                        out.ids[p + j] = oldIds.at(p + i);
                        gapI = int(p + ++i);
                        gapJ = int(p + ++j);
                    } else if (at(i + 1, j) >= at(i, j + 1)) {
                        ++i;
                    } else {
                        ++j;
                    }
                }
            }
            closeGap(n, m);
        }
        for (quint64 &id : out.ids)
            if (id == 0) id = newId();
        result.append(out);
    }
    return result;
}

} // namespace QindaTK::CanvasPathEditing
