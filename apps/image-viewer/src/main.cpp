#include "backend.h"
#include "photoitem.h"
#include "theme.h"
#include "translator.h"
#include "instance.h"

#include <QCommandLineParser>
#include <QGuiApplication>
#include <QIcon>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QScopeGuard>
#include <algorithm>

namespace {
int report(const QJsonObject &data, const QString &error, bool json)
{
    if (json) {
        auto result = data;
        result.insert("ok", error.isEmpty());
        if (!error.isEmpty())
            result.insert("error", error);
        QTextStream(stdout) << QJsonDocument(result).toJson(QJsonDocument::Compact);
    } else if (error.isEmpty()) {
        QTextStream(stdout) << QJsonDocument(data).toJson(QJsonDocument::Indented);
    } else {
        QTextStream(stderr) << error << Qt::endl;
    }
    return error.isEmpty() ? 0 : 1;
}
}

int main(int argc, char *argv[])
{
    const auto activationToken = qgetenv("XDG_ACTIVATION_TOKEN");
    qunsetenv("XDG_ACTIVATION_TOKEN");
    // Keep inspection and explicit overwrite commands usable without a compositor.
    const QStringList arguments = [&] {
        QStringList list;
        for (int index = 1; index < argc && QByteArrayView(argv[index]) != "--"; ++index)
            list.append(QString::fromLocal8Bit(argv[index]));
        return list;
    }();
    if (arguments.contains("--inspect") || arguments.contains("--formats")
        || arguments.contains("--rotate")
        || std::any_of(arguments.cbegin(), arguments.cend(), [](const QString &arg) {
            return arg.startsWith("--rotate=");
        })) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    QGuiApplication app(argc, argv);
    app.setApplicationName("omarchy-image-viewer");
    app.setApplicationVersion(QStringLiteral(VIEWER_VERSION));
    app.setDesktopFileName("omarchy-image-viewer");
    app.setWindowIcon(QIcon::fromTheme("omarchy-image-viewer"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QCoreApplication::translate("main", "An image viewer for Omarchy."));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption formats("formats", "List available image decoders.");
    const QCommandLineOption inspect("inspect", "Inspect one image.");
    const QCommandLineOption rotate("rotate", "Overwrite one image with a clockwise rotation.", "degrees");
    const QCommandLineOption json("json", "Write a JSON result.");
    parser.addOptions({formats, inspect, rotate, json});
    parser.addPositionalArgument("image", "Image file to open.", "[image]");
    parser.process(app);
    const auto positionals = parser.positionalArguments();
    const auto input = positionals.value(0);
    QString pathError;
    const auto path = Instance::imagePath(input, pathError);
    if (!pathError.isEmpty())
        return report({}, pathError, parser.isSet(json));
    if (parser.isSet(formats))
        return report({{"formats", QJsonArray::fromStringList(ImageIO::formats())},
                       {"imageAllocationLimitMiB", QImageReader::allocationLimit()}}, {}, parser.isSet(json));
    if (parser.isSet(inspect) || parser.isSet(rotate)) {
        if (positionals.size() != 1 || (parser.isSet(inspect) && parser.isSet(rotate)))
            return report({}, "Specify one operation and one image.", parser.isSet(json));
        const auto source = ImageIO::read(path, 512);
        if (source.image.isNull())
            return report({}, source.error, parser.isSet(json));
        if (parser.isSet(rotate)) {
            const int degrees = parser.value(rotate).toInt();
            if (degrees != 90 && degrees != 180 && degrees != 270)
                return report({}, "Rotation must be 90, 180, or 270 degrees.", parser.isSet(json));
            const auto result = ImageIO::save(path, source, degrees / 90);
            QJsonObject data{{"saved", result.committed()},
                             {"uncertain", result.outcome == ImageIO::SaveOutcome::Uncertain}};
            if (!result.durabilityWarning.isEmpty())
                data.insert("warning", result.durabilityWarning);
            return report(data, result.error, parser.isSet(json));
        }
        return report({{"width", source.original.width()}, {"height", source.original.height()},
                       {"format", QString::fromLatin1(source.format)}, {"saveBlock", source.saveBlock}},
                      {}, parser.isSet(json));
    }

    if (positionals.size() > 1)
        return report({}, "Specify one image to open.", parser.isSet(json));
    Instance instance;
    const auto launch = instance.start(path, QString::fromUtf8(activationToken));
    if (launch == Instance::Result::Forwarded)
        return 0;
    if (launch == Instance::Result::Failed)
        return report({}, instance.error(), parser.isSet(json));
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &instance, &Instance::stop);

    // A single photo and small overlays do not need a GPU render pipeline.
    // Keep Qt's renderer override available for systems that benefit from it.
    if (qEnvironmentVariableIsEmpty("QT_QUICK_BACKEND") && qEnvironmentVariableIsEmpty("QSG_RHI_BACKEND"))
        QQuickWindow::setSceneGraphBackend("software");
    QQuickStyle::setStyle("Material");
    qmlRegisterType<PhotoItem>("Omarchy.Viewer", 1, 0, "PhotoItem");
    Translator translator;
    app.installTranslator(&translator);
    Theme theme;
    Backend backend;
    QQmlApplicationEngine engine;
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    QObject::connect(&translator, &Translator::changed, &backend, &Backend::stateChanged);
    engine.rootContext()->setContextProperty("viewer", &backend);
    engine.rootContext()->setContextProperty("omarchyTheme", &theme);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;

    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    if (!window)
        return 1;
    QObject::connect(&instance, &Instance::openRequested, window,
                     [window](const QString &image, const QString &token) {
        if (window->visibility() == QWindow::Minimized)
            window->showNormal();
        if (!token.isEmpty())
            qputenv("XDG_ACTIVATION_TOKEN", token.toUtf8());
        const auto clearToken = qScopeGuard([] { qunsetenv("XDG_ACTIVATION_TOKEN"); });
        window->requestActivate();
        if (!image.isEmpty())
            QMetaObject::invokeMethod(window, "requestOpen", Q_ARG(QVariant, image));
    });
    instance.setReady();
    return app.exec();
}
