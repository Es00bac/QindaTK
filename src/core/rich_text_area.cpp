// SPDX-License-Identifier: LGPL-3.0-or-later
// RichTextArea: document binding, geometry, scrolling and visible tiles.
#include "rich_text_area.h"
#include "rich_text_clipboard.h"
#include "rich_text_format_state.h"
#include "rich_text_objects.h"
#include "rich_text_asset_renderer.h"

#include <QCursor>
#include <QGuiApplication>
#include <QStyleHints>
#include <cmath>

namespace QindaTK {
using namespace RichText;

RichTextArea::RichTextArea(QQuickItem *parent)
    : QQuickItem(parent),
      m_layout(std::make_unique<RichTextLayout>()),
      m_visible(new RichTextVisiblePages(this)),
      m_formatState(new RichTextFormatState(this)),
      m_defaultRenderer(std::make_unique<RichTextImageRenderer>()),
      m_assetRenderer(std::make_unique<RichTextAssetRenderer>(this)),
      m_clipboard(std::make_unique<RichTextClipboard>())
{
    m_renderer = m_defaultRenderer.get();
    setFlag(ItemIsFocusScope, false);
    setFlag(ItemAcceptsInputMethod, true);
    setActiveFocusOnTab(true);
    setAcceptedMouseButtons(Qt::LeftButton);
    setAcceptHoverEvents(true);
    setCursor(QCursor(Qt::IBeamCursor));
    setClip(true);
    connect(m_layout.get(), &RichTextLayout::pagesChanged, this, &RichTextArea::onPagesChanged);
    connect(m_assetRenderer.get(), &RichTextAssetRenderer::updated,
            this, [this] { emit repaintRequested(-1); });
    // A width change (continuous mode) or a document reset drops all pages;
    // lay out the viewport again at the next turn of the event loop.
    connect(m_layout.get(), &RichTextLayout::layoutReset, this, &RichTextArea::schedulePagination,
            Qt::QueuedConnection);
    connect(this, &RichTextArea::colorsChanged, this, [this] { emit repaintRequested(-1); });
    connect(this, &RichTextArea::caretChanged, this, &RichTextArea::refreshViewState);
    connect(this, &RichTextArea::viewportChanged, this, [this] {
        refreshViewState();
        // The caret's item rectangle moved; keep the candidate window with it.
        if (hasActiveFocus()) QGuiApplication::inputMethod()->update(Qt::ImCursorRectangle | Qt::ImAnchorRectangle);
    });
}

RichTextArea::~RichTextArea()
{
    if (m_document) {
        m_document->unregisterAnchor(&m_anchor);
        m_document->unregisterAnchor(&m_focus);
    }
}

void RichTextArea::setDocument(RichTextDocument *document)
{
    if (m_document == document) return;
    if (m_document) {
        disconnect(m_document, nullptr, this, nullptr);
        m_document->unregisterAnchor(&m_anchor);
        m_document->unregisterAnchor(&m_focus);
    }
    clearPreedit();
    m_document = document;
    m_pendingFormat.reset();
    m_goalX = -1;
    // AGENT-GUARD: the layout must handle every document change before this
    // area does. onContentChanged emits caretChanged, and the getters bound
    // to it (cursorRectangle, caretFrameId, caretContext) read the layout's
    // pages; connected the other way round they read pages built for the
    // previous content (tst_publishing_example crashed on a style undo).
    m_layout->setDocument(m_document);
    if (m_document) {
        m_anchor.position = m_focus.position = m_document->startPosition();
        m_document->registerAnchor(&m_anchor);
        m_document->registerAnchor(&m_focus);
        connect(m_document, &RichTextDocument::contentChanged, this, [this] { onContentChanged(); });
        connect(m_document, &RichTextDocument::historyNavigated, this,
                [this](const Selection &selection, bool) { onHistoryNavigated(selection); });
        connect(m_document, &RichTextDocument::changed, this, [this] {
            setFlag(ItemAcceptsInputMethod, !m_readOnly && m_document && !m_document->readOnly());
        });
        connect(m_document, &QObject::destroyed, this, [this] { setDocument(nullptr); });
    }
    m_assetRenderer->setDocument(m_document);
    updateLayoutWidth();
    schedulePagination();
    m_formatState->refresh();
    emit documentChanged();
    emit caretChanged();
    emit selectionChanged();
    emit accessibleTextChanged();
}

AssetProvider *RichTextArea::assetProvider() const { return m_assetRenderer->provider(); }

void RichTextArea::setAssetProvider(AssetProvider *provider)
{
    if (assetProvider() == provider) return;
    const bool usingDefault = m_renderer == m_defaultRenderer.get() || m_renderer == m_assetRenderer.get();
    m_assetRenderer->setProvider(provider);
    if (usingDefault) m_renderer = provider ? static_cast<RichTextObjectRenderer *>(m_assetRenderer.get())
                                           : static_cast<RichTextObjectRenderer *>(m_defaultRenderer.get());
    emit assetProviderChanged();
    emit repaintRequested(-1);
}

void RichTextArea::setPaged(bool paged)
{
    if (this->paged() == paged) return;
    m_layout->setMode(paged ? RichTextLayout::Mode::Paged : RichTextLayout::Mode::Continuous);
    updateLayoutWidth();
    schedulePagination();
    clampScroll();
    emit pagedChanged();
}

void RichTextArea::setReadOnly(bool readOnly)
{
    if (m_readOnly == readOnly) return;
    m_readOnly = readOnly;
    if (readOnly) clearPreedit();
    setFlag(ItemAcceptsInputMethod, !readOnly);
    m_formatState->refresh();
    emit readOnlyChanged();
}

void RichTextArea::setZoom(qreal zoom)
{
    zoom = qBound<qreal>(0.1, zoom, 8.0);
    if (qFuzzyCompare(m_zoom, zoom)) return;
    // Keep the centre of the viewport on the same document point.
    const qreal centre = (m_contentY + height() / 2) / m_zoom;
    m_zoom = zoom;
    updateLayoutWidth();
    m_contentY = centre * m_zoom - height() / 2;
    clampScroll();
    emit zoomChanged();
    emit viewportChanged();
    updateVisible();
    emit repaintRequested(-1);
}

void RichTextArea::setPadding(qreal padding)
{
    if (qFuzzyCompare(m_padding, padding)) return;
    m_padding = qMax<qreal>(0, padding);
    updateLayoutWidth();
    clampScroll();
    emit paddingChanged();
    updateVisible();
}

void RichTextArea::setPageGap(qreal gap)
{
    if (qFuzzyCompare(m_pageGap, gap)) return;
    m_pageGap = qMax<qreal>(0, gap);
    clampScroll();
    emit pageGapChanged();
    updateVisible();
}

void RichTextArea::setContentX(qreal x)
{
    if (qFuzzyCompare(m_contentX, x)) return;
    m_contentX = x;
    clampScroll();
    emit viewportChanged();
    updateVisible();
}

void RichTextArea::setContentY(qreal y)
{
    if (qFuzzyCompare(m_contentY, y)) return;
    m_contentY = y;
    clampScroll();
    emit viewportChanged();
    updateVisible();
}

qreal RichTextArea::contentWidth() const
{
    if (!paged()) return width();
    return m_layout->pageSize().width() * m_zoom + 2 * m_padding;
}

qreal RichTextArea::contentHeight() const
{
    if (!paged()) return m_layout->flowHeight() * m_zoom + 2 * m_padding;
    const int pages = qMax(1, m_layout->paginationComplete() ? pageCount() : estimatedPageCount());
    return 2 * m_padding + pages * m_layout->pageSize().height() * m_zoom + (pages - 1) * m_pageGap;
}

void RichTextArea::updateContentSize()
{
    const QSizeF size(contentWidth(), contentHeight());
    if (size == m_contentSize) return;
    m_contentSize = size;
    emit contentSizeChanged();
}

void RichTextArea::clampScroll()
{
    updateContentSize();
    m_contentX = qBound<qreal>(0, m_contentX, qMax<qreal>(0, contentWidth() - width()));
    m_contentY = qBound<qreal>(0, m_contentY, qMax<qreal>(0, contentHeight() - height()));
}

QPointF RichTextArea::pageOrigin(int page) const
{
    if (!paged()) return QPointF(m_padding - m_contentX, m_padding - m_contentY);
    const QSizeF size = m_layout->pageSize() * m_zoom;
    const qreal left = qMax(m_padding, (width() - size.width()) / 2);
    return QPointF(left - m_contentX, m_padding + page * (size.height() + m_pageGap) - m_contentY);
}

QRectF RichTextArea::pageRect(int page) const
{
    const QSizeF size = paged() ? m_layout->pageSize() * m_zoom
                                : QSizeF(m_layout->flowWidth() * m_zoom, m_layout->flowHeight() * m_zoom);
    return QRectF(pageOrigin(page), size);
}

int RichTextArea::pageAtY(qreal itemY) const
{
    if (!paged() || pageCount() == 0) return 0;
    const qreal stride = m_layout->pageSize().height() * m_zoom + m_pageGap;
    return qBound(0, int(std::floor((itemY + m_contentY - m_padding + m_pageGap / 2) / stride)), pageCount() - 1);
}

int RichTextArea::currentPage() const
{
    // AGENT-GUARD: property getters never trigger pagination (it emits
    // pagesChanged while a binding evaluates); unlaid positions fall back.
    // AGENT-NOTE: navigation scrolls and keeps the selection (OFF-05), so the
    // current page is the page navigated to while it is in view, else the
    // caret's page while the caret is in view, else the page at the centre.
    if (!m_document || pageCount() == 0) return 0;
    if (m_navigatedPage >= 0 && m_navigatedPage < pageCount() && pageVisible(m_navigatedPage))
        return m_navigatedPage;
    const QRectF caret = cursorRectangle();
    if (!caret.isNull() && caret.bottom() > 0 && caret.top() < height()) {
        const int page = m_layout->caretBox(m_focus.position).page;
        if (page >= 0) return page;
    }
    return pageAtY(height() / 2);
}

bool RichTextArea::pageVisible(int page) const
{
    const QRectF rect = pageRect(page);
    return rect.bottom() > 0 && rect.top() < height();
}

void RichTextArea::refreshViewState()
{
    emit cursorRectangleChanged();
    const int current = currentPage();
    if (current == m_lastCurrentPage) return;
    m_lastCurrentPage = current;
    emit currentPageChanged();
}

void RichTextArea::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() == oldGeometry.size()) return;
    updateLayoutWidth();
    clampScroll();
    updateVisible();
}

void RichTextArea::updateLayoutWidth()
{
    if (!paged()) m_layout->setFlowWidth(qMax<qreal>(24, (width() - 2 * m_padding) / m_zoom));
}

void RichTextArea::schedulePagination()
{
    if (!m_document || m_layout->paginationComplete()) return;
    // Pages up to the viewport are laid out now; the rest in 8 ms slices.
    const qreal stride = paged() ? m_layout->pageSize().height() * m_zoom + m_pageGap : 1;
    const int needed = paged() ? int((m_contentY + height()) / qMax<qreal>(1, stride)) + 1 : 0;
    m_layout->ensurePaginated(needed);
    if (!m_layout->paginationComplete()) m_pagination.start(0, this);
    updateVisible();
}

void RichTextArea::onPagesChanged(int firstChanged, int)
{
    clampScroll();
    if (m_layout->pageSize() != m_pageSize) {
        m_pageSize = m_layout->pageSize();
        emit pageSizeChanged();
    }
    emit pagesChanged();
    updateVisible();
    refreshViewState();
    emit repaintRequested(firstChanged);
}

void RichTextArea::onContentChanged()
{
    m_formatState->refresh();
    emit accessibleTextChanged();
    emit caretChanged();
    emit selectionChanged();
    emit repaintRequested(-1);
}

void RichTextArea::updateVisible()
{
    QVector<RichTextVisiblePages::Entry> entries;
    if (m_document && pageCount() > 0 && width() > 0 && height() > 0) {
        const QRectF viewport(0, 0, width(), height());
        if (!paged()) {
            const QRectF flow = pageRect(0);
            const QRectF tile = flow.adjusted(-m_padding, -m_padding, m_padding, m_padding).intersected(viewport);
            if (!tile.isEmpty()) {
                const QRectF source((tile.x() - flow.x()) / m_zoom, (tile.y() - flow.y()) / m_zoom,
                                    tile.width() / m_zoom, tile.height() / m_zoom);
                entries.append({0, m_layout->page(0).key, m_layout->page(0).revision, tile, source, flow});
            }
        } else {
            const QSizeF full = m_layout->pageSize();
            const bool wholePage = full.width() * full.height() * m_zoom * m_zoom <= 4.0e6;
            for (int page = pageAtY(0); page < pageCount(); ++page) {
                const QRectF rect = pageRect(page);
                if (rect.top() > viewport.bottom()) break;
                if (!rect.intersects(viewport)) continue;
                // Small pages are painted whole (scrolling only moves them);
                // large zooms paint just the visible slice.
                const QRectF tile = wholePage ? rect : rect.intersected(viewport);
                const QRectF source((tile.x() - rect.x()) / m_zoom, (tile.y() - rect.y()) / m_zoom,
                                    tile.width() / m_zoom, tile.height() / m_zoom);
                entries.append({page, m_layout->page(page).key, m_layout->page(page).revision, tile, source, rect});
            }
        }
    }
    m_visible->update(entries);
}

void RichTextArea::timerEvent(QTimerEvent *event)
{
    if (event->timerId() == m_pagination.timerId()) {
        if (!m_layout->paginateStep(8 * 1000 * 1000)) m_pagination.stop();
        return;
    }
    if (event->timerId() == m_blink.timerId()) {
        m_caretOn = !m_caretOn;
        emit caretBlinked();
        emit repaintRequested(currentPage());
        return;
    }
    if (event->timerId() == m_autoScroll.timerId()) {
        const qreal edge = 24;
        qreal dy = 0;
        if (m_dragPoint.y() < edge) dy = m_dragPoint.y() - edge;
        else if (m_dragPoint.y() > height() - edge) dy = m_dragPoint.y() - height() + edge;
        if (!m_selecting || qFuzzyIsNull(dy)) {
            m_autoScroll.stop();
            return;
        }
        setContentY(m_contentY + dy / 2);
        moveCaret(positionAt(m_dragPoint), true);
        return;
    }
    QQuickItem::timerEvent(event);
}

void RichTextArea::restartBlink()
{
    m_caretOn = true;
    emit caretBlinked();
    const int interval = QGuiApplication::styleHints()->cursorFlashTime() / 2;
    if (hasActiveFocus() && interval > 0) m_blink.start(interval, this);
    else m_blink.stop();
}

void RichTextArea::focusInEvent(QFocusEvent *event)
{
    QQuickItem::focusInEvent(event);
    restartBlink();
    emit repaintRequested(-1);
}

void RichTextArea::focusOutEvent(QFocusEvent *event)
{
    QQuickItem::focusOutEvent(event);
    if (!m_preedit.isEmpty()) commitPreedit();
    m_blink.stop();
    m_caretOn = false;
    emit caretBlinked();
    emit repaintRequested(-1);
}

QRectF RichTextArea::cursorRectangle() const
{
    if (!m_document || !m_layout->isLaidOut(m_focus.position)) return {};
    const CaretBox caret = m_layout->caretBox(m_focus.position);
    if (caret.page < 0) return {};
    const QPointF origin = pageOrigin(caret.page);
    return QRectF(origin + caret.rect.topLeft() * m_zoom, QSizeF(qMax<qreal>(1, m_caretWidth), caret.rect.height() * m_zoom));
}

void RichTextArea::ensureCaretVisible()
{
    const QRectF caret = cursorRectangle();
    if (caret.isNull()) return;
    const qreal margin = qMin<qreal>(32, height() / 4);
    if (caret.top() < margin) setContentY(m_contentY + caret.top() - margin);
    else if (caret.bottom() > height() - margin) setContentY(m_contentY + caret.bottom() - height() + margin);
    if (caret.left() < 0) setContentX(m_contentX + caret.left() - 16);
    else if (caret.right() > width()) setContentX(m_contentX + caret.right() - width() + 16);
}

Position RichTextArea::positionAt(const QPointF &itemPoint)
{
    if (!m_document || pageCount() == 0) return m_document ? m_document->startPosition() : Position{};
    const int page = paged() ? pageAtY(itemPoint.y()) : 0;
    const QPointF local = (itemPoint - pageOrigin(page)) / m_zoom;
    return m_layout->hitTest(page, local);
}

bool RichTextArea::goToPage(int page)
{
    if (!m_document || page < 0) return false;
    m_layout->ensurePaginated(page);
    if (page >= pageCount()) return false;
    m_navigatedPage = page;
    setContentY(m_contentY + pageOrigin(page).y() - m_padding);
    refreshViewState();
    return true;
}

bool RichTextArea::goToParagraph(const QString &paragraphId, int offset)
{
    if (!m_document || !m_document->paragraph(QUuid(paragraphId))) return false;
    const Position target = m_document->clampPosition({QUuid(paragraphId), offset});
    m_layout->ensurePaginatedThrough(target);
    setSelection({target, target});
    return true;
}

RichTextObjectRenderer *RichTextArea::objectRenderer() const { return m_renderer; }

void RichTextArea::setObjectRenderer(RichTextObjectRenderer *renderer)
{
    m_renderer = renderer ? renderer : assetProvider()
        ? static_cast<RichTextObjectRenderer *>(m_assetRenderer.get())
        : static_cast<RichTextObjectRenderer *>(m_defaultRenderer.get());
    emit repaintRequested(-1);
}

PaintContext RichTextArea::paintContext(bool decorations) const
{
    PaintContext context;
    context.ink = m_ink;
    context.link = m_linkColor;
    context.selectionBackground = m_selectionColor;
    context.selectionText = m_selectedTextColor;
    context.caretColor = m_caretColor.isValid() ? m_caretColor : m_ink;
    context.border = m_borderColor;
    context.objectFill = m_objectFill;
    context.objectText = m_objectText;
    context.marker = m_markerColor;
    context.caretWidth = qMax<qreal>(1, m_caretWidth) / m_zoom;
    context.objects = m_renderer;
    context.frameGuide = m_frameGuideColor;
    context.overset = m_oversetColor;
    context.insertion = m_insertionColor;
    context.deletion = m_deletionColor;
    context.comment = m_commentColor;
    context.guides = decorations && paged();
    if (decorations && m_document) {
        context.selection = selection();
        context.hasSelection = hasSelection();
        context.caret = m_focus.position;
        context.showCaret = hasActiveFocus() && m_caretOn && !hasSelection();
    }
    return context;
}

QStringList RichTextArea::missingFonts() const
{
    return m_document ? m_document->missingFontFamilies() : QStringList{};
}

} // namespace QindaTK
