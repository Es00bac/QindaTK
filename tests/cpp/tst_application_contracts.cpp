// SPDX-License-Identifier: LGPL-3.0-or-later
#include "application_contracts.h"

#include <QtTest>
#include <limits>

using namespace QindaTK;

class TstApplicationContracts final : public QObject {
    Q_OBJECT
private slots:
    void revisionIdentityAndQmlRoundTrip()
    {
        const auto document = DocumentId::create();
        QVERIFY(document.isValid());
        QCOMPARE(DocumentId::parse(document.toString()), document);
        QVERIFY(!DocumentId::parse(QStringLiteral("not-a-uuid")).isValid());

        const SnapshotHeader original{QStringLiteral("scene.v3"), document,
                                      RevisionId{std::numeric_limits<quint64>::max()},
                                      std::numeric_limits<quint64>::max()};
        const QVariantMap wire = ContractCodec::toQml(original);
        QCOMPARE(wire.value(QStringLiteral("revision")).metaType().id(), QMetaType::QString);
        QCOMPARE(wire.value(QStringLiteral("resetGeneration")).metaType().id(), QMetaType::QString);
        OperationError error;
        const auto decoded = ContractCodec::snapshotHeaderFromQml(wire, &error);
        QVERIFY2(decoded.has_value(), qPrintable(error.message));
        QCOMPARE(decoded->schemaId, original.schemaId);
        QCOMPARE(decoded->documentId, original.documentId);
        QCOMPARE(decoded->revision, original.revision);
        QCOMPARE(decoded->resetGeneration, original.resetGeneration);
        QVERIFY(!error);
    }

    void rejectsLossyQmlNumbersAndMalformedDecimals()
    {
        QVariantMap header{{QStringLiteral("schemaId"), QStringLiteral("scene.v1")},
                           {QStringLiteral("documentId"), DocumentId::create().toString()},
                           {QStringLiteral("revision"), QStringLiteral("9007199254740993")},
                           {QStringLiteral("resetGeneration"), QStringLiteral("4")}};
        OperationError error;
        QVERIFY(ContractCodec::snapshotHeaderFromQml(header, &error));
        header[QStringLiteral("revision")] = QVariant::fromValue<qulonglong>(9007199254740993ULL);
        QVERIFY(!ContractCodec::snapshotHeaderFromQml(header, &error));
        QCOMPARE(error.code, ErrorCode::InvalidArgument);
        QCOMPARE(error.fieldPath, QStringLiteral("revision"));

        header[QStringLiteral("revision")] = QStringLiteral("+12");
        QVERIFY(!ContractCodec::snapshotHeaderFromQml(header, &error));
        header[QStringLiteral("revision")] = QStringLiteral("18446744073709551616");
        QVERIFY(!ContractCodec::snapshotHeaderFromQml(header, &error));
        QVERIFY(!RevisionId::parseDecimal(QStringLiteral("-1")));
    }

    void exactTimeUsesStringCodecAndRejectsNumbers()
    {
        const ExactTime original{std::numeric_limits<qint64>::min(), 30000, 1001};
        const QVariantMap wire = ContractCodec::toQml(original);
        QCOMPARE(wire.value(QStringLiteral("ticks")).metaType().id(), QMetaType::QString);
        OperationError error;
        const auto decoded = ContractCodec::exactTimeFromQml(wire, &error);
        QVERIFY2(decoded.has_value(), qPrintable(error.message));
        QCOMPARE(decoded->ticks, original.ticks);
        QCOMPARE(decoded->rateNumerator, original.rateNumerator);
        QCOMPARE(decoded->rateDenominator, original.rateDenominator);

        QVariantMap inexact = wire;
        inexact[QStringLiteral("ticks")] = QVariant::fromValue<qlonglong>(9007199254740993LL);
        QVERIFY(!ContractCodec::exactTimeFromQml(inexact, &error));
        QCOMPARE(error.fieldPath, QStringLiteral("ticks"));
        inexact = wire;
        inexact[QStringLiteral("rateNumerator")] = QStringLiteral("0");
        QVERIFY(!ContractCodec::exactTimeFromQml(inexact, &error));
    }

    void resetGenerationAndCancellationInvalidateRequests()
    {
        const SnapshotHeader before{QStringLiteral("doc"), DocumentId::create(),
                                    RevisionId{8}, 5};
        RequestContext request{QStringLiteral("render"), before, {}, {}};
        QVERIFY(request.accepts(before));
        auto edited = before;
        edited.revision.value++;
        QVERIFY(!request.accepts(edited));
        auto reset = before;
        ++reset.resetGeneration;
        QVERIFY(!request.accepts(reset));
        auto replaced = before;
        replaced.documentId = DocumentId::create();
        QVERIFY(!request.accepts(replaced));
        auto incompatibleSchema = before;
        incompatibleSchema.schemaId = QStringLiteral("doc.v2");
        QVERIFY(!request.accepts(incompatibleSchema));
        auto invalidIdentity = before;
        invalidIdentity.documentId = {};
        QVERIFY(!request.accepts(invalidIdentity));

        CancellationSource source;
        request.cancellation = source.token();
        QVERIFY(request.accepts(before));
        source.cancel();
        QVERIFY(request.cancellation.isCancelled());
        QVERIFY(!request.accepts(before));
    }

    void immutableSnapshotsKeepTheirVersionAndPayload()
    {
        const SnapshotPtr first = std::make_shared<const ImmutableSnapshot>(ImmutableSnapshot{
            {QStringLiteral("model.v1"), DocumentId::create(), RevisionId{2}, 9},
            QByteArray("old-state")});
        const SnapshotPtr second = std::make_shared<const ImmutableSnapshot>(ImmutableSnapshot{
            {QStringLiteral("model.v1"), first->header.documentId, RevisionId{3}, 9},
            QByteArray("new-state")});
        QVERIFY(first);
        QVERIFY(second);
        QCOMPARE(first->header.revision.value, quint64(2));
        QCOMPARE(first->payload, QByteArray("old-state"));
        QCOMPARE(second->header.revision.value, quint64(3));
        QCOMPARE(second->payload, QByteArray("new-state"));
        QVERIFY(first.get() != second.get());
    }
};

QTEST_GUILESS_MAIN(TstApplicationContracts)
#include "tst_application_contracts.moc"
