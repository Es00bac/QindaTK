// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QObject>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// Installed-family/style discovery plus OpenType fvar and layout feature tags
// from the resolved face. Glyph coverage and per-script shaping remain outside
// this service and are reported unavailable.
class FontCatalog : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantList families READ families NOTIFY changed)
    Q_PROPERTY(QString fallbackFamily READ fallbackFamily NOTIFY changed)
public:
    explicit FontCatalog(QObject *parent = nullptr);
    QVariantList families() const { return m_families; }
    QString fallbackFamily() const { return m_fallbackFamily; }
    Q_INVOKABLE QVariantMap resolveFamily(const QString &requested) const;
    Q_INVOKABLE QVariantMap faceMetadata(const QString &family,
                                         const QString &style = {}) const;
    Q_INVOKABLE QVariantList search(const QString &query, int limit = 100) const;
signals:
    void changed();
private:
    void refresh();
    QVariantList m_families;
    QString m_fallbackFamily;
};

} // namespace QindaTK
