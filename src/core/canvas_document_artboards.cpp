// SPDX-License-Identifier: LGPL-3.0-or-later
// Artboards of the reference canvas (D-093): board commands, each one
// recorded CanvasEdit carrying the whole ordered list, the backdrops the
// renderers paint below objects, damage, and per-board SVG export.
#include "canvas_document.h"
#include "canvas_svg_export.h"

#include <QSet>
#include <algorithm>

namespace QindaTK {
namespace {
// Board colours are content saved as #AARRGGBB, so commands store 8 bits.
QColor storedColor(const QColor &color)
{
    return QColor::fromRgba(color.rgba());
}

qsizetype boardIndex(const QVector<CanvasArtboard> &boards, const QString &id)
{
    for (qsizetype i = 0; i < boards.size(); ++i)
        if (boards.at(i).id == id) return i;
    return -1;
}

QVariantMap record(const CanvasArtboard &board)
{
    return {{QStringLiteral("id"), board.id},
            {QStringLiteral("name"), board.name},
            {QStringLiteral("rect"), board.rect},
            {QStringLiteral("background"), board.background},
            {QStringLiteral("bleed"), board.bleed},
            {QStringLiteral("exportRect"), board.exportRect()}};
}
} // namespace

const CanvasArtboard *CanvasDocument::artboard(const QString &id) const
{
    const qsizetype at = boardIndex(m_artboards, id);
    return at < 0 ? nullptr : &m_artboards.at(at);
}

QVariantList CanvasDocument::artboardList() const
{
    QVariantList out;
    out.reserve(m_artboards.size());
    for (const CanvasArtboard &board : m_artboards) out << record(board);
    return out;
}

QVariantMap CanvasDocument::artboardInfo(const QString &id) const
{
    const CanvasArtboard *board = artboard(id);
    return board ? record(*board) : QVariantMap{};
}

QString CanvasDocument::artboardAt(qreal x, qreal y) const
{
    const QPointF point(x, y);
    for (auto it = m_artboards.crbegin(); it != m_artboards.crend(); ++it)
        if (it->rect.contains(point)) return it->id;
    return {};
}

QVector<CanvasBackdrop> CanvasDocument::backdrops() const
{
    QVector<CanvasBackdrop> fills;
    fills.reserve(m_artboards.size());
    for (const CanvasArtboard &board : m_artboards)
        if (board.background.alpha() > 0) fills.append({board.rect, board.background});
    return fills;
}

void CanvasDocument::artboardDamage(const CanvasEdit::ArtboardChange &change,
                                    QVector<QRectF> *damage) const
{
    // A board's pixels change with its rectangle, background or stacking
    // position; a rename or a bleed change repaints nothing.
    const auto compare = [damage](const QVector<CanvasArtboard> &from, const QVector<CanvasArtboard> &to) {
        for (qsizetype i = 0; i < from.size(); ++i) {
            const CanvasArtboard &board = from.at(i);
            const qsizetype at = boardIndex(to, board.id);
            if (at != i || to.at(at).rect != board.rect || to.at(at).background != board.background)
                damage->append(board.rect);
        }
    };
    compare(change.before, change.after);
    compare(change.after, change.before);
}

QString CanvasDocument::addArtboard(const QString &name, const QRectF &rect, const QColor &background)
{
    if (!background.isValid()) {
        refuse(tr("An artboard background must be a valid colour"));
        return {};
    }
    QSet<QString> used;
    for (const CanvasArtboard &board : std::as_const(m_artboards)) used.insert(board.id);
    // AGENT-NOTE: like guide ids, the counter is saved and never reused, so an
    // undone add cannot hand its id to a different board.
    quint64 next = m_nextArtboard;
    QString id = QStringLiteral("artboard-%1").arg(next);
    while (used.contains(id)) id = QStringLiteral("artboard-%1").arg(++next);
    CanvasArtboard board;
    board.id = id;
    board.name = name;
    board.rect = rect;
    board.background = storedColor(background);
    QVector<CanvasArtboard> after = m_artboards;
    after.append(board);
    CanvasEdit edit;
    edit.label = tr("Add artboard");
    edit.artboards = CanvasEdit::ArtboardChange{m_artboards, after};
    const quint64 previous = m_nextArtboard;
    m_nextArtboard = next + 1;
    if (!commitRecorded(edit)) {
        m_nextArtboard = previous;
        return {};
    }
    return id;
}

bool CanvasDocument::setArtboardRect(const QString &id, const QRectF &rect)
{
    const qsizetype at = boardIndex(m_artboards, id);
    if (at < 0) return refuse(tr("The artboard no longer exists"));
    const QRectF current = m_artboards.at(at).rect;
    if (current == rect) return unchanged();
    QVector<CanvasArtboard> after = m_artboards;
    after[at].rect = rect;
    CanvasEdit edit;
    // A pure translation arranges the board; anything else resizes it.
    edit.label = current.size() == rect.size() ? tr("Move artboard") : tr("Resize artboard");
    edit.artboards = CanvasEdit::ArtboardChange{m_artboards, after};
    return commitRecorded(edit);
}

bool CanvasDocument::renameArtboard(const QString &id, const QString &name)
{
    const qsizetype at = boardIndex(m_artboards, id);
    if (at < 0) return refuse(tr("The artboard no longer exists"));
    if (m_artboards.at(at).name == name) return unchanged();
    QVector<CanvasArtboard> after = m_artboards;
    after[at].name = name;
    CanvasEdit edit;
    edit.label = tr("Rename artboard");
    edit.artboards = CanvasEdit::ArtboardChange{m_artboards, after};
    return commitRecorded(edit);
}

bool CanvasDocument::setArtboardBackground(const QString &id, const QColor &color)
{
    const qsizetype at = boardIndex(m_artboards, id);
    if (at < 0) return refuse(tr("The artboard no longer exists"));
    if (!color.isValid()) return refuse(tr("An artboard background must be a valid colour"));
    const QColor stored = storedColor(color);
    if (m_artboards.at(at).background == stored) return unchanged();
    QVector<CanvasArtboard> after = m_artboards;
    after[at].background = stored;
    CanvasEdit edit;
    edit.label = tr("Artboard background");
    edit.artboards = CanvasEdit::ArtboardChange{m_artboards, after};
    return commitRecorded(edit);
}

bool CanvasDocument::setArtboardBleed(const QString &id, qreal bleed)
{
    const qsizetype at = boardIndex(m_artboards, id);
    if (at < 0) return refuse(tr("The artboard no longer exists"));
    if (m_artboards.at(at).bleed == bleed) return unchanged();
    QVector<CanvasArtboard> after = m_artboards;
    after[at].bleed = bleed;
    CanvasEdit edit;
    edit.label = tr("Artboard bleed");
    edit.artboards = CanvasEdit::ArtboardChange{m_artboards, after};
    return commitRecorded(edit);
}

bool CanvasDocument::moveArtboard(const QString &id, int index)
{
    const qsizetype at = boardIndex(m_artboards, id);
    if (at < 0) return refuse(tr("The artboard no longer exists"));
    const qsizetype target = std::clamp<qsizetype>(index, 0, m_artboards.size() - 1);
    if (target == at) return unchanged();
    QVector<CanvasArtboard> after = m_artboards;
    after.move(at, target);
    CanvasEdit edit;
    edit.label = tr("Reorder artboard");
    edit.artboards = CanvasEdit::ArtboardChange{m_artboards, after};
    return commitRecorded(edit);
}

bool CanvasDocument::removeArtboard(const QString &id)
{
    const qsizetype at = boardIndex(m_artboards, id);
    if (at < 0) return refuse(tr("The artboard no longer exists"));
    QVector<CanvasArtboard> after = m_artboards;
    after.removeAt(at);
    CanvasEdit edit;
    edit.label = tr("Delete artboard");
    edit.artboards = CanvasEdit::ArtboardChange{m_artboards, after};
    return commitRecorded(edit);
}

bool CanvasDocument::exportArtboardSvg(const QString &id, const QString &filePath)
{
    const CanvasArtboard *board = artboard(id);
    if (!board) return refuse(tr("The artboard no longer exists"));
    OperationError error;
    if (!CanvasSvgExport::writeRegion(*this, board->exportRect(), board->background, filePath, &error))
        return refuse(error.message);
    if (!m_lastError.isEmpty()) {
        m_lastError.clear();
        emit historyChanged();
    }
    return true;
}

} // namespace QindaTK
