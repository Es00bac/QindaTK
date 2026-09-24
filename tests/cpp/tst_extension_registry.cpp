// SPDX-License-Identifier: LGPL-3.0-or-later
#include "extension_registry.h"
#include "capability_registry.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace QindaTK;

class TstExtensionRegistry final : public QObject {
    Q_OBJECT
private:
    static QString writeDescriptor(const QString &directory, const QString &name,
                                   QJsonObject descriptor)
    {
        const QString path = directory + QLatin1Char('/') + name + QStringLiteral(".json");
        descriptor.insert(QStringLiteral("library"), QString::fromUtf8(QINDATK_EXTENSION_LIBRARY));
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) return {};
        file.write(QJsonDocument(descriptor).toJson(QJsonDocument::Compact));
        return path;
    }
    static QJsonObject base()
    {
        return {{QStringLiteral("schemaVersion"), 1},
                {QStringLiteral("abiVersion"), 1},
                {QStringLiteral("id"), QStringLiteral("org.qindatk.fixture.counter")},
                {QStringLiteral("version"), QStringLiteral("1.2.0")},
                {QStringLiteral("operations"), QJsonArray{QStringLiteral("increment")}},
                {QStringLiteral("providesCapabilities"), QJsonArray{
                    QJsonObject{{QStringLiteral("featureId"), QStringLiteral("fixture.counter")},
                                {QStringLiteral("operationId"), QStringLiteral("increment")}}}}};
    }
private slots:
    void loadInvokeUnloadAndRecover()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = writeDescriptor(directory.path(), QStringLiteral("counter"), base());
        QVERIFY(!path.isEmpty());
        CapabilityRegistry capabilities;
        ExtensionRegistry registry;
        registry.setCapabilities(&capabilities);
        QVERIFY(registry.registerDescriptor(path));
        QVERIFY(!capabilities.isAvailable(QStringLiteral("fixture.counter"), QStringLiteral("increment")));
        QVERIFY(registry.load(QStringLiteral("org.qindatk.fixture.counter")));
        QVERIFY(capabilities.isAvailable(QStringLiteral("fixture.counter"), QStringLiteral("increment")));
        QCOMPARE(registry.invoke(QStringLiteral("org.qindatk.fixture.counter"), QStringLiteral("increment"),
                                 {{QStringLiteral("by"), 3}}).value(QStringLiteral("count")).toInt(), 3);
        QVERIFY(!registry.invoke(QStringLiteral("org.qindatk.fixture.counter"), QStringLiteral("undeclared"))
                     .value(QStringLiteral("ok")).toBool());
        QVERIFY(registry.unload(QStringLiteral("org.qindatk.fixture.counter")));
        QVERIFY(!capabilities.isAvailable(QStringLiteral("fixture.counter"), QStringLiteral("increment")));
        const QVariantList snapshot = registry.saveState();
        ExtensionRegistry restored;
        QVERIFY(restored.restoreState(snapshot));
        const auto unresolved = restored.extension(QStringLiteral("org.qindatk.fixture.counter"));
        QVERIFY(!unresolved.value(QStringLiteral("installed")).toBool());
        QCOMPARE(unresolved.value(QStringLiteral("state")).toMap().value(QStringLiteral("count")).toInt(), 3);
        QVERIFY(restored.registerDescriptor(path));
        QVERIFY(restored.load(QStringLiteral("org.qindatk.fixture.counter")));
        QCOMPARE(restored.invoke(QStringLiteral("org.qindatk.fixture.counter"), QStringLiteral("increment"))
                     .value(QStringLiteral("count")).toInt(), 4);
        QVERIFY(restored.unload(QStringLiteral("org.qindatk.fixture.counter")));
    }
    void diagnosticsForIncompatibleAndMissingDependency()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        ExtensionRegistry registry;
        auto descriptor = base();
        descriptor.insert(QStringLiteral("abiVersion"), 2);
        const QString bad = writeDescriptor(directory.path(), QStringLiteral("bad"), descriptor);
        QVERIFY(!registry.registerDescriptor(bad));
        QVERIFY(registry.extension(QStringLiteral("org.qindatk.fixture.counter"))
                    .value(QStringLiteral("diagnostic")).toString().contains(QStringLiteral("ABI")));
        descriptor = base();
        descriptor.insert(QStringLiteral("dependencies"), QJsonArray{
            QJsonObject{{QStringLiteral("id"), QStringLiteral("org.qindatk.fixture.missing")},
                        {QStringLiteral("minVersion"), QStringLiteral("1.0")}}});
        const QString dependent = writeDescriptor(directory.path(), QStringLiteral("dependent"), descriptor);
        QVERIFY(registry.registerDescriptor(dependent));
        QVERIFY(!registry.load(QStringLiteral("org.qindatk.fixture.counter")));
        QVERIFY(registry.extension(QStringLiteral("org.qindatk.fixture.counter"))
                    .value(QStringLiteral("diagnostic")).toString().contains(QStringLiteral("not loaded")));
    }
};

QTEST_GUILESS_MAIN(TstExtensionRegistry)
#include "tst_extension_registry.moc"
