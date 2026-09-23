// SPDX-License-Identifier: LGPL-3.0-or-later
#include "capability_registry.h"

#include <QtTest>

using namespace QindaTK;

namespace {
CapabilityDescriptor capability(QString operation, QString backend,
                                bool available = true, int priority = 0,
                                QString reason = {}, QString version = QStringLiteral("1.0"))
{
    return {QStringLiteral("image.export"), std::move(operation), std::move(backend),
            std::move(version), available, std::move(reason), priority};
}
}

class TstCapabilityRegistry final : public QObject {
    Q_OBJECT
private slots:
    void availabilityIsPerOperationAndCarriesReason()
    {
        CapabilityRegistry registry;
        QVERIFY(registry.publish(capability(QStringLiteral("png"), QStringLiteral("native"))));
        QVERIFY(registry.publish(capability(QStringLiteral("svg"), QStringLiteral("vector"),
                                            false, 10, QStringLiteral("Vector engine is not installed"))));
        QVERIFY(registry.isAvailable(QStringLiteral("image.export"), QStringLiteral("png")));
        QVERIFY(!registry.isAvailable(QStringLiteral("image.export"), QStringLiteral("svg")));
        QCOMPARE(registry.disabledReason(QStringLiteral("image.export"), QStringLiteral("svg")),
                 QStringLiteral("Vector engine is not installed"));
        const auto missing = registry.resolve(QStringLiteral("image.export"), QStringLiteral("pdf"));
        QVERIFY(!missing.available);
        QVERIFY(missing.reason.contains(QStringLiteral("No provider")));
        QCOMPARE(registry.operations(QStringLiteral("image.export")),
                 (QStringList{QStringLiteral("png"), QStringLiteral("svg")}));
        QCOMPARE(registry.query(QStringLiteral("image.export"), QStringLiteral("png"))
                     .value(QStringLiteral("backendVersion")).toString(), QStringLiteral("1.0"));
    }

    void resolutionPrefersAvailableThenPriorityThenStableBackendId()
    {
        CapabilityRegistry registry;
        QVERIFY(registry.publish(capability(QStringLiteral("png"), QStringLiteral("z-disabled"),
                                            false, 100, QStringLiteral("offline"))));
        QVERIFY(registry.publish(capability(QStringLiteral("png"), QStringLiteral("b-backend"),
                                            true, 1, {}, QStringLiteral("2.1"))));
        QVERIFY(registry.publish(capability(QStringLiteral("png"), QStringLiteral("a-backend"),
                                            true, 1, {}, QStringLiteral("2.0"))));
        QCOMPARE(registry.resolve(QStringLiteral("image.export"), QStringLiteral("png")).backendId,
                 QStringLiteral("a-backend"));
        QVERIFY(registry.publish(capability(QStringLiteral("png"), QStringLiteral("z-backend"),
                                            true, 2)));
        const auto preferred = registry.resolve(QStringLiteral("image.export"), QStringLiteral("png"));
        QCOMPARE(preferred.backendId, QStringLiteral("z-backend"));
        QCOMPARE(preferred.backendVersion, QStringLiteral("1.0"));
    }

    void publishReplaceAndRemoveNotifyReactiveBinding()
    {
        CapabilityRegistry registry;
        CapabilityBinding binding;
        binding.setFeatureId(QStringLiteral("image.export"));
        binding.setOperationId(QStringLiteral("png"));
        binding.setRegistry(&registry);
        QVERIFY(!binding.available());
        QSignalSpy changed(&binding, &CapabilityBinding::changed);
        QSignalSpy generation(&registry, &CapabilityRegistry::generationChanged);
        QVERIFY(registry.publish(capability(QStringLiteral("png"), QStringLiteral("optional"),
                                            false, 0, QStringLiteral("Starting"))));
        QVERIFY(!binding.available());
        QCOMPARE(binding.reason(), QStringLiteral("Starting"));
        QCOMPARE(changed.count(), 1);
        QVERIFY(registry.publish(capability(QStringLiteral("png"), QStringLiteral("optional"),
                                            true, 0, {}, QStringLiteral("3.2"))));
        QVERIFY(binding.available());
        QCOMPARE(binding.reason(), QString{});
        QCOMPARE(binding.backendId(), QStringLiteral("optional"));
        QCOMPARE(binding.backendVersion(), QStringLiteral("3.2"));
        QCOMPARE(changed.count(), 2);
        QCOMPARE(generation.count(), 2);

        QCOMPARE(registry.removeBackend(QStringLiteral("optional")), 1);
        QVERIFY(!binding.available());
        QVERIFY(binding.reason().contains(QStringLiteral("No provider")));
        QCOMPARE(changed.count(), 3);
        QCOMPARE(registry.removeBackend(QStringLiteral("absent")), 0);
    }

    void removingWinnerFallsBackAndRegistryDestructionIsSafe()
    {
        auto registry = std::make_unique<CapabilityRegistry>();
        CapabilityBinding binding;
        binding.setFeatureId(QStringLiteral("image.export"));
        binding.setOperationId(QStringLiteral("png"));
        binding.setRegistry(registry.get());
        QSignalSpy changed(&binding, &CapabilityBinding::changed);

        QVERIFY(registry->publish(capability(QStringLiteral("png"), QStringLiteral("fallback"),
                                             true, 1, {}, QStringLiteral("1.4"))));
        QVERIFY(registry->publish(capability(QStringLiteral("png"), QStringLiteral("preferred"),
                                             true, 9, {}, QStringLiteral("2.7"))));
        QCOMPARE(binding.backendId(), QStringLiteral("preferred"));
        QCOMPARE(binding.backendVersion(), QStringLiteral("2.7"));
        QCOMPARE(registry->removeBackend(QStringLiteral("preferred")), 1);
        QVERIFY(binding.available());
        QCOMPARE(binding.backendId(), QStringLiteral("fallback"));
        QCOMPARE(binding.backendVersion(), QStringLiteral("1.4"));
        QCOMPARE(changed.count(), 3);

        registry.reset();
        QVERIFY(!binding.registry());
        QVERIFY(!binding.available());
        QVERIFY(binding.reason().contains(QStringLiteral("No provider")));
        QCOMPARE(changed.count(), 4);
    }

    void invalidManifestEntriesAreRejectedWithoutGenerationChange()
    {
        CapabilityRegistry registry;
        QSignalSpy generation(&registry, &CapabilityRegistry::generationChanged);
        QVERIFY(!registry.publish({{}, QStringLiteral("png"), QStringLiteral("backend"),
                                   QStringLiteral("1"), true, {}, 0}));
        QVERIFY(!registry.publish({QStringLiteral("feature"), {}, QStringLiteral("backend"),
                                   QStringLiteral("1"), true, {}, 0}));
        QVERIFY(!registry.publish({QStringLiteral("feature"), QStringLiteral("op"), {},
                                   QStringLiteral("1"), true, {}, 0}));
        QVERIFY(!registry.publish({QStringLiteral("feature"), QStringLiteral("op"),
                                   QStringLiteral("backend"), {}, true, {}, 0}));
        QVERIFY(!registry.publish({QStringLiteral("feature"), QStringLiteral("op"),
                                   QStringLiteral("backend"), QStringLiteral("1"), false, {}, 0}));
        QCOMPARE(generation.count(), 0);
        QCOMPARE(registry.generation(), QStringLiteral("0"));
    }

    void identicalPublishIsIdempotentAndMetadataDoesNotInstantiateProviders()
    {
        // Registry entries are value metadata; publishing has no provider factory
        // callback to invoke or optional engine object to construct.
        CapabilityRegistry registry;
        int providerCreations = 0;
        const auto entry = capability(QStringLiteral("png"), QStringLiteral("lazy-backend"));
        QVERIFY(registry.publish(entry));
        QVERIFY(registry.publish(entry));
        QCOMPARE(providerCreations, 0);
        QCOMPARE(registry.generation(), QStringLiteral("1"));
    }
};

QTEST_GUILESS_MAIN(TstCapabilityRegistry)
#include "tst_capability_registry.moc"
