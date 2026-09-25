// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "rich_text_format_state.h"
#include "rich_text_layout.h"
#include "rich_text_visible_pages.h"
#include "asset_provider.h"
#include <QBasicTimer>
#include <QColor>
#include <QQuickItem>
#include <QtQml/qqmlregistration.h>
#include <memory>

namespace QindaTK {
class CommandRegistry;
class ClipboardAdapter;
class RichTextClipboard;
class RichTextObjectRenderer;
class RichTextImageRenderer;
class RichTextAssetRenderer;

// Interactive surface of RichTextEditor (continuous flow) and RichDocumentView
// (pages). It owns one RichTextLayout of the shared RichTextDocument, the
// selection (document-mapped anchors), keyboard/pointer/IME input, scrolling
// and zoom, and exposes the visible tiles that QML draws with RichTextTile.
// AGENT-CONTRACT (docs/rich-text.md#views): `text`, `cursorPosition`,
// `selectionStart` and `selectionEnd` use the accessible plain-text offsets
// (paragraphs joined by "\n", objects as U+FFFC) that QAccessibleQuickItem
// reads by property name; editing goes through RichTextDocument transactions.
class RichTextArea : public QQuickItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QindaTK::RichTextDocument *document READ document WRITE setDocument NOTIFY documentChanged)
    Q_PROPERTY(QindaTK::AssetProvider *assetProvider READ assetProvider WRITE setAssetProvider NOTIFY assetProviderChanged)
    Q_PROPERTY(bool paged READ paged WRITE setPaged NOTIFY pagedChanged)
    Q_PROPERTY(bool readOnly READ readOnly WRITE setReadOnly NOTIFY readOnlyChanged)
    Q_PROPERTY(qreal zoom READ zoom WRITE setZoom NOTIFY zoomChanged)
    Q_PROPERTY(qreal padding READ padding WRITE setPadding NOTIFY paddingChanged)
    Q_PROPERTY(qreal pageGap READ pageGap WRITE setPageGap NOTIFY pageGapChanged)
    Q_PROPERTY(qreal contentX READ contentX WRITE setContentX NOTIFY viewportChanged)
    Q_PROPERTY(qreal contentY READ contentY WRITE setContentY NOTIFY viewportChanged)
    Q_PROPERTY(qreal contentWidth READ contentWidth NOTIFY contentSizeChanged)
    Q_PROPERTY(qreal contentHeight READ contentHeight NOTIFY contentSizeChanged)
    Q_PROPERTY(int pageCount READ pageCount NOTIFY pagesChanged)
    Q_PROPERTY(int estimatedPageCount READ estimatedPageCount NOTIFY pagesChanged)
    Q_PROPERTY(bool paginationComplete READ paginationComplete NOTIFY pagesChanged)
    Q_PROPERTY(int currentPage READ currentPage NOTIFY currentPageChanged)
    Q_PROPERTY(QSizeF pageSize READ pageSize NOTIFY pageSizeChanged)
    Q_PROPERTY(QAbstractItemModel *visiblePages READ visiblePages CONSTANT)
    Q_PROPERTY(QindaTK::RichTextFormatState *formatState READ formatState CONSTANT)
    Q_PROPERTY(QString text READ accessibleText NOTIFY accessibleTextChanged)
    Q_PROPERTY(int cursorPosition READ cursorPosition WRITE setCursorPosition NOTIFY caretChanged)
    Q_PROPERTY(int selectionStart READ selectionStart NOTIFY selectionChanged)
    Q_PROPERTY(int selectionEnd READ selectionEnd NOTIFY selectionChanged)
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedText READ selectedText NOTIFY selectionChanged)
    Q_PROPERTY(QString preeditText READ preeditText NOTIFY preeditChanged)
    Q_PROPERTY(bool caretVisible READ caretVisible NOTIFY caretBlinked)
    Q_PROPERTY(QRectF cursorRectangle READ cursorRectangle NOTIFY cursorRectangleChanged)
    Q_PROPERTY(QString caretParagraphId READ caretParagraphId NOTIFY caretChanged)
    Q_PROPERTY(int caretOffset READ caretOffset NOTIFY caretChanged)
    Q_PROPERTY(QStringList missingFonts READ missingFonts NOTIFY documentChanged)
    // Publishing structure (WP-051): the paragraphs a style request targets,
    // the caret's frame and structural location, and story text no frame holds.
    Q_PROPERTY(QStringList selectedParagraphIds READ selectedParagraphIds NOTIFY selectionChanged)
    Q_PROPERTY(QString caretFrameId READ caretFrameId NOTIFY caretChanged)
    Q_PROPERTY(QString caretContext READ caretContext NOTIFY caretChanged)
    Q_PROPERTY(bool frameLayout READ frameLayout NOTIFY pagesChanged)
    Q_PROPERTY(int oversetCharacters READ oversetCharacters NOTIFY pagesChanged)
    Q_PROPERTY(QColor inkColor MEMBER m_ink NOTIFY colorsChanged)
    Q_PROPERTY(QColor pageColor MEMBER m_pageColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor linkColor MEMBER m_linkColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor selectionColor MEMBER m_selectionColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor selectedTextColor MEMBER m_selectedTextColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor caretColor MEMBER m_caretColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor borderColor MEMBER m_borderColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor objectFillColor MEMBER m_objectFill NOTIFY colorsChanged)
    Q_PROPERTY(QColor objectTextColor MEMBER m_objectText NOTIFY colorsChanged)
    Q_PROPERTY(QColor markerColor MEMBER m_markerColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor frameGuideColor MEMBER m_frameGuideColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor oversetColor MEMBER m_oversetColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor insertionColor MEMBER m_insertionColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor deletionColor MEMBER m_deletionColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor commentColor MEMBER m_commentColor NOTIFY colorsChanged)
    Q_PROPERTY(qreal caretWidth MEMBER m_caretWidth NOTIFY colorsChanged)
public:
    explicit RichTextArea(QQuickItem *parent = nullptr);
    ~RichTextArea() override;

    RichTextDocument *document() const { return m_document; }
    void setDocument(RichTextDocument *document);
    AssetProvider *assetProvider() const;
    void setAssetProvider(AssetProvider *provider);
    bool paged() const { return m_layout->mode() == RichTextLayout::Mode::Paged; }
    void setPaged(bool paged);
    bool readOnly() const { return m_readOnly; }
    void setReadOnly(bool readOnly);
    qreal zoom() const { return m_zoom; }
    void setZoom(qreal zoom);
    qreal padding() const { return m_padding; }
    void setPadding(qreal padding);
    qreal pageGap() const { return m_pageGap; }
    void setPageGap(qreal gap);
    qreal contentX() const { return m_contentX; }
    void setContentX(qreal x);
    qreal contentY() const { return m_contentY; }
    void setContentY(qreal y);
    qreal contentWidth() const;
    qreal contentHeight() const;
    int pageCount() const { return m_layout->pageCount(); }
    int estimatedPageCount() const { return m_layout->estimatedPageCount(); }
    bool paginationComplete() const { return m_layout->paginationComplete(); }
    int currentPage() const;
    QSizeF pageSize() const { return m_layout->pageSize(); }
    QAbstractItemModel *visiblePages() const { return m_visible; }
    RichTextFormatState *formatState() const { return m_formatState; }
    RichTextLayout *layout() const { return m_layout.get(); }

    QString accessibleText() const;
    int cursorPosition() const;
    void setCursorPosition(int position);
    int selectionStart() const;
    int selectionEnd() const;
    bool hasSelection() const { return !(m_anchor.position == m_focus.position); }
    QString selectedText() const;
    QString preeditText() const { return m_preedit; }
    bool caretVisible() const { return m_caretOn; }
    QRectF cursorRectangle() const;
    QString caretParagraphId() const { return m_focus.position.paragraph.toString(QUuid::WithoutBraces); }
    int caretOffset() const { return m_focus.position.offset; }
    QStringList missingFonts() const;
    QStringList selectedParagraphIds() const;
    QString caretFrameId() const;
    QString caretContext() const;
    bool frameLayout() const { return m_layout->frameMode(); }
    int oversetCharacters() const { return m_layout->oversetCharacters(); }

    RichText::Selection selection() const { return {m_anchor.position, m_focus.position}; }
    void setSelection(const RichText::Selection &selection, bool ensureVisible = true);
    RichText::PaintContext paintContext(bool decorations) const;
    RichTextObjectRenderer *objectRenderer() const;
    void setObjectRenderer(RichTextObjectRenderer *renderer); // host-owned; nullptr restores the default
    void setClipboardAdapter(ClipboardAdapter *adapter);      // host-owned, for tests and hosts
    QPointF pageOrigin(int page) const;                       // item coordinates of a page's top-left
    RichText::Position positionAt(const QPointF &itemPoint);

    // Commands (docs/rich-text.md#commands): invoked by keys, FormatBar,
    // menus and CommandRegistry handlers; each edit is one transaction.
    Q_INVOKABLE bool invokeCommand(const QString &commandId, const QVariantMap &arguments = {});
    Q_INVOKABLE QVariantMap commandState(const QString &commandId) const;
    Q_INVOKABLE QStringList commandIds() const;
    Q_INVOKABLE bool registerCommands(QindaTK::CommandRegistry *registry, const QString &scopeToken);
    Q_INVOKABLE bool insertText(const QString &text);
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE bool goToPage(int page);     // scrolls; the selection stays (OFF-05)
    Q_INVOKABLE bool goToParagraph(const QString &paragraphId, int offset = 0);
    Q_INVOKABLE bool moveSectionOfPage(int page, int targetSection);
    Q_INVOKABLE void ensureCaretVisible();
    Q_INVOKABLE int pageAtY(qreal itemY) const;
    Q_INVOKABLE QRectF pageRect(int page) const;              // item coordinates
    Q_INVOKABLE QString frameAt(qreal itemX, qreal itemY) const; // text frame under an item point
    // Shows a frame and puts the caret at its first character (or just the
    // frame's page when the story does not reach it).
    Q_INVOKABLE bool goToFrame(const QString &frameId);
    // WP-052: the selection as paragraph positions ({anchorParagraphId,
    // anchorOffset, focusParagraphId, focusOffset}) and review requests at it.
    Q_INVOKABLE QVariantMap selectionRange() const;
    Q_INVOKABLE QString addComment(const QString &body);
    Q_INVOKABLE QString insertEquation(const QString &source);
    Q_INVOKABLE QString insertReference(const QString &type, const QString &targetId);
    Q_INVOKABLE bool selectRange(const QString &startParagraphId, int startOffset,
                                 const QString &endParagraphId, int endOffset);
    // Review navigation (OFF-11): selects a review item's anchored range and
    // scrolls to it; the adjacent form steps from the selection's start in
    // document order (wrapping), optionally only "comment" or "change"
    // items, and returns the ID it went to ("" when there is none).
    Q_INVOKABLE bool goToReviewItem(const QString &itemId);
    Q_INVOKABLE QString goToAdjacentReviewItem(bool forward, const QString &kind = {});
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;

signals:
    void documentChanged();
    void assetProviderChanged();
    void readOnlyChanged();
    void pagedChanged();
    void zoomChanged();
    void paddingChanged();
    void pageGapChanged();
    void pageSizeChanged();
    void contentSizeChanged();                                // only when the size really changes
    void viewportChanged();
    void pagesChanged();
    void caretChanged();
    void currentPageChanged();
    void cursorRectangleChanged(); // caret moves and scrolling (item coordinates)
    void selectionChanged();
    void preeditChanged();
    void caretBlinked();
    void colorsChanged();
    void accessibleTextChanged();
    void repaintRequested(int firstPage);                     // -1: every tile
    void linkActivated(const QString &href);
    void linkRequested(); // Ctrl+K: a host or FormatBar edits the selection's link
    void editRejected(const QString &message);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void inputMethodEvent(QInputMethodEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void hoverMoveEvent(QHoverEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void timerEvent(QTimerEvent *event) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private:
    friend class RichTextFormatState;
    void updateLayoutWidth();
    void updateVisible();
    void clampScroll();
    void updateContentSize();
    void onPagesChanged(int firstChanged, int pageCount);
    void onContentChanged();
    void onHistoryNavigated(const RichText::Selection &selection);
    void selectionUpdated(bool ensureVisible);
    void restartBlink();
    void schedulePagination();
    void refreshViewState();
    bool pageVisible(int page) const;
    bool edit(const QString &label, const QString &mergeKey,
              const std::function<std::optional<RichText::Position>(RichTextDocument &)> &operation);
    bool formatSelection(const RichText::CharFormatPatch &patch, const QString &label);
    bool formatParagraphs(const RichText::ParagraphFormatPatch &patch, const QString &label);
    void moveCaret(const RichText::Position &target, bool extend, bool keepGoal = false);
    bool deleteBackward(bool word);
    bool deleteForward(bool word);
    bool newParagraph();
    bool tabKey(bool backwards);
    bool copySelection(bool cut);
    bool paste();
    void selectWordAt(const RichText::Position &position);
    void selectParagraphAt(const RichText::Position &position);
    QString hrefAt(const RichText::Position &position) const;
    void commitPreedit();
    void clearPreedit();
    int accessibleOffset(const RichText::Position &position) const;
    RichText::Position positionFromAccessible(int offset) const;
    void rebuildAccessibleIndex() const;

    QPointer<RichTextDocument> m_document;
    std::unique_ptr<RichTextLayout> m_layout;
    RichTextVisiblePages *m_visible = nullptr;
    RichTextFormatState *m_formatState = nullptr;
    std::unique_ptr<RichTextImageRenderer> m_defaultRenderer;
    std::unique_ptr<RichTextAssetRenderer> m_assetRenderer;
    RichTextObjectRenderer *m_renderer = nullptr;
    std::unique_ptr<RichTextClipboard> m_clipboard;
    RichText::PositionAnchor m_anchor;
    RichText::PositionAnchor m_focus;
    std::optional<RichText::CharFormatPatch> m_pendingFormat;
    qreal m_goalX = -1;
    QString m_preedit;
    int m_preeditCursor = -1;
    bool m_preeditReplacedSelection = false;
    qreal m_zoom = 1.0;
    qreal m_padding = 16;
    qreal m_pageGap = 16;
    qreal m_contentX = 0;
    qreal m_contentY = 0;
    int m_navigatedPage = -1;     // goToPage target; cleared by a selection change
    int m_lastCurrentPage = -1;
    QSizeF m_contentSize;
    QSizeF m_pageSize;
    bool m_readOnly = false;
    bool m_caretOn = true;
    bool m_selecting = false;
    bool m_commandInFlight = false;
    int m_clickCount = 0;
    QPointF m_lastPress;
    qint64 m_lastPressTime = 0;
    QPointF m_dragPoint;
    QBasicTimer m_blink;
    QBasicTimer m_autoScroll;
    QBasicTimer m_pagination;
    mutable QVector<int> m_accessibleStarts;   // paragraph start offsets, by flat index
    mutable quint64 m_accessibleRevision = ~0ull;
    QColor m_ink, m_pageColor, m_linkColor, m_selectionColor, m_selectedTextColor, m_caretColor;
    QColor m_borderColor, m_objectFill, m_objectText, m_markerColor, m_frameGuideColor, m_oversetColor;
    QColor m_insertionColor, m_deletionColor, m_commentColor;
    qreal m_caretWidth = 1.0;
};

} // namespace QindaTK
