// SPDX-License-Identifier: LGPL-3.0-or-later
#include "icon.h"
#include "icon_data.h"
#include "svg_path.h"

#include <QtTest>

using namespace QindaTK;

// Guards the generated icon table: every built-in path must parse, every
// alias must resolve, and the registry must answer for both spellings.
class TstIcon final : public QObject {
    Q_OBJECT
private slots:
    void everyBuiltinPathParses()
    {
        int icons = 0;
        for (const IconData::BuiltinIcon &icon : IconData::builtinIcons()) {
            ++icons;
            QVERIFY2(!icon.paths.empty(), icon.name);
            for (const char *d : icon.paths) {
                bool ok = false;
                const QPainterPath path = svgPathToPainterPath(QString::fromLatin1(d), &ok);
                QVERIFY2(ok, qPrintable(QStringLiteral("%1: %2").arg(QString::fromLatin1(icon.name), QString::fromLatin1(d))));
                QVERIFY2(!path.isEmpty(), icon.name);
                const QRectF bounds = path.boundingRect();
                QVERIFY2(bounds.left() >= -1 && bounds.top() >= -1 && bounds.right() <= 25 && bounds.bottom() <= 25,
                         qPrintable(QStringLiteral("%1 out of the 24x24 box").arg(QString::fromLatin1(icon.name))));
            }
        }
        QVERIFY(icons > 250);
    }
    void aliasesResolve()
    {
        IconRegistry *registry = IconRegistry::instance();
        for (const IconData::IconAlias &alias : IconData::iconAliases()) {
            QCOMPARE(registry->resolve(QString::fromLatin1(alias.name)), QString::fromLatin1(alias.target));
            QVERIFY(registry->has(QString::fromLatin1(alias.target)));
        }
        QVERIFY(!registry->has(QStringLiteral("definitely-not-an-icon")));
        QVERIFY(registry->paths(QStringLiteral("layers")) != nullptr);
        QCOMPARE(registry->paths(QStringLiteral("layers"))->size(), 3);
    }
    void imageEditingIconsAreBuiltIn()
    {
        // QindaTK is shared by several desktop applications. These are
        // ordinary Lucide glyphs used by image-editing palettes; keeping
        // them in the generated registry lets consumers use Tk.Icon rather
        // than carrying a second image provider for four missing shapes.
        IconRegistry *registry = IconRegistry::instance();
        for (const QString &name : {QStringLiteral("bandage"),
                                    QStringLiteral("circle-off"),
                                    QStringLiteral("image-off"),
                                    QStringLiteral("wind")}) {
            QVERIFY2(registry->has(name), qPrintable(name));
            const QVector<QPainterPath> *paths = registry->paths(name);
            QVERIFY2(paths != nullptr && !paths->isEmpty(), qPrintable(name));
        }
    }
    void customIcons()
    {
        IconRegistry *registry = IconRegistry::instance();
        QSignalSpy spy(registry, &IconRegistry::registryChanged);
        registry->registerIcon(QStringLiteral("test-square"), {QStringLiteral("M4 4h16v16H4z")});
        QCOMPARE(spy.count(), 1);
        QVERIFY(registry->has(QStringLiteral("test-square")));
        QCOMPARE(registry->paths(QStringLiteral("test-square"))->first().boundingRect(), QRectF(4, 4, 16, 16));
        QVERIFY(registry->names().contains(QStringLiteral("test-square")));
    }
};

QTEST_GUILESS_MAIN(TstIcon)
#include "tst_icon.moc"
