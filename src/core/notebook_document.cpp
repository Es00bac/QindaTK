// SPDX-License-Identifier: LGPL-3.0-or-later
#include "notebook_document_p.h"
#include "undo_service.h"

namespace QindaTK {
using namespace Notebook;

namespace {
QString trimmedTitle(const QString &title, const QString &fallback)
{
    const QString value = title.trimmed().left(512);
    return value.isEmpty() ? fallback : value;
}
QString excerptOf(const Page &page)
{
    for (const Block &block : page.blocks) {
        if (block.type == QLatin1String("heading")) continue;
        const QString text = NotebookText::plainText(block).simplified();
        if (!text.isEmpty()) return text.left(160);
    }
    return {};
}
} // namespace

NotebookDocument::NotebookDocument(QObject *parent) : QObject(parent)
{
    m_documentId = DocumentId::create().toString();
    m_state.title = tr("Notebook");
    m_localHistory = new UndoService(this);
    m_localHistory->setMemoryLimit(quint64(256) << 20);
    m_localHistory->addModel(this);
    m_localHistory->setStepLimit(this, 200);
}

NotebookDocument::~NotebookDocument()
{
    disconnect(m_evaluationConnection);
}

bool NotebookDocument::fail(const QString &message) const
{
    m_error = message;
    emit const_cast<NotebookDocument *>(this)->errorChanged();
    return false;
}

void NotebookDocument::clearError() const
{
    if (m_error.isEmpty()) return;
    m_error.clear();
    emit const_cast<NotebookDocument *>(this)->errorChanged();
}

int NotebookDocument::pageIndex(const QString &pageId) const
{
    for (int i = 0; i < m_state.pages.size(); ++i)
        if (m_state.pages.at(i).id == pageId) return i;
    return -1;
}

int NotebookDocument::sectionIndex(const QString &sectionId) const
{
    for (int i = 0; i < m_state.sections.size(); ++i)
        if (m_state.sections.at(i).id == sectionId) return i;
    return -1;
}

NotebookDocument::Location NotebookDocument::locate(const QString &blockId) const
{
    if (!m_indexValid) {
        m_blockIndex.clear();
        for (int p = 0; p < m_state.pages.size(); ++p) {
            const auto &blocks = m_state.pages.at(p).blocks;
            for (int b = 0; b < blocks.size(); ++b) m_blockIndex.insert(blocks.at(b).id, {p, b});
        }
        m_indexValid = true;
    }
    return m_blockIndex.value(blockId);
}

bool NotebookDocument::editableBlock(const QString &blockId, Location *where) const
{
    const Location location = locate(blockId);
    if (!location.valid()) return fail(QStringLiteral("Unknown block %1").arg(blockId));
    if (m_state.pages.at(location.page).blocks.at(location.block).locked)
        return fail(QStringLiteral("Block %1 is read only").arg(blockId));
    if (where) *where = location;
    return true;
}

int NotebookDocument::blockCount() const
{
    int count = 0;
    for (const Page &page : m_state.pages) count += page.blocks.size();
    return count;
}

void NotebookDocument::setTitle(const QString &title)
{
    State next = m_state;
    next.title = trimmedTitle(title, tr("Notebook"));
    commit(next, tr("Rename notebook"));
}

QVariantList NotebookDocument::sections() const
{
    QVariantList result;
    for (const Section &section : m_state.sections) {
        int count = 0;
        for (const Page &page : m_state.pages) count += page.sectionId == section.id;
        result.append(QVariantMap{{"id", section.id}, {"title", section.title},
                                  {"collapsed", m_collapsed.contains(section.id)}, {"pageCount", count}});
    }
    return result;
}

QVariantList NotebookDocument::pages() const
{
    QVariantList result;
    for (const Page &page : m_state.pages)
        result.append(QVariantMap{{"id", page.id}, {"sectionId", page.sectionId}, {"title", page.title},
                                  {"tags", page.tags}, {"excerpt", excerptOf(page)},
                                  {"blockCount", int(page.blocks.size())}, {"status", QStringLiteral("ready")}});
    return result;
}

QVariantMap NotebookDocument::page(const QString &pageId) const
{
    const int index = pageIndex(pageId);
    if (index < 0) return {};
    return pages().at(index).toMap();
}

QVariantList NotebookDocument::blocks(const QString &pageId) const
{
    const int index = pageIndex(pageId);
    if (index < 0) return {};
    const auto &list = m_state.pages.at(index).blocks;
    const QVector<int> depth = NotebookTree::depths(list);
    QVariantList result;
    result.reserve(list.size());
    for (int i = 0; i < list.size(); ++i) {
        const Block &block = list.at(i);
        result.append(QVariantMap{{"id", block.id}, {"type", block.type}, {"parentId", block.parentId},
                                  {"depth", depth.at(i)}, {"index", i}, {"locked", block.locked},
                                  {"data", block.data.toVariantMap()},
                                  {"supported", NotebookText::isKnownType(block.type)}});
    }
    return result;
}

QVariantMap NotebookDocument::block(const QString &blockId) const
{
    const Location location = locate(blockId);
    if (!location.valid()) return {};
    const Page &page = m_state.pages.at(location.page);
    const Block &block = page.blocks.at(location.block);
    return {{"id", block.id}, {"type", block.type}, {"parentId", block.parentId},
            {"pageId", page.id}, {"index", location.block}, {"locked", block.locked},
            {"depth", NotebookTree::depths(page.blocks).at(location.block)},
            {"data", block.data.toVariantMap()}, {"supported", NotebookText::isKnownType(block.type)}};
}

QString NotebookDocument::pageOfBlock(const QString &blockId) const
{
    const Location location = locate(blockId);
    return location.valid() ? m_state.pages.at(location.page).id : QString();
}

QString NotebookDocument::addSection(const QString &title, int index)
{
    if (m_state.sections.size() >= Limits::MaxSections) {
        fail(QStringLiteral("A notebook holds at most 1,000 sections"));
        return {};
    }
    State next = m_state;
    const Section section{NotebookTree::newId(), trimmedTitle(title, tr("Section"))};
    if (index < 0 || index > next.sections.size()) index = int(next.sections.size());
    next.sections.insert(index, section);
    return commit(next, tr("Add section")) ? section.id : QString();
}

bool NotebookDocument::renameSection(const QString &sectionId, const QString &title)
{
    const int index = sectionIndex(sectionId);
    if (index < 0) return fail(QStringLiteral("Unknown section %1").arg(sectionId));
    State next = m_state;
    next.sections[index].title = trimmedTitle(title, next.sections.at(index).title);
    return commit(next, tr("Rename section"));
}

bool NotebookDocument::removeSection(const QString &sectionId)
{
    const int index = sectionIndex(sectionId);
    if (index < 0) return fail(QStringLiteral("Unknown section %1").arg(sectionId));
    State next = m_state;
    next.sections.removeAt(index);
    next.pages.removeIf([&](const Page &page) { return page.sectionId == sectionId; });
    return commit(next, tr("Delete section"));
}

bool NotebookDocument::setSectionCollapsed(const QString &sectionId, bool collapsed)
{
    if (sectionIndex(sectionId) < 0) return fail(QStringLiteral("Unknown section %1").arg(sectionId));
    if (m_collapsed.contains(sectionId) == collapsed) return true;
    if (collapsed) m_collapsed.insert(sectionId);
    else m_collapsed.remove(sectionId);
    emit changed();
    return true;
}

QString NotebookDocument::addPage(const QString &sectionId, const QString &afterPageId, const QString &title)
{
    if (sectionIndex(sectionId) < 0) {
        fail(QStringLiteral("Unknown section %1").arg(sectionId));
        return {};
    }
    if (m_state.pages.size() >= Limits::MaxPages) {
        fail(QStringLiteral("A notebook holds at most 10,000 pages"));
        return {};
    }
    State next = m_state;
    Page page;
    page.id = NotebookTree::newId();
    page.sectionId = sectionId;
    page.title = trimmedTitle(title, tr("Untitled page"));
    int position = int(next.pages.size());
    const int after = pageIndex(afterPageId);
    if (after >= 0 && next.pages.at(after).sectionId == sectionId) {
        position = after + 1;
    } else {
        for (int i = 0; i < next.pages.size(); ++i)
            if (next.pages.at(i).sectionId == sectionId) position = i + 1;
    }
    next.pages.insert(position, page);
    return commit(next, tr("Add page")) ? page.id : QString();
}

bool NotebookDocument::renamePage(const QString &pageId, const QString &title)
{
    const int index = pageIndex(pageId);
    if (index < 0) return fail(QStringLiteral("Unknown page %1").arg(pageId));
    State next = m_state;
    next.pages[index].title = trimmedTitle(title, next.pages.at(index).title);
    return commit(next, tr("Rename page"));
}

bool NotebookDocument::setPageTags(const QString &pageId, const QStringList &tags)
{
    const int index = pageIndex(pageId);
    if (index < 0) return fail(QStringLiteral("Unknown page %1").arg(pageId));
    QStringList clean;
    for (const QString &tag : tags) {
        const QString value = tag.trimmed().left(128);
        if (!value.isEmpty() && !clean.contains(value)) clean.append(value);
    }
    if (clean.size() > 64) return fail(QStringLiteral("A page holds at most 64 tags"));
    State next = m_state;
    next.pages[index].tags = clean;
    return commit(next, tr("Edit page tags"));
}

bool NotebookDocument::movePage(const QString &pageId, const QString &targetSectionId, int index)
{
    const int source = pageIndex(pageId);
    if (source < 0) return fail(QStringLiteral("Unknown page %1").arg(pageId));
    if (sectionIndex(targetSectionId) < 0) return fail(QStringLiteral("Unknown section %1").arg(targetSectionId));
    State next = m_state;
    Page page = next.pages.takeAt(source);
    page.sectionId = targetSectionId;
    QVector<int> members;
    for (int i = 0; i < next.pages.size(); ++i)
        if (next.pages.at(i).sectionId == targetSectionId) members.append(i);
    int position;
    if (index >= 0 && index < members.size()) position = members.at(index);
    else if (!members.isEmpty()) position = members.last() + 1;
    else {
        // An empty section: place after the pages of the sections before it.
        const int order = sectionIndex(targetSectionId);
        position = 0;
        for (int i = 0; i < next.pages.size(); ++i)
            if (sectionIndex(next.pages.at(i).sectionId) < order) position = i + 1;
    }
    next.pages.insert(position, page);
    return commit(next, tr("Move page"));
}

bool NotebookDocument::removePage(const QString &pageId)
{
    const int index = pageIndex(pageId);
    if (index < 0) return fail(QStringLiteral("Unknown page %1").arg(pageId));
    State next = m_state;
    next.pages.removeAt(index);
    return commit(next, tr("Delete page"));
}

QVariantMap NotebookDocument::caret(const QString &blockId, int offset) const
{
    return {{"blockId", blockId}, {"offset", offset}, {"pageId", pageOfBlock(blockId)}};
}

} // namespace QindaTK
