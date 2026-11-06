#include "viewer-tests.h"
#include "backend.h"
#include "photoitem.h"
#include "theme.h"

#include <QDir>
#include <QFile>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtTest>
#include <atomic>

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
