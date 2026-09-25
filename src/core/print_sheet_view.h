// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "print_adapter.h"
#include <QImage>
#include <QPointer>
#include <QQuickPaintedItem>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// Shows one output sheet of a PrintAdapter: the page placed on the selected
// paper with the ticket's scale, bleed, crop marks and colour, as the job will
// print it (D-130). Images come from the adapter's bounded preview cache and
// are fetched in updatePolish, on the GUI thread, never while painting.
class PrintSheetView : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QindaTK::PrintAdapter *adapter READ adapter WRITE setAdapter NOTIFY adapterChanged)
    Q_PROPERTY(int pageIndex READ pageIndex WRITE setPageIndex NOTIFY pageIndexChanged)
    Q_PROPERTY(QSizeF sheetSize READ sheetSize NOTIFY sheetChanged)          // points
    Q_PROPERTY(QRectF sheetRect READ sheetRect NOTIFY sheetChanged)          // item coordinates
    Q_PROPERTY(bool selected READ selected NOTIFY sheetChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY sheetChanged)
    Q_PROPERTY(double zoom READ zoom NOTIFY sheetChanged)                     // 1: 96 px per inch
public:
    explicit PrintSheetView(QQuickItem *parent = nullptr);
    PrintAdapter *adapter() const { return m_adapter; }
    void setAdapter(PrintAdapter *adapter);
    int pageIndex() const { return m_page; }
    void setPageIndex(int page);
    QSizeF sheetSize() const { return m_sheetSize; }
    QRectF sheetRect() const;
    bool selected() const { return m_selected; }
    bool ready() const { return !m_image.isNull(); }
    double zoom() const { return m_zoom; }
    void paint(QPainter *painter) override;

signals:
    void adapterChanged();
    void pageIndexChanged();
    void sheetChanged();

protected:
    void updatePolish() override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private:
    QPointer<PrintAdapter> m_adapter;
    QMetaObject::Connection m_invalidated;
    int m_page = 0;
    QImage m_image;
    QSizeF m_sheetSize;
    bool m_selected = false;
    double m_zoom = 0;
};

} // namespace QindaTK
