// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "rich_text_area.h"
#include <QPointer>
#include <QQuickPaintedItem>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// Paints `sourceRect` (page coordinates) of one laid-out page of an area,
// scaled to the item's size. RichDocumentView creates one per visible page;
// PageNavigator uses it for thumbnails (decorations off). Painting reads the
// area's layout, so no page image is cached beyond the item's own texture.
class RichTextTile : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QindaTK::RichTextArea *area READ area WRITE setArea NOTIFY areaChanged)
    Q_PROPERTY(int pageIndex READ pageIndex WRITE setPageIndex NOTIFY pageIndexChanged)
    Q_PROPERTY(QRectF sourceRect READ sourceRect WRITE setSourceRect NOTIFY sourceRectChanged)
    Q_PROPERTY(bool decorations READ decorations WRITE setDecorations NOTIFY decorationsChanged)
    Q_PROPERTY(bool paper READ paper WRITE setPaper NOTIFY decorationsChanged)
    Q_PROPERTY(int paintCount READ paintCount NOTIFY painted)
public:
    explicit RichTextTile(QQuickItem *parent = nullptr);
    RichTextArea *area() const { return m_area; }
    void setArea(RichTextArea *area);
    int pageIndex() const { return m_page; }
    void setPageIndex(int page);
    QRectF sourceRect() const { return m_source; }
    void setSourceRect(const QRectF &rect);
    bool decorations() const { return m_decorations; }
    void setDecorations(bool on);
    bool paper() const { return m_paper; }
    void setPaper(bool on);
    int paintCount() const { return m_paintCount; }
    void paint(QPainter *painter) override;
signals:
    void areaChanged();
    void pageIndexChanged();
    void sourceRectChanged();
    void decorationsChanged();
    void painted();
private:
    void onRepaintRequested(int firstPage);
    QPointer<RichTextArea> m_area;
    int m_page = -1;
    QRectF m_source;
    bool m_decorations = true;
    bool m_paper = true;
    int m_paintCount = 0;
};

} // namespace QindaTK
