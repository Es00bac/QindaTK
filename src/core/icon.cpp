// SPDX-License-Identifier: LGPL-3.0-or-later
#include "icon.h"

#include "icon_data.h"
#include "svg_path.h"

#include <QFile>
#include <QImage>
#include <QJSEngine>
#include <QPainter>
#include <QPen>
#include <QQmlEngine>
#include <QSvgRenderer>

#include <algorithm>

namespace QindaTK {

namespace {

const IconData::BuiltinIcon *findBuiltin(const QString &name)
{
    const QByteArray latin = name.toLatin1();
    for (const IconData::BuiltinIcon &icon : IconData::builtinIcons()) {
        if (latin == icon.name) {
            return &icon;
        }
    }
    return nullptr;
}

} // namespace

// ---- IconRegistry --------------------------------------------------------

IconRegistry::IconRegistry(QObject *parent)
    : QObject(parent)
{
}

IconRegistry *IconRegistry::instance()
{
    static IconRegistry *registry = new IconRegistry();
    return registry;
}

IconRegistry *IconRegistry::create(QQmlEngine *, QJSEngine *)
{
    IconRegistry *registry = instance();
    QJSEngine::setObjectOwnership(registry, QJSEngine::CppOwnership);
    return registry;
}

QString IconRegistry::resolve(const QString &name) const
{
    if (m_custom.contains(name) || findBuiltin(name) != nullptr) {
        return name;
    }
    const QByteArray latin = name.toLatin1();
    for (const IconData::IconAlias &alias : IconData::iconAliases()) {
        if (latin == alias.name) {
            return QString::fromLatin1(alias.target);
        }
    }
    return {};
}

bool IconRegistry::has(const QString &name) const
{
    return !resolve(name).isEmpty();
}

QStringList IconRegistry::names() const
{
    QStringList names;
    for (const IconData::BuiltinIcon &icon : IconData::builtinIcons()) {
        names.append(QString::fromLatin1(icon.name));
    }
    names.append(m_custom.keys());
    std::sort(names.begin(), names.end());
    names.removeDuplicates();
    return names;
}

void IconRegistry::registerIcon(const QString &name, const QStringList &paths)
{
    m_custom.insert(name, paths);
    m_cache.remove(name);
    emit registryChanged();
}

const QVector<QPainterPath> *IconRegistry::paths(const QString &name)
{
    const QString resolved = resolve(name);
    if (resolved.isEmpty()) {
        return nullptr;
    }
    auto cached = m_cache.constFind(resolved);
    if (cached != m_cache.cend()) {
        return &cached.value();
    }
    QVector<QPainterPath> parsed;
    if (const auto custom = m_custom.constFind(resolved); custom != m_custom.cend()) {
        for (const QString &d : custom.value()) {
            parsed.append(svgPathToPainterPath(d));
        }
    } else if (const IconData::BuiltinIcon *icon = findBuiltin(resolved)) {
        for (const char *d : icon->paths) {
            parsed.append(svgPathToPainterPath(QString::fromLatin1(d)));
        }
    }
    return &m_cache.insert(resolved, parsed).value();
}

// ---- Icon ----------------------------------------------------------------

Icon::Icon(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
    setImplicitSize(m_size, m_size);
    connect(IconRegistry::instance(), &IconRegistry::registryChanged, this, &Icon::refreshAvailability);
}

Icon::~Icon()
{
    delete m_renderer;
}

void Icon::setName(const QString &name)
{
    if (name == m_name) {
        return;
    }
    m_name = name;
    emit nameChanged();
    refreshAvailability();
}

void Icon::setSource(const QUrl &source)
{
    if (source == m_source) {
        return;
    }
    m_source = source;
    delete m_renderer;
    m_renderer = nullptr;
    if (source.isValid() && !source.isEmpty()) {
        QString path = source.isLocalFile() ? source.toLocalFile() : source.toString();
        if (path.startsWith(QLatin1String("qrc:"))) {
            path = path.mid(3);
        }
        QFile file(path);
        if (file.open(QIODevice::ReadOnly)) {
            m_renderer = new QSvgRenderer(file.readAll());
            if (!m_renderer->isValid()) {
                delete m_renderer;
                m_renderer = nullptr;
            }
        }
    }
    emit sourceChanged();
    refreshAvailability();
}

void Icon::setSize(qreal size)
{
    if (qFuzzyCompare(size, m_size)) {
        return;
    }
    m_size = size;
    setImplicitSize(size, size);
    emit sizeChanged();
    update();
}

void Icon::setColor(const QColor &color)
{
    if (color == m_color) {
        return;
    }
    m_color = color;
    emit colorChanged();
    update();
}

void Icon::setStrokeWidth(qreal width)
{
    if (qFuzzyCompare(width, m_strokeWidth)) {
        return;
    }
    m_strokeWidth = width;
    emit strokeWidthChanged();
    update();
}

void Icon::setTint(bool tint)
{
    if (tint == m_tint) {
        return;
    }
    m_tint = tint;
    emit tintChanged();
    update();
}

void Icon::refreshAvailability()
{
    const bool available = m_renderer != nullptr || IconRegistry::instance()->has(m_name);
    if (available != m_available) {
        m_available = available;
        emit availableChanged();
    }
    update();
}

void Icon::paint(QPainter *painter)
{
    const qreal w = width();
    const qreal h = height();
    if (w <= 0 || h <= 0) {
        return;
    }
    painter->setRenderHint(QPainter::Antialiasing, true);
    if (m_renderer != nullptr) {
        const QRectF target(0, 0, w, h);
        if (!m_tint) {
            m_renderer->render(painter, target);
            return;
        }
        const qreal dpr = painter->device()->devicePixelRatio();
        QImage image(QSize(qRound(w * dpr), qRound(h * dpr)), QImage::Format_ARGB32_Premultiplied);
        image.setDevicePixelRatio(dpr);
        image.fill(Qt::transparent);
        QPainter offscreen(&image);
        offscreen.setRenderHint(QPainter::Antialiasing, true);
        m_renderer->render(&offscreen, target);
        offscreen.setCompositionMode(QPainter::CompositionMode_SourceIn);
        offscreen.fillRect(target, m_color);
        offscreen.end();
        painter->drawImage(target, image);
        return;
    }
    const QVector<QPainterPath> *paths = IconRegistry::instance()->paths(m_name);
    if (paths == nullptr) {
        return;
    }
    const qreal scale = std::min(w, h) / 24.0;
    painter->translate((w - 24 * scale) / 2, (h - 24 * scale) / 2);
    painter->scale(scale, scale);
    QPen pen(m_color, m_strokeWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);
    for (const QPainterPath &path : *paths) {
        painter->drawPath(path);
    }
}

} // namespace QindaTK
