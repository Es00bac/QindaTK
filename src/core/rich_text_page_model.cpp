// SPDX-License-Identifier: LGPL-3.0-or-later
#include "rich_text_page_model.h"

namespace QindaTK {
using namespace RichText;

int RichTextVisiblePages::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_entries.size());
}

QVariant RichTextVisiblePages::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_entries.size()) return {};
    const Entry &entry = m_entries.at(index.row());
    switch (role) {
    case PageIndexRole: return entry.page;
    case PageKeyRole: return entry.key.toString(QUuid::WithoutBraces);
    case RevisionRole: return QString::number(entry.revision);
    case TileRectRole: return entry.tile;
    case SourceRectRole: return entry.source;
    case PageRectRole: return entry.pageRect;
    default: return {};
    }
}

QHash<int, QByteArray> RichTextVisiblePages::roleNames() const
{
    return {{PageIndexRole, "pageIndex"}, {PageKeyRole, "pageKey"}, {RevisionRole, "revision"},
            {TileRectRole, "tileRect"}, {SourceRectRole, "sourceRect"}, {PageRectRole, "pageRect"}};
}

void RichTextVisiblePages::update(const QVector<Entry> &entries)
{
    // Rows are keyed by page identity: a page that stays visible keeps its
    // delegate and only its geometry changes; others are inserted/removed.
    const int before = count();
    int row = 0;
    while (row < m_entries.size() || row < entries.size()) {
        if (row >= entries.size()) {
            beginRemoveRows({}, row, int(m_entries.size()) - 1);
            m_entries.resize(row);
            endRemoveRows();
            break;
        }
        if (row >= m_entries.size()) {
            beginInsertRows({}, row, int(entries.size()) - 1);
            m_entries += entries.mid(row);
            endInsertRows();
            break;
        }
        const Entry &next = entries.at(row);
        if (m_entries.at(row).key == next.key && m_entries.at(row).page == next.page) {
            if (!(m_entries.at(row) == next)) {
                m_entries[row] = next;
                emit dataChanged(index(row), index(row));
            }
            ++row;
            continue;
        }
        const bool laterInNew = std::any_of(entries.begin() + row, entries.end(), [&](const Entry &e) {
            return e.key == m_entries.at(row).key; });
        if (!laterInNew) {
            beginRemoveRows({}, row, row);
            m_entries.removeAt(row);
            endRemoveRows();
        } else {
            beginInsertRows({}, row, row);
            m_entries.insert(row, next);
            endInsertRows();
            ++row;
        }
    }
    if (count() != before) emit countChanged();
}

void RichTextPageModel::setArea(RichTextArea *area)
{
    if (m_area == area) return;
    if (m_area) disconnect(m_area, nullptr, this, nullptr);
    beginResetModel();
    m_area = area;
    m_count = area ? area->pageCount() : 0;
    m_current = area ? area->currentPage() : -1;
    endResetModel();
    if (m_area) {
        connect(m_area->layout(), &RichTextLayout::pagesChanged, this, &RichTextPageModel::onPagesChanged);
        connect(m_area, &RichTextArea::caretChanged, this, [this] {
            const int current = m_area->currentPage();
            if (current == m_current) return;
            const int previous = m_current;
            m_current = current;
            if (previous >= 0 && previous < m_count) emit dataChanged(index(previous), index(previous), {CurrentRole});
            if (current >= 0 && current < m_count) emit dataChanged(index(current), index(current), {CurrentRole});
        });
    }
    emit areaChanged();
    emit countChanged();
}

void RichTextPageModel::onPagesChanged(int firstChanged, int pageCount)
{
    if (pageCount > m_count) {
        beginInsertRows({}, m_count, pageCount - 1);
        m_count = pageCount;
        endInsertRows();
        emit countChanged();
    } else if (pageCount < m_count) {
        beginRemoveRows({}, pageCount, m_count - 1);
        m_count = pageCount;
        endRemoveRows();
        emit countChanged();
    }
    if (firstChanged < m_count) emit dataChanged(index(qMax(0, firstChanged)), index(m_count - 1));
    ++m_generation;
    emit generationChanged();
}

int RichTextPageModel::sectionIndex(int page) const
{
    if (!m_area || !m_area->document() || page < 0 || page >= m_area->pageCount()) return -1;
    return int(m_area->document()->sectionIds().indexOf(m_area->layout()->page(page).section));
}

int RichTextPageModel::sectionCount() const
{
    return m_area && m_area->document() ? int(m_area->document()->sectionIds().size()) : 0;
}

int RichTextPageModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_count;
}

QVariant RichTextPageModel::data(const QModelIndex &index, int role) const
{
    if (!m_area || !index.isValid() || index.row() >= m_area->pageCount()) return {};
    const Page &page = m_area->layout()->page(index.row());
    switch (role) {
    case PageIndexRole: return index.row();
    case PageKeyRole: return page.key.toString(QUuid::WithoutBraces);
    case RevisionRole: return QString::number(page.revision);
    case LabelRole: return QString::number(index.row() + 1);
    case SectionRole: return page.section.toString(QUuid::WithoutBraces);
    case OrdinalRole: return page.ordinal;
    case CurrentRole: return index.row() == m_current;
    case HeadingRole: {
        const RichTextDocument *doc = m_area->document();
        for (const PageItem &item : page.items)
            if (item.kind == PageItem::Lines && item.first == 0)
                if (const Paragraph *p = doc->paragraph(item.block); p && p->format.headingLevel > 0)
                    return p->text.left(80);
        return QString();
    }
    default: return {};
    }
}

QHash<int, QByteArray> RichTextPageModel::roleNames() const
{
    return {{PageIndexRole, "pageIndex"}, {PageKeyRole, "pageKey"}, {RevisionRole, "revision"},
            {LabelRole, "label"}, {SectionRole, "section"}, {OrdinalRole, "ordinal"},
            {HeadingRole, "heading"}, {CurrentRole, "current"}};
}

QVariantList RichTextPageModel::outline(int limit) const
{
    QVariantList result;
    if (!m_area || !m_area->document()) return result;
    for (const Paragraph *p : m_area->document()->paragraphsInOrder()) {
        if (p->format.headingLevel <= 0 || p->table) continue;
        if (result.size() >= limit) break;
        QString text = p->text;
        text.remove(QChar(ObjectChar));
        result.append(QVariantMap{{QStringLiteral("paragraphId"), p->id.toString(QUuid::WithoutBraces)},
                                  {QStringLiteral("level"), p->format.headingLevel},
                                  {QStringLiteral("text"), text.left(120)},
                                  {QStringLiteral("page"), m_area->layout()->pageOfPosition({p->id, 0})}});
    }
    return result;
}

} // namespace QindaTK
