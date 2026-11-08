#include "viewer-tests.h"
#include "backend.h"
#include "photoitem.h"
#include "theme.h"
#include "translator.h"

#include <QDir>
#include <QFile>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QPointingDevice>
#include <QNativeGestureEvent>
#include <QPainter>
#include <QTemporaryDir>
#include <QtTest>
#include <atomic>

void ViewerTests::qmlContextMenuCoversPicture()
{
    QTemporaryDir dir;
    QFile language(dir.filePath("language"));
    QVERIFY(language.open(QIODevice::WriteOnly));
    language.write("en\n");
    language.close();
    Translator translator(language.fileName());
    QGuiApplication::installTranslator(&translator);
    const auto path = dir.filePath("Synthetic landscape.png");
    QImage image(1200, 800, QImage::Format_RGB32);
    QPainter painter(&image);
    QLinearGradient gradient(0, 0, 1200, 800);
    gradient.setColorAt(0, QColor("#254857"));
    gradient.setColorAt(1, QColor("#e8b887"));
    painter.fillRect(image.rect(), gradient);
    painter.setBrush(QColor("#243b48"));
    painter.setPen(Qt::NoPen);
    painter.drawPolygon(QPolygon{{0, 800}, {420, 220}, {820, 800}});
    painter.setBrush(QColor("#456674"));
    painter.drawPolygon(QPolygon{{480, 800}, {900, 300}, {1200, 800}});
    painter.end();
    QVERIFY(image.save(path));
    Backend backend;
    Theme theme(dir.filePath("theme"));
    QQmlApplicationEngine engine;
    connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    engine.rootContext()->setContextProperty("viewer", &backend);
    engine.rootContext()->setContextProperty("omarchyTheme", &theme);
    engine.load(QUrl(qEnvironmentVariable("VIEWER_TEST_QML", "qrc:/Main.qml")));
    QCOMPARE(engine.rootObjects().size(), 1);
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QVERIFY(window && QTest::qWaitForWindowExposed(window));
    auto *menu = window->findChild<QObject *>("imageContextMenu");
    auto *handle = window->findChild<QQuickItem *>("dockHandle");
    QVERIFY(menu && handle);
    backend.open(path);
    QTRY_COMPARE(backend.name(), "Synthetic landscape.png");
    const QList<QPoint> points{{10, 10}, {window->width() - 10, 10},
        {10, window->height() - 10}, {window->width() - 10, window->height() - 10},
        {window->width() / 2, window->height() / 2},
        handle->mapToScene(handle->boundingRect().center()).toPoint()};
    for (const auto &point : points) {
        QTest::mouseClick(window, Qt::RightButton, Qt::NoModifier, point);
        QTRY_VERIFY2(menu->property("visible").toBool(), qPrintable(QString("Right click at %1,%2").arg(point.x()).arg(point.y())));
        QTRY_VERIFY(menu->property("opened").toBool());
        QVERIFY(!menu->property("fromDock").toBool());
        auto *content = qvariant_cast<QQuickItem *>(menu->property("background"));
        QVERIFY(content);
        const auto bounds = content->mapRectToScene(content->boundingRect());
        QVERIFY(bounds.left() >= 0 && bounds.right() <= window->width());
        QVERIFY(bounds.top() >= 0 && bounds.bottom() <= window->height());
        // Native compositor policy can leave a synthetic test window inactive.
        if (window->isActive())
            QTest::keyClick(window, Qt::Key_Escape);
        else
            QVERIFY(QMetaObject::invokeMethod(menu, "close"));
        QTRY_VERIFY(!menu->property("visible").toBool());
    }
    const auto output = qEnvironmentVariable("VIEWER_QA_DIR");
    if (!output.isEmpty()) {
        QDir().mkpath(output);
        QTest::mouseClick(window, Qt::RightButton, Qt::NoModifier, QPoint(220, 100));
        QTRY_VERIFY(menu->property("visible").toBool());
        QTRY_VERIFY(menu->property("opened").toBool());
        QVERIFY(window->grabWindow().save(output + "/context-menu.png"));
        QVERIFY(language.open(QIODevice::WriteOnly | QIODevice::Truncate));
        language.write("da\n");
        language.close();
        QTRY_COMPARE(translator.translate("", "Open image…", "", -1), "Åbn billede…");
        QSignalSpy frames(window, &QQuickWindow::frameSwapped);
        window->update();
        QTRY_VERIFY(!frames.isEmpty());
        QVERIFY(window->grabWindow().save(output + "/context-menu-da.png"));
    }
}

void ViewerTests::qmlNativePinchUsesViewportZoom()
{
    QTemporaryDir dir;
    const auto path = dir.filePath("photo.png");
    QVERIFY(fixture(1600, 1200).save(path));
    Backend backend;
    Theme theme(dir.filePath("theme"));
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("viewer", &backend);
    engine.rootContext()->setContextProperty("omarchyTheme", &theme);
    engine.load(QUrl("qrc:/Main.qml"));
    QCOMPARE(engine.rootObjects().size(), 1);
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QVERIFY(window && QTest::qWaitForWindowExposed(window));
    auto *viewport = window->findChild<QQuickItem *>("imageViewport");
    QVERIFY(viewport);
    viewport->setProperty("interactive", true);
    backend.open(path);
    QTRY_COMPARE(backend.name(), "photo.png");
    QPointingDevice device("Synthetic trackpad", 1, QInputDevice::DeviceType::TouchPad,
                          QPointingDevice::PointerType::Finger, QInputDevice::Capability::Position,
                          2, 0);
    const QPointF at(window->width() * .3, window->height() * .4);
    const auto gesture = [&](Qt::NativeGestureType type, qreal value = 0) {
        QNativeGestureEvent event(type, &device, 2, at, at, window->mapToGlobal(at), value, {});
        QCoreApplication::sendEvent(window, &event);
    };
    gesture(Qt::BeginNativeGesture);
    gesture(Qt::ZoomNativeGesture, .5);
    QTRY_VERIFY(viewport->property("zoom").toReal() > 1.4);
    const auto offset = viewport->property("offsetX").toReal();
    QVERIFY(offset > 0);
    gesture(Qt::EndNativeGesture);
    gesture(Qt::BeginNativeGesture);
    gesture(Qt::ZoomNativeGesture, -.2);
    gesture(Qt::EndNativeGesture);
    QVERIFY(qAbs(viewport->property("zoom").toReal() - 1.2) < .01);
    gesture(Qt::BeginNativeGesture);
    gesture(Qt::RotateNativeGesture, 30);
    gesture(Qt::EndNativeGesture);
    QCOMPARE(backend.turns(), 0);
    viewport->setProperty("interactive", false);
    gesture(Qt::BeginNativeGesture);
    gesture(Qt::ZoomNativeGesture, 1);
    gesture(Qt::EndNativeGesture);
    QVERIFY(qAbs(viewport->property("zoom").toReal() - 1.2) < .01);
    QVERIFY(QMetaObject::invokeMethod(viewport, "fit"));
    QCOMPARE(viewport->property("zoom").toReal(), 1.0);
}

void ViewerTests::qmlExternalOpenProtectsEdits()
{
    QTemporaryDir dir;
    const auto first = dir.filePath("photo1.png");
    const auto second = dir.filePath("photo2.png");
    QVERIFY(fixture().save(first));
    QVERIFY(fixture(70, 40).save(second));
    Backend backend;
    Theme theme(dir.filePath("theme"));
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("viewer", &backend);
    engine.rootContext()->setContextProperty("omarchyTheme", &theme);
    engine.load(QUrl("qrc:/Main.qml"));
    QCOMPARE(engine.rootObjects().size(), 1);
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    auto *prompt = window->findChild<QObject *>("unsavedEdits");
    auto *save = window->findChild<QObject *>("saveUnsaved");
    auto *cancel = window->findChild<QObject *>("cancelUnsaved");
    QVERIFY(prompt && save && cancel);
    const auto open = [&](const QString &path) {
        return QMetaObject::invokeMethod(window, "requestOpen", Q_ARG(QVariant, path));
    };
    QVERIFY(open(first));
    QTRY_COMPARE(backend.path(), first);
    backend.rotate(1);
    QVERIFY(open(first));
    QVERIFY(!prompt->property("visible").toBool());
    QCOMPARE(backend.turns(), 1);
    QVERIFY(open(second));
    QTRY_VERIFY(prompt->property("visible").toBool());
    QCOMPARE(prompt->property("destination").toString(), "photo2.png");
    QVERIFY(QMetaObject::invokeMethod(cancel, "click"));
    QTRY_VERIFY(!prompt->property("visible").toBool());
    QCOMPARE(backend.path(), first);
    QCOMPARE(backend.turns(), 1);
    QVERIFY(open(second));
    QTRY_VERIFY(prompt->property("visible").toBool());
    QVERIFY(QMetaObject::invokeMethod(save, "click"));
    QTRY_COMPARE(backend.path(), second);
    QCOMPARE(QImage(first).size(), QSize(64, 96));
    backend.rotate(1);
    QVERIFY(fixture(40, 40).save(second));
    QVERIFY(open(first));
    QTRY_VERIFY(prompt->property("visible").toBool());
    QVERIFY(QMetaObject::invokeMethod(save, "click"));
    QTRY_VERIFY(!backend.busy());
    QCOMPARE(backend.path(), second);
    QCOMPARE(backend.turns(), 1);
    QVERIFY(backend.statusPersistent());
}

class TestPhotoItem : public PhotoItem
{
public:
    using PhotoItem::PhotoItem;
    std::atomic<bool> failTexture{false};
    std::atomic<bool> failNode{false};

protected:
    QSGTexture *createPhotoTexture() override
    {
        return failTexture ? nullptr : PhotoItem::createPhotoTexture();
    }

    QSGImageNode *createPhotoNode() override
    {
        return failNode ? nullptr : PhotoItem::createPhotoNode();
    }
};

void ViewerTests::failedRenderNeverKeepsAnOlderPhoto()
{
    if (QGuiApplication::platformName() != "offscreen")
        QSKIP("The pixel assertion uses the offscreen renderer.");
    QQuickWindow window;
    window.setColor(Qt::black);
    window.resize(64, 64);
    TestPhotoItem item(window.contentItem());
    item.setSize({64, 64});
    QSignalSpy failures(&item, &PhotoItem::presentationFailed);
    const auto render = [&] { return window.grabWindow().pixelColor(32, 32); };
    const auto frame = [](QColor color) {
        QImage image(64, 64, QImage::Format_RGB32);
        image.fill(color);
        return image;
    };
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    item.failNode = true;
    item.setPhoto(frame(Qt::blue));
    QCOMPARE(render(), QColor(Qt::black));
    QTRY_COMPARE(failures.size(), 1);
    item.failNode = false;
    item.setPhoto(frame(Qt::green));
    QCOMPARE(render(), QColor(Qt::green));
    item.failTexture = true;
    item.setPhoto(frame(Qt::red));
    QCOMPARE(render(), QColor(Qt::black));
    QTRY_COMPARE(failures.size(), 2);
    item.failTexture = false;
    item.setPhoto(frame(Qt::blue));
    QCOMPARE(render(), QColor(Qt::blue));
}

void ViewerTests::qmlContinuesOnlyAfterVerifiedSave()
{
    QTemporaryDir dir;
    const auto first = dir.filePath("photo1.png");
    const auto second = dir.filePath("photo2.png");
    QVERIFY(fixture().save(first));
    QVERIFY(fixture(70, 40).save(second));
    Backend backend;
    Theme theme(dir.filePath("theme"));
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("viewer", &backend);
    engine.rootContext()->setContextProperty("omarchyTheme", &theme);
    engine.load(QUrl("qrc:/Main.qml"));
    QCOMPARE(engine.rootObjects().size(), 1);
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QVERIFY(window);
    auto *prompt = window->findChild<QObject *>("unsavedEdits");
    auto *save = window->findChild<QObject *>("saveUnsaved");
    auto *cancel = window->findChild<QObject *>("cancelUnsaved");
    QVERIFY(prompt && save && cancel);

    backend.open(first);
    QTRY_COMPARE(backend.name(), "photo1.png");
    QTRY_VERIFY(backend.next());
    backend.rotate(1);
    QTest::keyClick(window, Qt::Key_Right);
    QTRY_VERIFY(prompt->property("visible").toBool());
    QVERIFY(QMetaObject::invokeMethod(cancel, "click"));
    QTRY_VERIFY(!prompt->property("visible").toBool());
    QCOMPARE(backend.name(), "photo1.png");
    QCOMPARE(backend.turns(), 1);

    QTest::keyClick(window, Qt::Key_Right);
    QTRY_VERIFY(prompt->property("visible").toBool());
    QSignalSpy shown(&backend, &Backend::photoChanged);
    QVERIFY(QMetaObject::invokeMethod(save, "click"));
    QTRY_COMPARE(backend.name(), "photo2.png");
    QCOMPARE(shown.size(), 1);
    QCOMPARE(QImage(first).size(), QSize(64, 96));

    backend.rotate(1);
    QVERIFY(fixture(40, 40).save(second));
    QTest::keyClick(window, Qt::Key_Left);
    QTRY_VERIFY(prompt->property("visible").toBool());
    QVERIFY(QMetaObject::invokeMethod(save, "click"));
    QTRY_VERIFY(!backend.busy());
    QCOMPARE(backend.name(), "photo2.png");
    QCOMPARE(backend.turns(), 1);
    QVERIFY(!backend.canSave());
    QVERIFY(backend.statusPersistent());

    auto *close = window->findChild<QQuickItem *>("closeWindowButton");
    auto *discard = window->findChild<QObject *>("discardUnsaved");
    QVERIFY(close && discard);
    const auto atClose = close->mapToScene(close->boundingRect().center()).toPoint();
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, atClose);
    QTRY_VERIFY(prompt->property("visible").toBool());
    QVERIFY(window->isVisible());
    QVERIFY(!save->property("enabled").toBool());
    QVERIFY(QMetaObject::invokeMethod(cancel, "click"));
    QTRY_VERIFY(!prompt->property("visible").toBool());
    QCOMPARE(backend.turns(), 1);
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, atClose);
    QTRY_VERIFY(prompt->property("visible").toBool());
    QVERIFY(QMetaObject::invokeMethod(discard, "click"));
    QTRY_VERIFY(!window->isVisible());
    QCOMPARE(QImage(second).size(), QSize(40, 40));
}

void ViewerTests::benchmarkNavigation()
{
    const auto directory = qEnvironmentVariable("VIEWER_BENCH_DIR");
    if (directory.isEmpty())
        QSKIP("Set VIEWER_BENCH_DIR to a disposable two-image benchmark folder.");
    QTemporaryDir themeDirectory;
    const auto first = QDir(directory).filePath("Viewer bench 1.jpg");
    const auto second = QDir(directory).filePath("Viewer bench 2.jpg");
    QVERIFY(QFileInfo::exists(first) && QFileInfo::exists(second));
    Backend backend;
    Theme theme(themeDirectory.path());
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("viewer", &backend);
    engine.rootContext()->setContextProperty("omarchyTheme", &theme);
    engine.load(QUrl("qrc:/Main.qml"));
    QCOMPARE(engine.rootObjects().size(), 1);
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QVERIFY(window && QTest::qWaitForWindowExposed(window));
    qInfo() << "benchmark viewport" << window->size() << "DPR" << window->devicePixelRatio();
    backend.open(first);
    QTRY_COMPARE(backend.name(), "Viewer bench 1.jpg");
    QTRY_VERIFY(backend.next());
    QTest::qWait(3000);
    for (int sample = 0; sample < 34; ++sample) {
        const bool forward = sample % 2 == 0;
        backend.navigate(forward ? 1 : -1);
        QTRY_COMPARE(backend.name(), forward ? "Viewer bench 2.jpg" : "Viewer bench 1.jpg");
        QTest::qWait(130);
    }
    for (const auto &path : {"/proc/self/status", "/proc/self/smaps_rollup", "/proc/self/io"}) {
        QFile stats(path);
        if (stats.open(QIODevice::ReadOnly)) {
            for (const auto &line : stats.readAll().split('\n')) {
                if (line.startsWith("VmHWM:") || line.startsWith("VmRSS:")
                    || line.startsWith("Pss:") || line.startsWith("read_bytes:")) {
                    qInfo().noquote() << line.trimmed();
                }
            }
        }
    }
}

void ViewerTests::qmlShowsDockAndEnablesSaveOnlyForAnEdit()
{
    QTemporaryDir dir;
    const auto path = dir.filePath("photo.png");
    QVERIFY(fixture().save(path));
    Backend backend;
    Theme theme(dir.filePath("theme"));
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("viewer", &backend);
    engine.rootContext()->setContextProperty("omarchyTheme", &theme);
    engine.load(QUrl("qrc:/Main.qml"));
    QCOMPARE(engine.rootObjects().size(), 1);
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QVERIFY(window);
    auto *handle = window->findChild<QQuickItem *>("dockHandle");
    auto *save = window->findChild<QQuickItem *>("saveButton");
    auto *controls = window->findChild<QQuickItem *>("dockControls");
    auto *menu = window->findChild<QObject *>("imageContextMenu");
    auto *open = window->findChild<QQuickItem *>("openImageButton");
    auto *help = window->findChild<QObject *>("shortcutHelp");
    QVERIFY(handle && save && controls && menu && open && help);
    QTRY_VERIFY(open->isVisible() && open->width() > 0);
    QTest::mouseMove(window, open->mapToScene(open->boundingRect().center()).toPoint());
    QVERIFY(!handle->isVisible());

    backend.open(path);
    QTRY_VERIFY(handle->isVisible());
    QVERIFY(!controls->isVisible());
    const QPoint atHandle = handle->mapToScene(handle->boundingRect().center()).toPoint();
    QTest::mouseMove(window, atHandle);
    QTRY_VERIFY(controls->isVisible());
    QVERIFY(!save->isEnabled());
    QTest::keyClick(window, Qt::Key_R);
    QTRY_COMPARE(backend.turns(), 1);
    QVERIFY(save->isEnabled());
    QTest::keyClick(window, Qt::Key_S, Qt::ControlModifier);
    QTRY_COMPARE(backend.turns(), 0);
    QCOMPARE(QImage(path).size(), QSize(64, 96));
    QVERIFY(!save->isEnabled());

    QVERIFY(QMetaObject::invokeMethod(help, "open"));
    QTRY_VERIFY(help->property("visible").toBool());
    QTest::keyClick(window, Qt::Key_Escape);
    QTRY_VERIFY(!help->property("visible").toBool());

    QTest::keyClick(window, Qt::Key_F11);
    QVERIFY(window->visibility() != QWindow::FullScreen);

    QTest::mouseClick(window, Qt::RightButton, Qt::NoModifier, QPoint(100, 100));
    QTRY_VERIFY(menu->property("visible").toBool());
}
