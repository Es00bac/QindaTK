// SPDX-License-Identifier: LGPL-3.0-or-later
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QTemporaryFile>
#include <QUrl>

#include <qindatk/application_contracts.h>
#include <qindatk/capability_registry.h>

#include <limits>

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);

    QindaTK::CapabilityRegistry registry;
    const QString unavailableReason =
        QStringLiteral("PDF backend is not installed");
    if (!registry.publish({QStringLiteral("document.pdf"),
                           QStringLiteral("export"),
                           QStringLiteral("pdf.adapter"),
                           QStringLiteral("4.2.1"), false,
                           unavailableReason, 10})) {
        return 1;
    }
    const quint64 exactRevision = (quint64{1} << 53) + 123;
    const QindaTK::SnapshotHeader header{
        QStringLiteral("document.v1"), QindaTK::DocumentId::create(),
        QindaTK::RevisionId{exactRevision}, exactRevision};
    const QVariantMap headerMap = QindaTK::ContractCodec::toQml(header);
    QQmlApplicationEngine engine;
    engine.addImportPath(QINDATK_QML_IMPORT_ROOT);
    engine.rootContext()->setContextProperty(QStringLiteral("appCapabilities"),
                                             &registry);
    engine.rootContext()->setContextProperty(QStringLiteral("snapshotWire"),
                                             headerMap);
    engine.load(QUrl::fromLocalFile(QStringLiteral(CONSUMER_QML_FILE)));
    if (engine.rootObjects().size() != 1)
        return 2;

    QObject *root = engine.rootObjects().first();
    if (root->property("capabilityAvailable").toBool()
        || root->property("capabilityReason").toString() != unavailableReason
        || root->property("exportEnabled").toBool()
        || root->property("exportTooltip").toString() != unavailableReason
        || root->property("revisionText").toString() != QString::number(exactRevision)) {
        return 3;
    }

    const QByteArray opaquePayload = QByteArray::fromHex("00ff80756e6b6e6f776e00");
    const QindaTK::SnapshotPtr unsupportedSnapshot =
        std::make_shared<const QindaTK::ImmutableSnapshot>(
            QindaTK::ImmutableSnapshot{header, opaquePayload});
    if (headerMap.value(QStringLiteral("revision")).toString()
            != QString::number(exactRevision)
        || headerMap.value(QStringLiteral("resetGeneration")).toString()
            != QString::number(exactRevision)) {
        return 4;
    }
    QindaTK::OperationError error;
    const auto decodedHeader =
        QindaTK::ContractCodec::snapshotHeaderFromQml(headerMap, &error);
    if (!decodedHeader || decodedHeader->revision.value != exactRevision
        || decodedHeader->resetGeneration != exactRevision) {
        return 5;
    }

    // This local fixture persists opaque bytes and the versioned header only;
    // it does not claim to implement document-manager serialization.
    QTemporaryFile snapshotFile;
    if (!snapshotFile.open())
        return 6;
    const QJsonObject storedSnapshot{
        {QStringLiteral("header"), QJsonObject::fromVariantMap(
             QindaTK::ContractCodec::toQml(unsupportedSnapshot->header))},
        {QStringLiteral("opaquePayloadBase64"),
         QString::fromLatin1(unsupportedSnapshot->payload.toBase64())}};
    const QByteArray storedBytes =
        QJsonDocument(storedSnapshot).toJson(QJsonDocument::Compact);
    if (snapshotFile.write(storedBytes) != storedBytes.size()
        || !snapshotFile.flush() || !snapshotFile.seek(0)) {
        return 7;
    }
    QJsonParseError parseError;
    const QJsonDocument loadedDocument =
        QJsonDocument::fromJson(snapshotFile.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !loadedDocument.isObject())
        return 8;
    QJsonObject loadedObject = loadedDocument.object();
    const auto roundTripHeader = QindaTK::ContractCodec::snapshotHeaderFromQml(
        loadedObject.value(QStringLiteral("header")).toObject().toVariantMap(), &error);
    const QByteArray roundTripPayload = QByteArray::fromBase64(
        loadedObject.value(QStringLiteral("opaquePayloadBase64")).toString().toLatin1());
    if (!roundTripHeader || roundTripPayload != unsupportedSnapshot->payload
        || roundTripHeader->schemaId != unsupportedSnapshot->header.schemaId
        || roundTripHeader->documentId != unsupportedSnapshot->header.documentId
        || roundTripHeader->revision != unsupportedSnapshot->header.revision
        || roundTripHeader->resetGeneration
            != unsupportedSnapshot->header.resetGeneration) {
        return 9;
    }
    const QindaTK::SnapshotPtr restoredSnapshot =
        std::make_shared<const QindaTK::ImmutableSnapshot>(
            QindaTK::ImmutableSnapshot{*roundTripHeader, roundTripPayload});
    if (root->property("capabilityAvailable").toBool()
        || restoredSnapshot->payload != unsupportedSnapshot->payload
        || restoredSnapshot->header.revision != QindaTK::RevisionId{exactRevision}) {
        return 10;
    }

    const QindaTK::ExactTime exactTime{
        std::numeric_limits<qint64>::min(), 30000, 1001};
    const QVariantMap timeMap = QindaTK::ContractCodec::toQml(exactTime);
    if (timeMap.value(QStringLiteral("ticks")).toString()
            != QString::number(exactTime.ticks)
        || timeMap.value(QStringLiteral("rateNumerator")).toString() != "30000"
        || timeMap.value(QStringLiteral("rateDenominator")).toString() != "1001") {
        return 11;
    }
    const auto decodedTime = QindaTK::ContractCodec::exactTimeFromQml(timeMap, &error);
    if (!decodedTime || *decodedTime != exactTime)
        return 12;

    if (!registry.publish({QStringLiteral("document.pdf"),
                           QStringLiteral("export"),
                           QStringLiteral("pdf.adapter"),
                           QStringLiteral("4.2.1"), true, {}, 10})) {
        return 13;
    }
    QCoreApplication::processEvents();
    if (!root->property("capabilityAvailable").toBool()
        || root->property("capabilityReason").toString().size() != 0
        || !root->property("exportEnabled").toBool()
        || root->property("exportTooltip").toString() != QStringLiteral("Export PDF")
        || root->property("capabilityBackend").toString()
            != QStringLiteral("pdf.adapter")
        || root->property("capabilityVersion").toString()
            != QStringLiteral("4.2.1")) {
        return 15;
    }
    if (unsupportedSnapshot->payload != opaquePayload
        || restoredSnapshot->payload != opaquePayload
        || unsupportedSnapshot->header.schemaId != restoredSnapshot->header.schemaId
        || unsupportedSnapshot->header.documentId != restoredSnapshot->header.documentId
        || unsupportedSnapshot->header.revision != restoredSnapshot->header.revision
        || unsupportedSnapshot->header.resetGeneration
            != restoredSnapshot->header.resetGeneration) {
        return 14;
    }
    return 0;
}
