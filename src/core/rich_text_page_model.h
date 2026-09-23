// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "rich_text_area.h"
#include <QAbstractListModel>
#include <QPointer>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// All pages of an area for PageNavigator: one row per page, data computed on
// demand; delegates are created only for the rows the navigator shows.
class RichTextPageModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QindaTK::RichTextArea *area READ area WRITE setArea NOTIFY areaChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int generation READ generation NOTIFY generationChanged)
public:
    enum Role { PageIndexRole = Qt::UserRole + 1, PageKeyRole, RevisionRole, LabelRole,
                SectionRole, OrdinalRole, HeadingRole, CurrentRole };
    explicit RichTextPageModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}
    RichTextArea *area() const { return m_area; }
    void setArea(RichTextArea *area);
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const { return m_count; }
    int generation() const { return m_generation; }
    // Headings (level, text, paragraphId, pageIndex), at most `limit`.
    Q_INVOKABLE QVariantList outline(int limit = 2000) const;
    // Index of the page's section (page-break group) in document order, and
    // the number of sections; RichTextArea::moveSectionOfPage takes indexes.
    Q_INVOKABLE int sectionIndex(int page) const;
    Q_INVOKABLE int sectionCount() const;
signals:
    void areaChanged();
    void countChanged();
    void generationChanged();
private:
    void onPagesChanged(int firstChanged, int pageCount);
    QPointer<RichTextArea> m_area;
    int m_count = 0;
    int m_current = -1;
    int m_generation = 0;
};

} // namespace QindaTK
