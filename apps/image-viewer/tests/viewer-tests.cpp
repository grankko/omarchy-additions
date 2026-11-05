#include "backend.h"
#include "imageio.h"
#include "photoitem.h"
#include "theme.h"

#include <QClipboard>
#include <QColorSpace>
#include <QFile>
#include <QImageReader>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QtTest>

class ViewerTests : public QObject
{
    Q_OBJECT

private:
    static QImage fixture(int width = 96, int height = 64)
    {
        QImage image(width, height, QImage::Format_ARGB32);
        image.fill(QColor("#33aa6699"));
        image.setPixelColor(0, 0, Qt::blue);
        image.setPixelColor(width - 1, height - 1, Qt::yellow);
        return image;
    }

private slots:
    void initTestCase()
    {
        QQuickStyle::setStyle("Material");
        qmlRegisterType<PhotoItem>("Omarchy.Viewer", 1, 0, "PhotoItem");
    }

    void readsCommonFormats()
    {
        QTemporaryDir dir;
        for (const auto &extension : {"jpeg", "png", "webp", "tiff", "bmp"}) {
            const auto path = dir.filePath(QString("photo.") + extension);
            QVERIFY2(fixture().save(path), extension);
            const auto preview = ImageIO::read(path, 64);
            QVERIFY2(!preview.image.isNull(), qPrintable(preview.error));
            QCOMPARE(preview.original, QSize(96, 64));
            QVERIFY(qMax(preview.image.width(), preview.image.height()) <= 64);
        }
        const auto gif = dir.filePath("photo.gif");
        QFile file(gif);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.write(QByteArray::fromBase64("R0lGODlhAQABAIAAAAAAAP///yH5BAEAAAAALAAAAAABAAEAAAIBRAA7")) > 0);
        file.close();
        const auto preview = ImageIO::read(gif, 64);
        QVERIFY(!preview.image.isNull());
        QVERIFY(!preview.saveBlock.isEmpty());
    }

    void rotatesPngInPlaceWithMetadata()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath("photo.png");
        auto original = fixture();
        original.setColorSpace(QColorSpace::DisplayP3);
        original.setText("Description", "Fixture");
        original.setDotsPerMeterX(2835);
        original.setDotsPerMeterY(4000);
        QVERIFY(original.save(path));

        const auto source = ImageIO::read(path, 32);
        QVERIFY2(source.saveBlock.isEmpty(), qPrintable(source.saveBlock));
        const auto saved = ImageIO::save(path, source, 1);
        QVERIFY2(saved.stamp.valid, qPrintable(saved.error));

        QImage result(path);
        QCOMPARE(result.size(), QSize(64, 96));
        QCOMPARE(result.pixelColor(63, 0), Qt::blue);
        QCOMPARE(result.pixelColor(0, 95), Qt::yellow);
        QCOMPARE(result.pixelColor(62, 1), original.pixelColor(1, 1));
        QCOMPARE(result.colorSpace(), original.colorSpace());
        QCOMPARE(result.text("Description"), QString("Fixture"));
        QCOMPARE(result.dotsPerMeterX(), 4000);
        QCOMPARE(result.dotsPerMeterY(), 2835);
        QCOMPARE(QDir(dir.path()).entryList(QDir::Files | QDir::Hidden), QStringList{"photo.png"});
    }

    void rotatesExactJpegAndPreservesOddSizedPixels()
    {
        QVERIFY(!QStandardPaths::findExecutable("jpegtran").isEmpty());
        QTemporaryDir dir;
        const auto path = dir.filePath("wide.jpg");
        QVERIFY(fixture().save(path));
        const auto source = ImageIO::read(path, 64);
        QVERIFY2(source.saveBlock.isEmpty(), qPrintable(source.saveBlock));
        const auto saved = ImageIO::save(path, source, 1);
        QVERIFY2(saved.stamp.valid, qPrintable(saved.error));
        QCOMPARE(QImage(path).size(), QSize(64, 96));

        const auto odd = dir.filePath("odd.jpg");
        QVERIFY(fixture(93, 61).save(odd));
        const auto unaligned = ImageIO::read(odd, 64);
        const QImage original(odd);
        const auto fallback = ImageIO::save(odd, unaligned, 1);
        QVERIFY2(fallback.stamp.valid, qPrintable(fallback.error));
        QCOMPARE(QImage(odd), original);
        QCOMPARE(ImageIO::read(odd, 0).image, ImageIO::rotate(original, 1));
    }

    void savesJpegWithExistingExifOrientation()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath("oriented.jpg");
        QVERIFY(fixture().save(path));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto raw = file.readAll();
        const QImage pixels(path);
        file.close();

        for (int orientation = 1; orientation <= 8; ++orientation) {
            auto exif = QByteArray::fromHex("ffe1002245786966000049492a0008000000"
                                           "010012010300010000000100000000000000");
            exif[28] = char(orientation);
            auto jpeg = raw;
            jpeg.insert(2, exif);
            QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
            QCOMPARE(file.write(jpeg), jpeg.size());
            file.close();

            const auto photo = ImageIO::read(path, 0);
            QVERIFY2(photo.saveBlock.isEmpty(), qPrintable(photo.saveBlock));
            QVERIFY(!photo.image.isNull());
            const auto saved = ImageIO::save(path, photo, 1);
            QVERIFY2(saved.stamp.valid, qPrintable(saved.error));
            QCOMPARE(QImage(path), pixels);
            QCOMPARE(ImageIO::read(path, 0).image, ImageIO::rotate(photo.image, 1));
        }
    }

    void detectsExternalEditAndRejectsLinks()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath("photo.png");
        QVERIFY(fixture().save(path));
        const auto source = ImageIO::read(path, 64);
        QVERIFY(fixture(120, 90).save(path));
        const auto changed = ImageIO::save(path, source, 1);
        QVERIFY(!changed.stamp.valid);
        QCOMPARE(QImage(path).size(), QSize(120, 90));

        const auto link = dir.filePath("link.png");
        QVERIFY(QFile::link(path, link));
        QVERIFY(!ImageIO::read(link, 64).saveBlock.isEmpty());
        QVERIFY(QFile::setPermissions(path, QFile::ReadOwner));
        QVERIFY(!ImageIO::read(path, 64).saveBlock.isEmpty());
    }

    void navigatesAndRecoversFromInvalidFiles()
    {
        QTemporaryDir dir;
        QVERIFY(fixture().save(dir.filePath("photo2.png")));
        QVERIFY(fixture(100, 50).save(dir.filePath("photo3.png")));
        QVERIFY(fixture(70, 40).save(dir.filePath("photo10.png")));
        QFile broken(dir.filePath("photo11.png"));
        QVERIFY(broken.open(QIODevice::WriteOnly));
        broken.write("broken");
        broken.close();

        Backend backend;
        backend.open(dir.filePath("photo2.png"));
        QTRY_COMPARE(backend.name(), "photo2.png");
        QTRY_VERIFY(backend.next());
        QVERIFY(!backend.previous());
        backend.navigate(1);
        backend.navigate(1);
        QTRY_COMPARE(backend.name(), "photo10.png");
        QCOMPARE(backend.frame().size(), QSize(70, 40));
        backend.navigate(1);
        QTRY_VERIFY(!backend.busy());
        QCOMPARE(backend.name(), "photo10.png");
        QVERIFY(backend.status().contains("photo11.png"));
        backend.navigate(-1);
        QTRY_COMPARE(backend.name(), "photo3.png");
        QCOMPARE(backend.frame().size(), QSize(100, 50));
    }

    void clipboardCopiesFullResolutionWithRotation()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath("photo.png");
        QVERIFY(fixture(920, 710).save(path));
        Backend backend;
        backend.setPreviewEdge(512);
        backend.open(path);
        QTRY_VERIFY(!backend.busy());
        QCOMPARE(backend.frame().width(), 512);
        backend.rotate(1);
        backend.copy();
        QTRY_COMPARE(QGuiApplication::clipboard()->image().size(), QSize(710, 920));
        QCOMPARE(backend.turns(), 1);
    }

    void localSendReceivesOneAbsolutePath()
    {
        QTemporaryDir dir;
        const auto photo = dir.filePath("Coast å 1.png");
        const auto received = dir.filePath("arguments");
        QVERIFY(fixture().save(photo));

        QFile stub(dir.filePath("localsend"));
        QVERIFY(stub.open(QIODevice::WriteOnly));
        stub.write("#!/bin/sh\nprintf '%s\\n' \"$@\" > \"$VIEWER_TEST_ARGUMENTS\"\n");
        stub.close();
        QVERIFY(stub.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));

        const QByteArray previousPath = qgetenv("PATH");
        const auto restore = qScopeGuard([&] {
            qputenv("PATH", previousPath);
            qunsetenv("VIEWER_TEST_ARGUMENTS");
        });
        qputenv("PATH", QFile::encodeName(dir.path()) + ':' + previousPath);
        qputenv("VIEWER_TEST_ARGUMENTS", QFile::encodeName(received));

        Backend backend;
        QVERIFY(backend.canSend());
        backend.open(photo);
        QTRY_COMPARE(backend.name(), "Coast å 1.png");
        backend.send();
        QFile output(received);
        QTRY_VERIFY(output.size() > 0);
        QVERIFY(output.open(QIODevice::ReadOnly));
        QCOMPARE(output.readAll(), QFile::encodeName(photo) + '\n');
    }

    void qmlShowsDockAndEnablesSaveOnlyForAnEdit()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath("photo.png");
        QVERIFY(fixture().save(path));
        Backend backend;
        Theme theme;
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
};

QTEST_MAIN(ViewerTests)
#include "viewer-tests.moc"
