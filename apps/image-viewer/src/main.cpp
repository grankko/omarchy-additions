#include "backend.h"
#include "photoitem.h"
#include "theme.h"
#include "translator.h"

#include <QCommandLineParser>
#include <QGuiApplication>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
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
    app.setApplicationVersion("0.1.0");
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
    const QUrl url(input);
    const auto path = url.isLocalFile() ? url.toLocalFile() : input;
    if (parser.isSet(formats))
        return report({{"formats", QJsonArray::fromStringList(ImageIO::formats())}}, {}, parser.isSet(json));
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
            return report({{"saved", result.stamp.valid}}, result.error, parser.isSet(json));
        }
        return report({{"width", source.original.width()}, {"height", source.original.height()},
                       {"format", QString::fromLatin1(source.format)}, {"saveBlock", source.saveBlock}},
                      {}, parser.isSet(json));
    }

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
    engine.rootContext()->setContextProperty("viewer", &backend);
    engine.rootContext()->setContextProperty("omarchyTheme", &theme);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;

    if (!path.isEmpty())
        backend.open(path);
    return app.exec();
}
