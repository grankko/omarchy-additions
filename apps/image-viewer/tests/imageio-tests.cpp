#include "viewer-tests.h"
#include "backend.h"
#include "imageio.h"

#include <QColorSpace>
#include <QDir>
#include <QFile>
#include <QImageReader>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>
#include <cstdio>

void ViewerTests::readsCommonFormats()
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

void ViewerTests::rotatesPngInPlaceWithMetadata()
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

void ViewerTests::rotatesExactJpegAndPreservesOddSizedPixels()
{
    QVERIFY(!QStandardPaths::findExecutable("jpegtran").isEmpty());
    QTemporaryDir dir;
    const auto path = dir.filePath("wide.jpg");
    auto image = fixture();
    image.setColorSpace(QColorSpace::DisplayP3);
    image.setText("Description", "Synthetic comment");
    image.setDotsPerMeterX(2835);
    image.setDotsPerMeterY(5669);
    QVERIFY(image.save(path));
    const QImage before(path);
    QVERIFY(before.dotsPerMeterX() != before.dotsPerMeterY());
    const auto source = ImageIO::read(path, 64);
    QVERIFY2(source.saveBlock.isEmpty(), qPrintable(source.saveBlock));
    const auto saved = ImageIO::save(path, source, 1);
    QVERIFY2(saved.stamp.valid, qPrintable(saved.error));
    const QImage after(path);
    QCOMPARE(after.size(), QSize(64, 96));
    QCOMPARE(after.dotsPerMeterX(), before.dotsPerMeterY());
    QCOMPARE(after.dotsPerMeterY(), before.dotsPerMeterX());
    QCOMPARE(after.colorSpace(), before.colorSpace());
    QCOMPARE(after.text(), before.text());

    const auto odd = dir.filePath("odd.jpg");
    QVERIFY(fixture(93, 61).save(odd));
    const auto unaligned = ImageIO::read(odd, 64);
    const QImage original(odd);
    const auto fallback = ImageIO::save(odd, unaligned, 1);
    QVERIFY2(fallback.stamp.valid, qPrintable(fallback.error));
    QCOMPARE(QImage(odd), original);
    QCOMPARE(ImageIO::read(odd, 0).image, ImageIO::rotate(original, 1));
}

void ViewerTests::jpegHelperDeadlinePreservesOriginal()
{
    QTemporaryDir dir;
    const auto path = dir.filePath("photo.jpg");
    QVERIFY(fixture().save(path));
    QFile original(path);
    QVERIFY(original.open(QIODevice::ReadOnly));
    const auto bytes = original.readAll();
    original.close();

    QFile fake(dir.filePath("jpegtran"));
    QVERIFY(fake.open(QIODevice::WriteOnly));
    QVERIFY(fake.write("#!/bin/sh\nprintf started > \"$VIEWER_TEST_HELPER_STARTED\"\n"
                       "trap '' TERM\nexec /usr/bin/sleep 15\n") > 0);
    fake.close();
    QVERIFY(fake.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
    const auto previousPath = qgetenv("PATH");
    const auto restore = qScopeGuard([&] {
        qputenv("PATH", previousPath);
        qunsetenv("VIEWER_TEST_HELPER_STARTED");
    });
    const auto marker = dir.filePath("started");
    qputenv("VIEWER_TEST_HELPER_STARTED", QFile::encodeName(marker));
    qputenv("PATH", QFile::encodeName(dir.path()) + ':' + previousPath);

    const auto photo = ImageIO::read(path, 64);
    QVERIFY(photo.saveBlock.isEmpty());
    QElapsedTimer timer;
    timer.start();
    const auto failed = ImageIO::save(path, photo, 1, {.helperTimeoutMs = 100});
    QVERIFY(!failed.stamp.valid);
    QVERIFY(failed.error.contains("timed out"));
    QVERIFY(timer.elapsed() < 3000);

    auto control = std::make_shared<ImageIO::SaveControl>();
    QVERIFY(control->cancel());
    const auto cancelled = ImageIO::save(path, photo, 1, {control});
    QVERIFY(!cancelled.stamp.valid);
    QVERIFY(cancelled.error.contains("cancelled"));
    QVERIFY(!control->beginCommit());

    ImageIO::SaveControl committing;
    QVERIFY(committing.beginCommit());
    QVERIFY(!committing.cancel());

    QVERIFY(QFile::remove(marker));
    Backend backend;
    backend.open(path);
    QTRY_COMPARE(backend.name(), "photo.jpg");
    backend.rotate(1);
    QSignalSpy completed(&backend, &Backend::saveFinished);
    backend.save();
    QTRY_VERIFY(QFileInfo::exists(marker));
    QVERIFY(backend.cancelSave());
    QTRY_COMPARE(completed.size(), 1);
    QCOMPARE(completed.first().at(0).toBool(), false);
    QCOMPARE(backend.turns(), 1);
    QVERIFY(!backend.busy());
    QVERIFY(QFile::remove(marker));
    QVERIFY(original.open(QIODevice::ReadOnly));
    QCOMPARE(original.readAll(), bytes);
    QCOMPARE(QDir(dir.path()).entryList(QDir::Files | QDir::Hidden), (QStringList{"jpegtran", "photo.jpg"}));
}

void ViewerTests::savesJpegWithExistingExifOrientation()
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
        for (int turns = 1; turns <= 3; ++turns) {
            QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
            QCOMPARE(file.write(jpeg), jpeg.size());
            file.close();

            const auto photo = ImageIO::read(path, 0);
            QVERIFY2(photo.saveBlock.isEmpty(), qPrintable(photo.saveBlock));
            QVERIFY(!photo.image.isNull());
            const auto saved = ImageIO::save(path, photo, turns);
            QVERIFY2(saved.stamp.valid, qPrintable(saved.error));
            QCOMPARE(QImage(path), pixels);
            QCOMPARE(ImageIO::read(path, 0).image, ImageIO::rotate(photo.image, turns));
        }
    }
}

void ViewerTests::rejectsAnimatedAndUnknownPngChunks()
{
    QTemporaryDir dir;
    const auto path = dir.filePath("photo.png");
    QVERIFY(fixture().save(path));
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const auto plain = file.readAll();
    file.close();
    const auto unknown = QByteArray::fromHex("00000000767041670155c2b3");
    const auto animated = QByteArray::fromBase64(
        "iVBORw0KGgoAAAANSUhEUgAAAAIAAAACCAYAAABytg0kAAAACXBIWXMAAAAAAAAAAQCEeRdzAAAAIGNIUk0AAHomAACAhAAA"
        "+gAAAIDoAAB1MAAA6mAAADqYAAAXcJy6UTwAAAAIYWNUTAAAAAIAAAAA842TcAAAABpmY1RMAAAAAAAAAAIAAAACAAAAAAAA"
        "AAAAAQACAADmR424AAAAE0lEQVR4nGP8z8AARAwMLAxQAAAfKQIESEBLNwAAABpmY1RMAAAAAQAAAAIAAAACAAAAAAAAAAAA"
        "AQACAAB9NGdsAAAAF2ZkQVQAAAACeJxjZGD4/58BCFgYoAAAHSsCBHay9EoAAAAASUVORK5CYII=");
    const auto insertBefore = [&](const QByteArray &marker) {
        auto data = plain;
        data.insert(plain.indexOf(marker) - 4, unknown);
        return data;
    };
    for (const auto &bytes : {insertBefore("IDAT"), insertBefore("IEND"), animated}) {
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(file.write(bytes), bytes.size());
        file.close();
        const auto photo = ImageIO::read(path, 64);
        QVERIFY(!photo.image.isNull());
        const auto result = ImageIO::save(path, photo, 1);
        QCOMPARE(result.outcome, ImageIO::SaveOutcome::NotCommitted);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), bytes);
        file.close();
    }
}

void ViewerTests::preservesIndexedAndHighDepthPngPixels()
{
    QTemporaryDir dir;
    QImage indexed(16, 24, QImage::Format_Indexed8);
    indexed.setColorTable({qRgba(0, 0, 0, 0), qRgba(210, 72, 130, 255)});
    indexed.fill(0);
    indexed.setPixel(2, 3, 1);
    QImage highDepth(16, 24, QImage::Format_RGBA64);
    highDepth.fill(Qt::transparent);
    highDepth.setPixelColor(2, 3, QColor::fromRgba64(1234, 43210, 7777, 45000));
    const auto path = dir.filePath("photo.png");
    for (const auto &image : {indexed, highDepth}) {
        QVERIFY(image.save(path));
        const QImage original(path);
        const auto photo = ImageIO::read(path, 64);
        const auto result = ImageIO::save(path, photo, 1);
        QVERIFY2(result.committed(), qPrintable(result.error));
        const QImage rotated(path);
        QCOMPARE(rotated.size(), QSize(24, 16));
        QCOMPARE(rotated.pixelColor(20, 2), original.pixelColor(2, 3));
        QCOMPARE(rotated.pixelColor(0, 0).alpha(), 0);
    }
}

void ViewerTests::refusesTruncatedJpegHelperOutput()
{
    QTemporaryDir dir;
    const auto path = dir.filePath("photo.jpg");
    QVERIFY(fixture().save(path));
    QFile original(path);
    QVERIFY(original.open(QIODevice::ReadOnly));
    const auto bytes = original.readAll();
    original.close();

    QFile fake(dir.filePath("jpegtran"));
    QVERIFY(fake.open(QIODevice::WriteOnly));
    QVERIFY(fake.write("#!/bin/sh\nexec /usr/bin/head -c -2\n") > 0);
    fake.close();
    QVERIFY(fake.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
    const auto previousPath = qgetenv("PATH");
    const auto restore = qScopeGuard([&] { qputenv("PATH", previousPath); });
    qputenv("PATH", QFile::encodeName(dir.path()) + ':' + previousPath);
    const auto photo = ImageIO::read(path, 64);
    const auto result = ImageIO::save(path, photo, 2);
    QCOMPARE(result.outcome, ImageIO::SaveOutcome::NotCommitted);
    QVERIFY(result.error.contains("staged JPEG"));
    QVERIFY(original.open(QIODevice::ReadOnly));
    QCOMPARE(original.readAll(), bytes);
}

void ViewerTests::distinguishesSaveFailuresFromCommittedWarnings()
{
    QTemporaryDir dir;
    const auto path = dir.filePath("photo.png");
    QVERIFY(fixture().save(path));
    const auto source = ImageIO::read(path, 64);
    const auto original = QImage(path);

    ImageIO::CommitOps ops;
    const ImageIO::SaveOptions options{.commitOps = &ops};
    ops.syncStaged = [](int) { return EIO; };
    auto saved = ImageIO::save(path, source, 1, options);
    QCOMPARE(saved.outcome, ImageIO::SaveOutcome::NotCommitted);
    QCOMPARE(QImage(path), original);
    ops.syncStaged = {};
    ops.replace = [](int, const QByteArray &, const QByteArray &) { return EACCES; };
    saved = ImageIO::save(path, source, 1, options);
    QCOMPARE(saved.outcome, ImageIO::SaveOutcome::NotCommitted);
    QCOMPARE(QImage(path), original);
    ops.replace = {};
    ops.syncDirectory = [](int) { return ENOTSUP; };
    saved = ImageIO::save(path, source, 1, options);
    QCOMPARE(saved.outcome, ImageIO::SaveOutcome::Committed);
    QVERIFY(!saved.durabilityWarning.isEmpty());
    QCOMPARE(QImage(path), ImageIO::rotate(original, 1));

    auto updated = ImageIO::read(path, 64);
    ops.replace = [](int fd, const QByteArray &staged, const QByteArray &target) {
        return ::renameat(fd, staged.constData(), fd, target.constData()) == 0 ? EIO : errno;
    };
    saved = ImageIO::save(path, updated, 1, options);
    QCOMPARE(saved.outcome, ImageIO::SaveOutcome::Committed);
    QCOMPARE(QImage(path), ImageIO::rotate(original, 2));
    updated = ImageIO::read(path, 64);
    ops.replace = {};
    ops.verify = [](const QString &) { return ImageIO::Stamp{}; };
    saved = ImageIO::save(path, updated, 1, options);
    QCOMPARE(saved.outcome, ImageIO::SaveOutcome::Uncertain);
    QVERIFY(saved.reloadRequired);
    QCOMPARE(QImage(path), ImageIO::rotate(original, 3));
    QCOMPARE(QDir(dir.path()).entryList(QDir::Files | QDir::Hidden), QStringList{"photo.png"});
}

void ViewerTests::rejectsUnknownJpegMarkersAfterScan()
{
    QTemporaryDir dir;
    const auto path = dir.filePath("photo.jpg");
    QVERIFY(fixture().save(path));
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    auto bytes = file.readAll();
    file.close();
    const auto eoi = bytes.lastIndexOf(QByteArray::fromHex("ffd9"));
    QVERIFY(eoi > 0);
    bytes.insert(eoi, QByteArray::fromHex("ffe3000664617461"));
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(file.write(bytes), bytes.size());
    file.close();

    const auto source = ImageIO::read(path, 64);
    QVERIFY(!source.image.isNull());
    const auto saved = ImageIO::save(path, source, 1);
    QCOMPARE(saved.outcome, ImageIO::SaveOutcome::NotCommitted);
    QVERIFY(saved.error.contains("metadata"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), bytes);
}

void ViewerTests::detectsExternalEditAndRejectsLinks()
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

void ViewerTests::speculationSkipsLargePng()
{
    QTemporaryDir dir;
    const auto path = dir.filePath("large.png");
    QVERIFY(fixture(3000, 2000).save(path));
    QVERIFY(ImageIO::readSpeculative(path, 512).image.isNull());
    const auto foreground = ImageIO::read(path, 512);
    QCOMPARE(foreground.original, QSize(3000, 2000));
    QVERIFY(!foreground.image.isNull());
}
