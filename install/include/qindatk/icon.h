// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QColor>
#include <QHash>
#include <QPainterPath>
#include <QQuickPaintedItem>
#include <QStringList>
#include <QUrl>
#include <QVector>
#include <QtQml/qqmlregistration.h>

class QQmlEngine;
class QJSEngine;
class QSvgRenderer;

namespace QindaTK {

// AGENT-CONTRACT: the icon registry. Built-in names are Lucide's (see
// tools/scripts/gen_icons.py); `registerIcon` adds an application's own
// stroked icons as 24x24 SVG path strings. Lookups resolve deprecated
// Lucide aliases so both "alert-triangle" and "triangle-alert" work.
class IconRegistry : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(Icons)
    QML_SINGLETON

public:
    [[nodiscard]] static IconRegistry *instance();
    [[nodiscard]] static IconRegistry *create(QQmlEngine *engine, QJSEngine *jsEngine);

    Q_INVOKABLE [[nodiscard]] bool has(const QString &name) const;
    Q_INVOKABLE [[nodiscard]] QStringList names() const;
    Q_INVOKABLE [[nodiscard]] QString resolve(const QString &name) const;
    Q_INVOKABLE void registerIcon(const QString &name, const QStringList &paths);

    // Parsed paths, cached; null for an unknown name.
    [[nodiscard]] const QVector<QPainterPath> *paths(const QString &name);

signals:
    void registryChanged();

private:
    explicit IconRegistry(QObject *parent = nullptr);

    QHash<QString, QStringList> m_custom;
    QHash<QString, QVector<QPainterPath>> m_cache;
};

// A stroked vector icon painted in `color`: `name` from the registry, or
// `source` for an SVG file (`tint` recolours it). Its implicit size is
// `size`; the stroke scales with it (Lucide's 2/24 ratio at strokeWidth 2).
class Icon : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged)
    Q_PROPERTY(QUrl source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(qreal size READ size WRITE setSize NOTIFY sizeChanged)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY colorChanged)
    Q_PROPERTY(qreal strokeWidth READ strokeWidth WRITE setStrokeWidth NOTIFY strokeWidthChanged)
    Q_PROPERTY(bool tint READ tint WRITE setTint NOTIFY tintChanged)
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)

public:
    explicit Icon(QQuickItem *parent = nullptr);
    ~Icon() override;

    [[nodiscard]] QString name() const { return m_name; }
    void setName(const QString &name);
    [[nodiscard]] QUrl source() const { return m_source; }
    void setSource(const QUrl &source);
    [[nodiscard]] qreal size() const { return m_size; }
    void setSize(qreal size);
    [[nodiscard]] QColor color() const { return m_color; }
    void setColor(const QColor &color);
    [[nodiscard]] qreal strokeWidth() const { return m_strokeWidth; }
    void setStrokeWidth(qreal width);
    [[nodiscard]] bool tint() const { return m_tint; }
    void setTint(bool tint);
    [[nodiscard]] bool available() const { return m_available; }

    void paint(QPainter *painter) override;

signals:
    void nameChanged();
    void sourceChanged();
    void sizeChanged();
    void colorChanged();
    void strokeWidthChanged();
    void tintChanged();
    void availableChanged();

private:
    void refreshAvailability();

    QString m_name;
    QUrl m_source;
    qreal m_size = 16;
    QColor m_color = QColor(0xf3, 0xf7, 0xfb);
    qreal m_strokeWidth = 2;
    bool m_tint = true;
    bool m_available = false;
    QSvgRenderer *m_renderer = nullptr;
};

} // namespace QindaTK
