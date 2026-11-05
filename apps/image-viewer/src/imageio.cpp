#include "imageio.h"

#include <QCollator>
#include <QColorSpace>
#include <QCoreApplication>
#include <QDirListing>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QImageWriter>
#include <QMap>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QtEndian>

#include <algorithm>
#include <cerrno>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>

namespace ImageIO {
namespace {

QString message(const char *text)
{
    return QCoreApplication::translate("ImageIO", text);
}

Stamp fromStat(const struct stat &value)
{
    return {quint64(value.st_dev), quint64(value.st_ino), quint64(value.st_size),
            value.st_mtim.tv_sec * 1000000000LL + value.st_mtim.tv_nsec,
            value.st_ctim.tv_sec * 1000000000LL + value.st_ctim.tv_nsec, true};
}

bool writeStat(const QString &path, struct stat &value)
{
    const QByteArray name = QFile::encodeName(path);
    return lstat(name.constData(), &value) == 0 && S_ISREG(value.st_mode)
        && value.st_nlink == 1 && value.st_uid == geteuid()
        && (value.st_mode & (S_ISUID | S_ISGID | S_ISVTX)) == 0
        && (value.st_mode & (S_IWUSR | S_IWGRP | S_IWOTH)) != 0
        && access(name.constData(), W_OK) == 0;
}

// Reject PNG chunks whose semantics the Qt round trip cannot preserve.
bool plainPng(QFile &file, bool thorough)
{
    if (!file.seek(8))
        return false;
    static const QList<QByteArray> allowed = {
        "IHDR", "PLTE", "tRNS", "iCCP", "sRGB", "gAMA", "cHRM",
        "tEXt", "zTXt", "iTXt", "pHYs", "IDAT", "IEND"
    };
    while (true) {
        const QByteArray header = file.read(8);
        if (header.size() != 8 || !allowed.contains(header.mid(4)))
            return false;
        const quint32 size = qFromBigEndian<quint32>(header.constData());
        if (!thorough && header.endsWith("IDAT"))
            return true;
        if (header.endsWith("IEND"))
            return size == 0 && file.pos() + 4 == file.size();
        if (file.pos() + size + 4 > file.size() || !file.seek(file.pos() + size + 4))
            return false;
    }
}

// Qualify single-image JPEGs with standard metadata. Other APP markers may
// describe secondary images or contain independent orientation information.
bool supportedJpeg(QFile &file, bool &exif)
{
    if (!file.seek(2))
        return false;
    while (true) {
        if (file.read(1) != "\xff")
            return false;
        char marker;
        do {
            if (!file.getChar(&marker))
                return false;
        } while (uchar(marker) == 0xff);
        const uchar type = uchar(marker);
        const QByteArray length = file.read(2);
        if (length.size() != 2)
            return false;
        const quint16 size = qFromBigEndian<quint16>(length.constData());
        if (size < 2 || file.pos() + size - 2 > file.size())
            return false;
        if (type == 0xda)
            return true;
        if (type >= 0xe0 && type <= 0xef) {
            const QByteArray payload = file.read(size - 2);
            const bool jfif = type == 0xe0 && payload.size() == 14
                && payload.startsWith(QByteArray("JFIF\0", 5)) && payload.endsWith(QByteArray(2, '\0'));
            const bool icc = type == 0xe2 && payload.startsWith(QByteArray("ICC_PROFILE\0", 12));
            const bool adobe = type == 0xee && payload.size() == 12 && payload.startsWith("Adobe");
            const bool exifMarker = type == 0xe1 && payload.startsWith(QByteArray("Exif\0\0", 6));
            if ((!jfif && !icc && !adobe && !exifMarker) || (exifMarker && exif))
                return false;
            exif = exif || exifMarker;
        } else if (!file.seek(file.pos() + size - 2)) {
            return false;
        }
    }
}

QString saveBlock(const QString &path, const QByteArray &format, QFile &file, bool thorough = false)
{
    struct stat value;
    const auto parent = QFile::encodeName(QFileInfo(path).absolutePath());
    if (!writeStat(path, value) || access(parent.constData(), W_OK | X_OK) != 0)
        return message("This file is read-only or cannot be safely replaced.");
    const ssize_t attributes = flistxattr(file.handle(), nullptr, 0);
    if (attributes > 0 || (attributes < 0 && errno != ENOTSUP))
        return message("This file has filesystem attributes the viewer cannot preserve.");
    if (format == "png" && plainPng(file, thorough))
        return {};
    if (format == "jpeg") {
        bool exif = false;
        if (!supportedJpeg(file, exif))
            return message("This JPEG contains metadata the viewer cannot safely update.");
        if (QStandardPaths::findExecutable(exif ? "exiv2" : "jpegtran").isEmpty())
            return exif ? message("Install Exiv2 to save rotations for JPEGs with EXIF.")
                        : message("Install jpegtran to save JPEG rotations.");
        return {};
    }
    if (format == "png")
        return message("This PNG contains image data or metadata the viewer cannot safely preserve.");
    return message("Saving rotation is currently supported for JPEG and PNG.");
}

// Use the EXIF tag when rotating compressed blocks would crop the image or
// invalidate existing metadata. The stored pixels and thumbnail stay intact.
QString saveJpegOrientation(QFile &input, QTemporaryFile &output, int turns)
{
    const auto executable = QStandardPaths::findExecutable("exiv2");
    if (executable.isEmpty())
        return message("Install Exiv2 to save this rotation without cropping the JPEG.");
    if (!input.seek(0))
        return input.errorString();
    int orientation;
    {
        QImageReader reader(&input, "jpeg");
        constexpr int exifByTransform[] = {1, 2, 4, 3, 6, 7, 5, 8};
        orientation = exifByTransform[reader.transformation().toInt() & 7];
    }
    constexpr int clockwise[] = {0, 6, 7, 8, 5, 2, 3, 4, 1};
    for (int step = 0; step < turns; ++step)
        orientation = clockwise[orientation];

    if (!input.seek(0) || !output.resize(0) || !output.seek(0))
        return message("The JPEG could not be staged for saving.");
    while (!input.atEnd()) {
        const auto chunk = input.read(256 * 1024);
        if (chunk.isEmpty() || output.write(chunk) != chunk.size())
            return message("The JPEG could not be staged for saving.");
    }
    if (!output.flush())
        return output.errorString();

    QProcess metadata;
    metadata.start(executable, {"-M", QString("set Exif.Image.Orientation Short %1").arg(orientation),
                                "modify", output.fileName()});
    if (!metadata.waitForStarted() || !metadata.waitForFinished(-1)
        || metadata.exitStatus() != QProcess::NormalExit || metadata.exitCode() != 0
        || !metadata.readAllStandardError().trimmed().isEmpty()) {
        return message("The JPEG orientation metadata could not be safely updated.");
    }
    QImageReader check(output.fileName(), "jpeg");
    constexpr unsigned transformByExif[] = {0, 0, 1, 3, 2, 6, 4, 5, 7};
    if (check.transformation().toInt() != transformByExif[orientation])
        return message("The saved JPEG orientation could not be verified.");
    return {};
}

}

Stamp stamp(const QString &path)
{
    struct stat value;
    return stat(QFile::encodeName(path).constData(), &value) == 0 && S_ISREG(value.st_mode)
        ? fromStat(value) : Stamp{};
}

QStringList formats()
{
    QStringList result;
    for (const auto &format : QImageReader::supportedImageFormats())
        result << QString::fromLatin1(format);
    // These suffixes share decoders whose advertised names are more specific.
    if (result.contains("png"))
        result << "apng";
    if (result.contains("ppm"))
        result << "pnm";
    if (result.contains("j2k"))
        result << "j2c" << "jpc";
    return result;
}

QStringList folderImages(const QString &directory, const QStringList &suffixes)
{
    QStringList result;
    for (const auto &entry : QDirListing(directory, QDirListing::IteratorFlag::FilesOnly)) {
        if (suffixes.contains(entry.suffix().toLower()))
            result << entry.filePath();
    }
    // The C locale has no numeric collation, so use a stable locale for ordering.
    QCollator collator(QLocale(QLocale::English));
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(result.begin(), result.end(), [&collator](const QString &a, const QString &b) {
        const int order = collator.compare(a, b);
        return order == 0 ? a < b : order < 0;
    });
    return result;
}

Photo read(const QString &path, int edge)
{
    Photo result;
    result.stamp = stamp(path);
    QFile file(path);
    if (!result.stamp.valid || !file.open(QIODevice::ReadOnly)) {
        result.error = message("The image is missing or could not be opened.");
        return result;
    }
    struct stat opened;
    if (fstat(file.handle(), &opened) != 0 || fromStat(opened) != result.stamp) {
        result.error = message("The image changed while it was being opened. Try again.");
        return result;
    }

    {
        QImageReader reader(&file);
        reader.setAutoTransform(true);
        result.format = reader.format();
        const QSize raw = reader.size();
        result.original = raw;
        if (reader.transformation() & QImageIOHandler::TransformationRotate90)
            result.original.transpose();
        if (edge > 0 && raw.isValid() && qMax(raw.width(), raw.height()) > edge
            && reader.supportsOption(QImageIOHandler::ScaledSize)) {
            reader.setScaledSize(raw.scaled(QSize(edge, edge), Qt::KeepAspectRatio));
        }
        result.image = reader.read();
        if (result.image.isNull())
            result.error = reader.errorString();
    }
    if (result.image.isNull())
        return result;
    if (!result.original.isValid())
        result.original = result.image.size();
    result.saveBlock = saveBlock(path, result.format, file);
    if (stamp(path) != result.stamp) {
        result.image = {};
        result.error = message("The image changed while it was being opened. Try again.");
        return result;
    }
    if (edge > 0 && qMax(result.image.width(), result.image.height()) > edge)
        result.image = result.image.scaled(edge, edge, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (edge > 0 && result.image.colorSpace().isValid() && result.image.colorSpace() != QColorSpace::SRgb)
        result.image.convertToColorSpace(QColorSpace::SRgb);
    return result;
}

QImage rotate(const QImage &image, int turns)
{
    if (turns == 0 || image.isNull())
        return image;
    QImage rotated = image.transformed(QTransform().rotate(turns * 90));
    if (turns % 2) {
        rotated.setDotsPerMeterX(image.dotsPerMeterY());
        rotated.setDotsPerMeterY(image.dotsPerMeterX());
    }
    return rotated;
}

Saved save(const QString &path, const Photo &source, int turns)
{
    if (!source.stamp.valid || turns < 1 || turns > 3)
        return {{}, message("There is no rotation to save.")};
    struct stat original;
    if (!writeStat(path, original) || fromStat(original) != source.stamp)
        return {{}, message("The file changed or can no longer be written. Reload it before saving."), true};

    QFile input(path);
    struct stat opened;
    if (!input.open(QIODevice::ReadOnly) || fstat(input.handle(), &opened) != 0
        || fromStat(opened) != source.stamp)
        return {{}, message("The file changed or could not be reopened."), true};
    const QString blocked = saveBlock(path, source.format, input, true);
    if (!blocked.isEmpty())
        return {{}, blocked};

    QTemporaryFile output(QFileInfo(path).absolutePath() + "/.image-viewer-XXXXXX");
    if (!output.open())
        return {{}, output.errorString()};

    if (source.format == "jpeg") {
        bool exif = false;
        if (!supportedJpeg(input, exif))
            return {{}, message("This JPEG contains metadata the viewer cannot safely update.")};
        if (!exif) {
            QProcess jpegtran;
            auto environment = QProcessEnvironment::systemEnvironment();
            environment.insert("LC_ALL", "C");
            jpegtran.setProcessEnvironment(environment);
            // Read the already-open inode so the transform cannot use a replacement path.
            jpegtran.setStandardInputFile(QStringLiteral("/proc/%1/fd/%2")
                .arg(QCoreApplication::applicationPid()).arg(input.handle()));
            jpegtran.setStandardOutputFile(output.fileName());
            jpegtran.start(QStandardPaths::findExecutable("jpegtran"),
                           {"-strict", "-copy", "all", "-perfect", "-rotate", QString::number(turns * 90)});
            if (!jpegtran.waitForStarted() || !jpegtran.waitForFinished(-1)
                || jpegtran.exitStatus() != QProcess::NormalExit) {
                return {{}, message("The JPEG rotation could not be completed.")};
            }
            if (jpegtran.exitCode() != 0) {
                if (!jpegtran.readAllStandardError().contains("transformation is not perfect"))
                    return {{}, message("The JPEG rotation could not be completed.")};
                exif = true;
            }
        }
        if (exif) {
            const QString error = saveJpegOrientation(input, output, turns);
            if (!error.isEmpty())
                return {{}, error};
        }
    } else {
        QColorSpace profile;
        QSize density;
        QMap<QString, QString> text;
        {
            input.seek(0);
            QImageReader reader(&input, source.format);
            reader.setAutoTransform(true);
            const QImage image = rotate(reader.read(), turns);
            QImageWriter writer(&output, source.format);
            if (image.isNull() || !writer.write(image))
                return {{}, image.isNull() ? reader.errorString() : writer.errorString()};
            if (!output.flush())
                return {{}, output.errorString()};

            profile = image.colorSpace();
            density = {image.dotsPerMeterX(), image.dotsPerMeterY()};
            for (const auto &key : image.textKeys())
                text.insert(key, image.text(key));
        }
        // Release the full-size encode buffer before verifying the PNG.
        QImageReader check(output.fileName(), source.format);
        const auto written = check.read();
        if (written.isNull() || written.colorSpace() != profile
            || written.dotsPerMeterX() != density.width() || written.dotsPerMeterY() != density.height()
            || written.textKeys() != text.keys()) {
            return {{}, message("The image metadata could not be preserved. The file was not changed.")};
        }
        for (auto it = text.cbegin(); it != text.cend(); ++it) {
            if (written.text(it.key()) != it.value())
                return {{}, message("The image metadata could not be preserved. The file was not changed.")};
        }
    }

    QImageReader check(output.fileName(), source.format);
    const QSize expected = turns % 2 ? source.original.transposed() : source.original;
    QSize dimensions = check.size();
    if (check.transformation() & QImageIOHandler::TransformationRotate90)
        dimensions.transpose();
    if (dimensions != expected)
        return {{}, message("The replacement image has an unexpected size. The file was not changed.")};

    // The metadata helper may replace the staged inode while writing EXIF.
    QFile staged(output.fileName());
    if (!staged.open(QIODevice::ReadWrite))
        return {{}, staged.errorString()};
    struct stat replacement;
    const ssize_t attributes = flistxattr(staged.handle(), nullptr, 0);
    const bool unexpectedAttributes = attributes > 0 || (attributes < 0 && errno != ENOTSUP);
    if (fstat(staged.handle(), &replacement) != 0 || replacement.st_size == 0
        || unexpectedAttributes
        || (replacement.st_gid != original.st_gid
            && fchown(staged.handle(), original.st_uid, original.st_gid) != 0)
        || fchmod(staged.handle(), original.st_mode & 0777) != 0 || fsync(staged.handle()) != 0)
        return {{}, message("The replacement could not be written safely.")};
    if (!writeStat(path, opened) || fromStat(opened) != source.stamp)
        return {{}, message("The file changed before saving. Reload it and try again."), true};

    const auto temporary = QFile::encodeName(output.fileName());
    const auto target = QFile::encodeName(path);
    const int failure = ::rename(temporary.constData(), target.constData()) == 0 ? 0 : errno;
    if (!failure)
        output.setAutoRemove(false);
    const Stamp after = stamp(path);
    if (after.valid && after.device == quint64(replacement.st_dev)
        && after.inode == quint64(replacement.st_ino) && after.size == quint64(replacement.st_size)) {
        output.setAutoRemove(false);
        return {after, {}};
    }
    if (after == source.stamp && (failure == EACCES || failure == EPERM || failure == EROFS
                                || failure == ENOSPC || failure == EDQUOT || failure == ENOTSUP)) {
        return {{}, message("The filesystem could not replace the file.")};
    }
    return {{}, message("The save result could not be verified. Reload the file before saving again."), true};
}

}
