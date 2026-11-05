#include "viewer-tests.h"
#include "backend.h"
#include "imageio.h"

#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QScopeGuard>
#include <QSemaphore>
#include <QTemporaryDir>
#include <QtTest>
#include <atomic>

void ViewerTests::navigatesAndRecoversFromInvalidFiles()
{
    QTemporaryDir dir;
    QVERIFY(fixture().save(dir.filePath("photo2.png")));
    QVERIFY(fixture(70, 40).save(dir.filePath("photo10.png")));
    for (const auto *name : {"photo3.png", "photo4.png", "photo11.png"}) {
        QFile broken(dir.filePath(name));
        QVERIFY(broken.open(QIODevice::WriteOnly));
        QCOMPARE(broken.write("broken"), 6);
    }

    Backend backend;
    backend.open(dir.filePath("photo2.png"));
    QTRY_COMPARE(backend.name(), "photo2.png");
    QTRY_VERIFY(backend.next());
    QVERIFY(!backend.previous());

    const auto settled = [&] {
        return QTest::qWaitFor([&] { return !backend.busy(); }) ? backend.name() : QString{};
    };
    backend.navigate(1);
    QCOMPARE(settled(), "photo2.png");
    QVERIFY(backend.status().contains("photo3.png"));
    backend.navigate(1);
    QCOMPARE(settled(), "photo2.png");
    QVERIFY(backend.status().contains("photo4.png"));
    backend.navigate(1);
    QCOMPARE(settled(), "photo10.png");
    QCOMPARE(backend.frame().size(), QSize(70, 40));
    backend.navigate(1);
    QCOMPARE(settled(), "photo10.png");
    QVERIFY(backend.status().contains("photo11.png"));
    QVERIFY(!backend.next());
    backend.navigate(-1);
    QCOMPARE(settled(), "photo10.png");
    backend.navigate(-1);
    QCOMPARE(settled(), "photo10.png");
    QVERIFY(backend.status().contains("photo4.png"));
}

void ViewerTests::retainsSnapshotOnFailedRefreshAndReconcilesDeletedCursor()
{
    QTemporaryDir dir;
    QVERIFY(fixture().save(dir.filePath("photo1.png")));
    QVERIFY(fixture().save(dir.filePath("photo2.png")));
    QVERIFY(fixture().save(dir.filePath("photo3.png")));

    const auto failScan = std::make_shared<std::atomic<bool>>(false);
    Backend::Jobs jobs;
    jobs.scan = [failScan](const QString &path, const QStringList &suffixes, const std::atomic_bool *stop) {
        if (*failScan)
            return ImageIO::FolderSnapshot{{}, "fixture scan failure"};
        return ImageIO::folderImages(path, suffixes, stop);
    };
    Backend backend(jobs);
    backend.open(dir.filePath("photo2.png"));
    QTRY_COMPARE(backend.name(), "photo2.png");
    QTRY_VERIFY(backend.next() && backend.previous());

    *failScan = true;
    backend.refresh();
    QTRY_COMPARE(backend.status(), "fixture scan failure");
    QVERIFY(backend.next() && backend.previous());
    QVERIFY(QFile::remove(dir.filePath("photo2.png")));
    *failScan = false;
    backend.refresh();
    backend.navigate(1);
    QTRY_COMPARE(backend.name(), "photo3.png");
    backend.navigate(-1);
    QTRY_COMPARE(backend.name(), "photo1.png");
}

void ViewerTests::coalescesForegroundRequestsWhileReadIsHeld()
{
    QTemporaryDir dir;
    const auto first = dir.filePath("photo1.png");
    const auto second = dir.filePath("photo2.png");
    const auto third = dir.filePath("photo3.png");
    QVERIFY(fixture().save(first));
    QVERIFY(fixture().save(second));
    QVERIFY(fixture(70, 40).save(third));

    const auto gate = std::make_shared<QSemaphore>();
    const auto started = std::make_shared<std::atomic<bool>>(false);
    Backend::Jobs jobs;
    jobs.read = [first, gate, started](const QString &path, int edge, const std::atomic_bool *stop) {
        if (path == first && !started->exchange(true))
            gate->acquire();
        return ImageIO::readCancellable(path, edge, stop);
    };
    Backend backend(jobs);
    const auto release = qScopeGuard([&] { gate->release(); });
    backend.open(first);
    QTRY_VERIFY(*started);
    backend.open(second);
    backend.open(third);
    QVERIFY(backend.busy());
    QVERIFY(backend.frame().isNull());
    QSignalSpy shown(&backend, &Backend::photoChanged);
    gate->release();
    QTRY_COMPARE(backend.name(), "photo3.png");
    QCOMPARE(backend.frame().size(), QSize(70, 40));
    QCOMPARE(shown.size(), 1);
}

void ViewerTests::detailCanBeDroppedAndCannotBlockForeground()
{
    QTemporaryDir dir;
    const auto first = dir.filePath("photo1.jpg");
    const auto second = dir.filePath("photo2.jpg");
    QVERIFY(fixture(2400, 1600).save(first));
    QVERIFY(fixture(320, 240).save(second));

    const auto gate = std::make_shared<QSemaphore>();
    const auto detailStarted = std::make_shared<std::atomic<int>>(0);
    Backend::Jobs jobs;
    jobs.read = [first, gate, detailStarted](const QString &path, int edge, const std::atomic_bool *stop) {
        if (path == first && edge > 512 && detailStarted->fetch_add(1) == 0)
            gate->acquire();
        return ImageIO::readCancellable(path, edge, stop);
    };
    jobs.prefetch = [](const QString &, int, const std::atomic_bool *) { return ImageIO::Photo{}; };
    Backend backend(jobs);
    const auto release = qScopeGuard([&] { gate->release(); });
    backend.setPreviewEdge(512);
    backend.open(first);
    QTRY_COMPARE(backend.name(), "photo1.jpg");
    QCOMPARE(backend.frame().width(), 512);
    backend.detail(2400);
    QTRY_VERIFY(*detailStarted > 0);
    backend.open(second);
    QTRY_COMPARE(backend.name(), "photo2.jpg");
    QCOMPARE(backend.frame().size(), QSize(320, 240));
    gate->release();

    backend.open(first);
    QTRY_COMPARE(backend.name(), "photo1.jpg");
    backend.detail(2400);
    QTRY_VERIFY(backend.frame().width() > 512);
    backend.detail(0);
    QCOMPARE(backend.frame().width(), 512);
    QVERIFY(ImageIO::boundedDetailEdge(QSize(12000, 12000), 8192) <= 2048);
}

void ViewerTests::clipboardCopiesFullResolutionWithRotation()
{
    if (QGuiApplication::platformName() != "offscreen")
        QSKIP("Clipboard tests require the private offscreen clipboard.");
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

void ViewerTests::localSendReceivesOneAbsolutePath()
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

void ViewerTests::heldScanCannotBlockOrReplaceNewFolder()
{
    QTemporaryDir first;
    QTemporaryDir second;
    const auto oldPhoto = first.filePath("old.png");
    const auto newPhoto = second.filePath("photo1.png");
    QVERIFY(fixture().save(oldPhoto));
    QVERIFY(fixture(70, 40).save(newPhoto));
    QVERIFY(fixture().save(second.filePath("photo2.png")));

    const auto gate = std::make_shared<QSemaphore>();
    const auto started = std::make_shared<std::atomic_bool>(false);
    Backend::Jobs jobs;
    jobs.scan = [gate, started, oldFolder = first.path()](const QString &path, const QStringList &suffixes,
                                                        const std::atomic_bool *stop) {
        if (path == oldFolder) {
            started->store(true);
            gate->acquire();
        }
        return ImageIO::folderImages(path, suffixes, stop);
    };
    Backend backend(jobs);
    const auto release = qScopeGuard([&] { gate->release(); });
    backend.open(oldPhoto);
    QTRY_VERIFY(started->load());
    QTRY_COMPARE(backend.name(), "old.png");
    backend.open(newPhoto);
    QTRY_COMPARE(backend.name(), "photo1.png");
    QVERIFY(!backend.next());
    gate->release();
    QTRY_VERIFY(backend.next());
    QVERIFY(!backend.previous());
    QCOMPARE(backend.frame().size(), QSize(70, 40));
}
