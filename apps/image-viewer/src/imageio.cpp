#include "imageio.h"

#include <QCollator>
#include <QColorSpace>
#include <QCoreApplication>
#include <QDir>
#include <QDeadlineTimer>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QImageWriter>
#include <QMap>
#include <QProcess>
#include <QProcessEnvironment>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QtEndian>

#include <algorithm>
#include <cerrno>
#include <dirent.h>
#include <fcntl.h>
#include <memory>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>

namespace ImageIO {
namespace {

QString message(const char *text)
{
    // Workers return source keys. The GUI translates them using the active catalog.
    return QString::fromUtf8(text);
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

// Find the next marker after entropy-coded data. Stuffed bytes and restart
// markers are part of the scan. Read in blocks to avoid per-byte filesystem I/O.
int jpegMarker(QFile &file, bool entropy)
{
    if (entropy) {
        bool prefix = false;
        while (true) {
            const qint64 start = file.pos();
            const auto block = file.read(64 * 1024);
            if (block.isEmpty())
                return -1;
            for (qsizetype offset = 0; offset < block.size(); ++offset) {
                const uchar byte = uchar(block.at(offset));
                if (!prefix) {
                    prefix = byte == 0xff;
                } else if (byte != 0xff) {
                    prefix = false;
                    if (byte != 0 && (byte < 0xd0 || byte > 0xd7))
                        return file.seek(start + offset + 1) ? byte : -1;
                }
            }
        }
    }
    if (file.read(1) != "\xff")
        return -1;
    char marker;
    do {
        if (!file.getChar(&marker))
            return -1;
    } while (uchar(marker) == 0xff);
    return uchar(marker);
}

// Qualify single-image JPEGs with standard metadata. Other APP markers may
// describe secondary images or contain independent orientation information.
bool supportedJpeg(QFile &file, bool &exif, bool thorough = false, qint64 *densityOffset = nullptr)
{
    if (!file.seek(2))
        return false;
    bool entropy = false;
    bool hasScan = false;
    bool hasJfif = false;
    while (true) {
        const int type = jpegMarker(file, entropy);
        entropy = false;
        if (type == 0xd9)
            return hasScan && file.pos() == file.size();
        if (type <= 0 || type == 0xd8)
            return false;
        const QByteArray length = file.read(2);
        if (length.size() != 2)
            return false;
        const quint16 size = qFromBigEndian<quint16>(length.constData());
        if (size < 2 || file.pos() + size - 2 > file.size())
            return false;
        if (type == 0xda) {
            if (!thorough)
                return true;
            hasScan = true;
            entropy = true;
        }
        if (type >= 0xe0 && type <= 0xef) {
            if (hasScan)
                return false;
            const QByteArray payload = file.read(size - 2);
            const bool jfif = type == 0xe0 && payload.size() == 14
                && payload.startsWith(QByteArray("JFIF\0", 5)) && payload.endsWith(QByteArray(2, '\0'));
            const bool icc = type == 0xe2 && payload.startsWith(QByteArray("ICC_PROFILE\0", 12));
            const bool adobe = type == 0xee && payload.size() == 12 && payload.startsWith("Adobe");
            const bool exifMarker = type == 0xe1 && payload.startsWith(QByteArray("Exif\0\0", 6));
            if ((!jfif && !icc && !adobe && !exifMarker) || (exifMarker && exif) || (jfif && hasJfif))
                return false;
            if (jfif && densityOffset)
                *densityOffset = file.pos() - payload.size() + 8;
            hasJfif = hasJfif || jfif;
            exif = exif || exifMarker;
        } else if (!file.seek(file.pos() + size - 2)) {
            return false;
        }
    }
}

QString saveBlock(const QString &path, const QByteArray &format, QFile &file, bool thorough = false,
                  bool *jpegHasExif = nullptr)
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
        if (!supportedJpeg(file, exif, thorough))
            return message("This JPEG contains data or metadata the viewer cannot safely update.");
        if (jpegHasExif)
            *jpegHasExif = exif;
        if (QStandardPaths::findExecutable(exif ? "exiv2" : "jpegtran").isEmpty())
            return exif ? message("Install Exiv2 to save rotations for JPEGs with EXIF.")
                        : message("Install jpegtran to save JPEG rotations.");
        return {};
    }
    if (format == "png")
        return message("This PNG contains image data or metadata the viewer cannot safely preserve.");
    return message("Saving rotation is currently supported for JPEG and PNG.");
}

enum class HelperOutcome { Success, StartFailed, TimedOut, Cancelled, Crashed, Failed };

struct HelperResult {
    HelperOutcome outcome;
    QByteArray diagnostics;
};

bool cancelled(const SaveOptions &options)
{
    return options.control && options.control->cancelled();
}

HelperResult runHelper(QProcess &process, const QString &program, const QStringList &arguments,
                       const SaveOptions &options)
{
    constexpr int pollMs = 50;
    constexpr int terminateGraceMs = 250;
    constexpr qsizetype diagnosticLimit = 64 * 1024;
    QByteArray diagnostics;
    const auto drain = [&] {
        diagnostics.append(process.readAllStandardError());
        if (diagnostics.size() > diagnosticLimit)
            diagnostics = diagnostics.last(diagnosticLimit);
    };
    if (cancelled(options))
        return {HelperOutcome::Cancelled, {}};
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("LC_ALL", "C");
    process.setProcessEnvironment(environment);
    process.start(program, arguments);
    const QDeadlineTimer deadline(options.helperTimeoutMs);
    while (process.state() != QProcess::NotRunning) {
        if (cancelled(options) || deadline.hasExpired()) {
            process.terminate();
            if (!process.waitForFinished(terminateGraceMs) && process.state() != QProcess::NotRunning) {
                process.kill();
                process.waitForFinished(terminateGraceMs);
            }
            return {cancelled(options) ? HelperOutcome::Cancelled : HelperOutcome::TimedOut, {}};
        }
        process.waitForFinished(int(qMin<qint64>(pollMs, deadline.remainingTime())));
        drain();
    }
    drain();
    if (process.error() == QProcess::FailedToStart)
        return {HelperOutcome::StartFailed, diagnostics};
    if (process.exitStatus() != QProcess::NormalExit)
        return {HelperOutcome::Crashed, diagnostics};
    return {process.exitCode() == 0 ? HelperOutcome::Success : HelperOutcome::Failed, diagnostics};
}

QString helperError(HelperOutcome outcome, const char *fallback)
{
    if (outcome == HelperOutcome::TimedOut)
        return message("The JPEG helper timed out. The file was not changed.");
    if (outcome == HelperOutcome::Cancelled)
        return message("Save cancelled. The file was not changed.");
    return message(fallback);
}

// Use the EXIF tag when rotating compressed blocks would crop the image or
// invalidate existing metadata. The stored pixels and thumbnail stay intact.
QString saveJpegOrientation(QFile &input, QTemporaryFile &output, int turns, const SaveOptions &options)
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
        if (cancelled(options))
            return helperError(HelperOutcome::Cancelled, "");
        const auto chunk = input.read(256 * 1024);
        if (chunk.isEmpty() || output.write(chunk) != chunk.size())
            return message("The JPEG could not be staged for saving.");
    }
    if (!output.flush())
        return output.errorString();

    QProcess metadata;
    metadata.setStandardOutputFile(QProcess::nullDevice());
    const auto result = runHelper(metadata, executable,
        {"-M", QString("set Exif.Image.Orientation Short %1").arg(orientation), "modify", output.fileName()}, options);
    if (result.outcome != HelperOutcome::Success || !result.diagnostics.trimmed().isEmpty())
        return helperError(result.outcome, "The JPEG orientation metadata could not be safely updated.");
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

namespace {
auto folderOrder()
{
    // The C locale has no numeric collation, so use an explicit locale.
    QCollator collator(QLocale(QLocale::English));
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    return [collator](const QString &a, const QString &b) {
        const int order = collator.compare(a, b);
        return order == 0 ? a < b : order < 0;
    };
}
}

FolderSnapshot folderImages(const QString &directory, const QStringList &suffixes,
                            const std::atomic_bool *stop)
{
    const QDir folder(directory);
    const auto close = [](DIR *stream) { closedir(stream); };
    const std::unique_ptr<DIR, decltype(close)> stream(opendir(QFile::encodeName(directory).constData()), close);
    if (!stream)
        return {{}, message("The folder could not be opened.")};

    FolderSnapshot result;
    while (!stop || !stop->load()) {
        errno = 0;
        const dirent *entry = readdir(stream.get());
        if (!entry) {
            if (errno)
                return {{}, message("The folder could not be read completely.")};
            break;
        }
        if (entry->d_name[0] == '.')
            continue;
        const QString path = folder.filePath(QFile::decodeName(entry->d_name));
        const QFileInfo info(path);
        if (suffixes.contains(info.suffix().toLower()) &&
            (entry->d_type == DT_REG || ((entry->d_type == DT_UNKNOWN || entry->d_type == DT_LNK) && info.isFile()))) {
            result.entries << path;
        }
    }
    if (stop && stop->load())
        return {};
    std::sort(result.entries.begin(), result.entries.end(), folderOrder());
    return result;
}

qsizetype folderInsertionIndex(const QStringList &entries, const QString &path)
{
    return std::lower_bound(entries.cbegin(), entries.cend(), path, folderOrder()) - entries.cbegin();
}

namespace {
constexpr qint64 speculativeBytes = 64 * 1024 * 1024;
constexpr int maximumBytesPerPixel = 16;
constexpr int maximumDetailEdge = 8192;

bool withinBudget(const QSize &size, qint64 budget)
{
    return size.width() > 0 && size.height() > 0
        && size.width() <= budget / maximumBytesPerPixel / size.height();
}

Photo readPhoto(const QString &path, int edge, bool speculative, const std::atomic_bool *stop)
{
    if (stop && stop->load())
        return {};
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
        if (speculative && !withinBudget(raw, speculativeBytes)
            && !(raw.isValid() && result.format == "jpeg" && reader.supportsOption(QImageIOHandler::ScaledSize))) {
            return result;
        }
        result.original = raw;
        if (reader.transformation() & QImageIOHandler::TransformationRotate90)
            result.original.transpose();
        if (edge > 0 && raw.isValid() && qMax(raw.width(), raw.height()) > edge
            && reader.supportsOption(QImageIOHandler::ScaledSize)) {
            reader.setScaledSize(raw.scaled(QSize(edge, edge), Qt::KeepAspectRatio));
        }
        if (stop && stop->load())
            return {};
        result.image = reader.read();
        if (result.image.isNull())
            result.error = reader.errorString();
    }
    if (stop && stop->load())
        return {};
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
    if (stop && stop->load())
        return {};
    if (edge > 0 && qMax(result.image.width(), result.image.height()) > edge)
        result.image = result.image.scaled(edge, edge, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (edge > 0 && result.image.colorSpace().isValid() && result.image.colorSpace() != QColorSpace::SRgb)
        result.image.convertToColorSpace(QColorSpace::SRgb);
    return result;
}

}

Photo read(const QString &path, int edge)
{
    return readPhoto(path, edge, false, nullptr);
}

Photo readCancellable(const QString &path, int edge, const std::atomic_bool *stop)
{
    return readPhoto(path, edge, false, stop);
}

Photo readSpeculative(const QString &path, int edge, const std::atomic_bool *stop)
{
    return readPhoto(path, edge, true, stop);
}

int boundedDetailEdge(const QSize &source, int requested)
{
    if (source.isEmpty() || requested <= 0)
        return 0;
    int low = 1;
    int high = std::min({requested, qMax(source.width(), source.height()), maximumDetailEdge});
    while (low < high) {
        const int edge = low + (high - low + 1) / 2;
        const QSize scaled = source.scaled(edge, edge, Qt::KeepAspectRatio).expandedTo({1, 1});
        if (withinBudget(scaled, DetailLimitBytes))
            low = edge;
        else
            high = edge - 1;
    }
    return low;
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

bool SaveControl::cancel()
{
    Phase expected = Phase::Preparing;
    return m_phase.compare_exchange_strong(expected, Phase::Cancelled) || expected == Phase::Cancelled;
}

bool SaveControl::cancelled() const
{
    return m_phase.load() == Phase::Cancelled;
}

bool SaveControl::beginCommit()
{
    Phase expected = Phase::Preparing;
    return m_phase.compare_exchange_strong(expected, Phase::Committing);
}

Saved save(const QString &path, const Photo &source, int turns, const SaveOptions &options)
{
    if (cancelled(options))
        return {{}, helperError(HelperOutcome::Cancelled, "")};
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
    bool exif = false;
    const QString blocked = saveBlock(path, source.format, input, true, &exif);
    if (!blocked.isEmpty())
        return {{}, blocked};

    const QFileInfo targetInfo(path);
    const auto parentPath = QFile::encodeName(targetInfo.absolutePath());
    const int directory = ::open(parentPath.constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (directory < 0)
        return {{}, message("The containing folder could not be opened for safe replacement.")};
    const auto closeDirectory = qScopeGuard([directory] { ::close(directory); });
    const CommitOps defaults;
    const CommitOps &ops = options.commitOps ? *options.commitOps : defaults;
    const auto sync = [](int fd) { return fsync(fd) == 0 ? 0 : errno; };

    QTemporaryFile output(targetInfo.absolutePath() + "/.image-viewer-XXXXXX");
    if (!output.open())
        return {{}, output.errorString()};

    if (source.format == "jpeg") {
        if (!exif) {
            QProcess jpegtran;
            // Read the already-open inode so the transform cannot use a replacement path.
            jpegtran.setStandardInputFile(QStringLiteral("/proc/%1/fd/%2")
                .arg(QCoreApplication::applicationPid()).arg(input.handle()));
            jpegtran.setStandardOutputFile(output.fileName());
            const auto result = runHelper(jpegtran, QStandardPaths::findExecutable("jpegtran"),
                {"-strict", "-copy", "all", "-perfect", "-rotate", QString::number(turns * 90)}, options);
            if (result.outcome != HelperOutcome::Success) {
                if (result.outcome != HelperOutcome::Failed
                    || !result.diagnostics.contains("transformation is not perfect")) {
                    return {{}, helperError(result.outcome, "The JPEG rotation could not be completed.")};
                }
                exif = true;
            }
        }
        if (exif) {
            const QString error = saveJpegOrientation(input, output, turns, options);
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

    if (cancelled(options))
        return {{}, helperError(HelperOutcome::Cancelled, "")};
    // The metadata helper may replace the staged inode while writing EXIF.
    QFile staged(output.fileName());
    if (!staged.open(QIODevice::ReadWrite))
        return {{}, staged.errorString()};
    if (source.format == "jpeg") {
        bool stagedExif = false;
        qint64 densityOffset = -1;
        if (!supportedJpeg(staged, stagedExif, true, &densityOffset))
            return {{}, message("The staged JPEG could not be verified.")};
        if (!exif && turns % 2 && densityOffset >= 0) {
            // jpegtran rotates pixels but leaves the JFIF density fields in place.
            if (!staged.seek(densityOffset))
                return {{}, message("The JPEG pixel density could not be updated.")};
            const auto density = staged.read(4);
            if (density.size() != 4 || !staged.seek(densityOffset)
                || staged.write(density.sliced(2) + density.first(2)) != 4 || !staged.flush()) {
                return {{}, message("The JPEG pixel density could not be updated.")};
            }
        }
    }
    QImageReader check(output.fileName(), source.format);
    const QSize expected = turns % 2 ? source.original.transposed() : source.original;
    QSize dimensions = check.size();
    if (check.transformation() & QImageIOHandler::TransformationRotate90)
        dimensions.transpose();
    if (dimensions != expected)
        return {{}, message("The replacement image has an unexpected size. The file was not changed.")};
    if (source.format == "jpeg") {
        const QSize raw = check.size();
        if (qMax(raw.width(), raw.height()) > 512)
            check.setScaledSize(raw.scaled(512, 512, Qt::KeepAspectRatio));
        if (check.read().isNull())
            return {{}, message("The staged JPEG could not be verified.")};
    }
    struct stat replacement;
    const ssize_t attributes = flistxattr(staged.handle(), nullptr, 0);
    const bool unexpectedAttributes = attributes > 0 || (attributes < 0 && errno != ENOTSUP);
    if (fstat(staged.handle(), &replacement) != 0 || replacement.st_size == 0
        || unexpectedAttributes
        || (replacement.st_gid != original.st_gid
            && fchown(staged.handle(), original.st_uid, original.st_gid) != 0)
        || fchmod(staged.handle(), original.st_mode & 0777) != 0
        || (ops.syncStaged ? ops.syncStaged(staged.handle()) : sync(staged.handle())) != 0
        || fstat(staged.handle(), &replacement) != 0)
        return {{}, message("The replacement could not be written safely.")};
    if (!writeStat(path, opened) || fromStat(opened) != source.stamp)
        return {{}, message("The file changed before saving. Reload it and try again."), true};

    const auto temporary = QFile::encodeName(QFileInfo(output.fileName()).fileName());
    const auto target = QFile::encodeName(targetInfo.fileName());
    struct stat inDirectory;
    if (fstatat(directory, target.constData(), &inDirectory, AT_SYMLINK_NOFOLLOW) != 0
        || fromStat(inDirectory) != source.stamp
        || fstatat(directory, temporary.constData(), &inDirectory, AT_SYMLINK_NOFOLLOW) != 0
        || fromStat(inDirectory) != fromStat(replacement)) {
        return {{}, message("The source folder changed before saving. Reload the image and try again."), true};
    }
    if (options.control && !options.control->beginCommit())
        return {{}, helperError(HelperOutcome::Cancelled, "")};
    const int failure = ops.replace ? ops.replace(directory, temporary, target)
        : (::renameat(directory, temporary.constData(), directory, target.constData()) == 0 ? 0 : errno);
    if (!failure)
        output.setAutoRemove(false);
    const Stamp after = ops.verify ? ops.verify(path) : stamp(path);
    if (after.valid && after.device == quint64(replacement.st_dev)
        && after.inode == quint64(replacement.st_ino) && after.size == quint64(replacement.st_size)) {
        output.setAutoRemove(false);
        const int syncFailure = ops.syncDirectory ? ops.syncDirectory(directory) : sync(directory);
        QString warning;
        if (syncFailure == ENOTSUP || syncFailure == EINVAL || syncFailure == ENOSYS)
            warning = message("Saved. This filesystem does not support syncing the containing folder.");
        else if (syncFailure)
            warning = message("Saved, but the containing folder could not be synced.");
        return {after, {}, false, SaveOutcome::Committed, warning};
    }
    if (after == source.stamp && (failure == EACCES || failure == EPERM || failure == EROFS
                                || failure == ENOSPC || failure == EDQUOT || failure == ENOTSUP)) {
        return {{}, message("The filesystem could not replace the file.")};
    }
    return {{}, message("The save result could not be verified. Reload the file before saving again."), true,
            SaveOutcome::Uncertain};
}

}
