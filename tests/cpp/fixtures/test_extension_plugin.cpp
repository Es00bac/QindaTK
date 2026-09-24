// SPDX-License-Identifier: LGPL-3.0-or-later
#include "extension_provider.h"

#include <QObject>

class QindaTestExtension final : public QObject, public QindaTK::ExtensionProvider {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QindaTK_ExtensionProvider_IID)
    Q_INTERFACES(QindaTK::ExtensionProvider)
public:
    QString extensionId() const override { return QStringLiteral("org.qindatk.fixture.counter"); }
    bool start(const QVariantMap &state, QString *error) override
    {
        Q_UNUSED(error)
        m_count = state.value(QStringLiteral("count")).toInt();
        m_running = true;
        return true;
    }
    QVariantMap saveState() const override
    {
        return {{QStringLiteral("count"), m_count}};
    }
    QVariantMap invoke(const QString &operation, const QVariantMap &arguments) override
    {
        if (!m_running || operation != QStringLiteral("increment"))
            return {{QStringLiteral("ok"), false}};
        m_count += arguments.value(QStringLiteral("by"), 1).toInt();
        return {{QStringLiteral("ok"), true}, {QStringLiteral("count"), m_count}};
    }
    void stop() override { m_running = false; }
private:
    int m_count = 0;
    bool m_running = false;
};

#include "test_extension_plugin.moc"
