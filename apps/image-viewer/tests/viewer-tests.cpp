#include "viewer-tests.h"
#include "photoitem.h"
#include "instance.h"

#include <QQuickStyle>
#include <QFile>
#include <QTextStream>
#include <QtTest>

QImage ViewerTests::fixture(int width, int height)
{
    QImage image(width, height, QImage::Format_ARGB32);
    image.fill(QColor("#33aa6699"));
    image.setPixelColor(0, 0, Qt::blue);
    image.setPixelColor(width - 1, height - 1, Qt::yellow);
    return image;
}

void ViewerTests::initTestCase()
{
    QQuickStyle::setStyle("Material");
    qmlRegisterType<PhotoItem>("Omarchy.Viewer", 1, 0, "PhotoItem");
}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QFile version(QFINDTESTDATA("../VERSION"));
    if (version.open(QIODevice::ReadOnly))
        app.setApplicationVersion(QString::fromUtf8(version.readAll()).trimmed());
    if (qEnvironmentVariableIsSet("VIEWER_INSTANCE_PROBE")) {
        Instance instance;
        const auto result = instance.start(app.arguments().value(1), {});
        if (result == Instance::Result::Failed)
            return 1;
        if (result == Instance::Result::Forwarded)
            return 0;
        QObject::connect(&instance, &Instance::openRequested, &app, [](const QString &path, const QString &) {
            QTextStream(stdout) << (path.isEmpty() ? QStringLiteral("activate") : path) << Qt::endl;
        });
        instance.setReady();
        QTimer::singleShot(15000, &app, &QCoreApplication::quit);
        return app.exec();
    }
    ViewerTests tests;
    return QTest::qExec(&tests, argc, argv);
}
