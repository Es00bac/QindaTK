// SPDX-License-Identifier: LGPL-3.0-or-later
#include <QQmlEngine>
#include <QtQuickTest/quicktest.h>

// Adds the build-tree import path before any test file loads.
class Setup final : public QObject {
    Q_OBJECT
public slots:
    void qmlEngineAvailable(QQmlEngine *engine)
    {
        engine->addImportPath(QStringLiteral(QINDATK_BUILD_QML_DIR));
    }
};

QUICK_TEST_MAIN_WITH_SETUP(qindatk_qml, Setup)

#include "tst_qml.moc"
