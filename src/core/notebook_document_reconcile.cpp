// SPDX-License-Identifier: LGPL-3.0-or-later
#include "notebook_document_p.h"

// Reconciling another version of a notebook (docs/notebooks.md#reconcile):
// the file changed on disk while this copy was edited, or an offline copy
// comes back. Stable IDs match sections and pages across the versions.
namespace QindaTK {
using namespace Notebook;

namespace {
template <typename T>
int indexById(const QVector<T> &items, const QString &id)
{
    for (int i = 0; i < items.size(); ++i) if (items.at(i).id == id) return i;
    return -1;
}

// Gives every block of `page` whose ID is taken (or every block, `all`) a
// fresh ID and rewires parents and chart sources inside the page, so a page
// from the other version can join this one without an ID collision.
void freshen(Page *page, QSet<QString> *taken, bool all)
{
    QHash<QString, QString> renamed;
    for (Block &block : page->blocks) {
        if (all || taken->contains(block.id)) {
            const QString id = NotebookTree::newId();
            renamed.insert(block.id, id);
            block.id = id;
        }
        taken->insert(block.id);
    }
    for (Block &block : page->blocks) {
        block.parentId = renamed.value(block.parentId, block.parentId);
        const QString source = block.data.value(QStringLiteral("sourceBlockId")).toString();
        if (block.type == QLatin1String("chart") && renamed.contains(source))
            block.data.insert(QStringLiteral("sourceBlockId"), renamed.value(source));
    }
}

// Global page position just after the last page of `sectionId` (or the end).
int positionInSection(const QVector<Page> &pages, const QString &sectionId)
{
    int position = int(pages.size());
    for (int i = 0; i < pages.size(); ++i) if (pages.at(i).sectionId == sectionId) position = i + 1;
    return position;
}
} // namespace

// AGENT-GUARD: reconciliation only adds or replaces; it never deletes a
// section, page or block, so no attachment (image resource ID, embed target,
// unsupported block) is dropped. A page changed on both sides keeps this
// side and gains "<title> (other version)" with fresh block IDs. The whole
// merge is one undo step, so the user can take it back.
QVariantMap NotebookDocument::reconcileJson(const QString &json, const QString &baseJson)
{
    State theirs, base;
    QString problem;
    if (!decodeNative(json.toUtf8(), &theirs, nullptr, nullptr, nullptr, &problem)) {
        fail(problem);
        return {};
    }
    const bool hasBase = !baseJson.isEmpty();
    if (hasBase && !decodeNative(baseJson.toUtf8(), &base, nullptr, nullptr, nullptr, &problem)) {
        fail(QStringLiteral("Base version: %1").arg(problem));
        return {};
    }
    State next = m_state;
    QSet<QString> taken;
    for (const Section &section : next.sections) taken.insert(section.id);
    for (const Page &page : next.pages) {
        taken.insert(page.id);
        for (const Block &block : page.blocks) taken.insert(block.id);
    }
    int sectionsAdded = 0, pagesAdded = 0, pagesUpdated = 0, copies = 0;
    if (hasBase && m_state.title == base.title) next.title = theirs.title;
    for (const Section &section : std::as_const(theirs.sections)) {
        const int mine = indexById(next.sections, section.id);
        if (mine < 0) {
            next.sections.append(section);
            taken.insert(section.id);
            ++sectionsAdded;
        } else if (hasBase) {
            const int ancestor = indexById(base.sections, section.id);
            if (ancestor >= 0 && next.sections.at(mine) == base.sections.at(ancestor))
                next.sections[mine] = section;
        }
    }
    for (Page page : std::as_const(theirs.pages)) {
        const int mine = indexById(next.pages, page.id);
        if (mine < 0) {
            taken.insert(page.id);
            freshen(&page, &taken, false);
            next.pages.insert(positionInSection(next.pages, page.sectionId), page);
            ++pagesAdded;
            continue;
        }
        if (next.pages.at(mine) == page) continue;
        const int ancestor = hasBase ? indexById(base.pages, page.id) : -1;
        if (ancestor >= 0 && page == base.pages.at(ancestor)) continue; // only this side changed
        if (ancestor >= 0 && next.pages.at(mine) == base.pages.at(ancestor)) {
            // Only the other side changed: take it, keeping IDs unique.
            for (const Block &block : next.pages.at(mine).blocks) taken.remove(block.id);
            freshen(&page, &taken, false);
            next.pages[mine] = page;
            ++pagesUpdated;
            continue;
        }
        page.id = NotebookTree::newId();
        page.title = tr("%1 (other version)").arg(page.title).left(512);
        taken.insert(page.id);
        freshen(&page, &taken, true);
        next.pages.insert(mine + 1, page);
        ++copies;
    }
    int blocks = 0;
    for (const Page &page : std::as_const(next.pages)) blocks += int(page.blocks.size());
    if (next.sections.size() > Limits::MaxSections || next.pages.size() > Limits::MaxPages
        || blocks > Limits::MaxBlocks) {
        fail(QStringLiteral("The reconciled notebook would exceed the section, page or block limit"));
        return {};
    }
    const bool changedAny = next != m_state;
    if (changedAny && !commit(next, tr("Reconcile notebook"))) return {};
    if (!changedAny) clearError();
    return {{"changed", changedAny}, {"sectionsAdded", sectionsAdded}, {"pagesAdded", pagesAdded},
            {"pagesUpdated", pagesUpdated}, {"conflictCopies", copies}};
}

} // namespace QindaTK
