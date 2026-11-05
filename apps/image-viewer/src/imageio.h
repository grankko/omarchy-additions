#pragma once

#include <QImage>
#include <QStringList>
#include <atomic>
#include <memory>
#include <functional>

namespace ImageIO {

inline constexpr qint64 DetailLimitBytes = 64 * 1024 * 1024;

struct Stamp {
    quint64 device = 0, inode = 0, size = 0;
    qint64 modified = 0, changed = 0;
    bool valid = false;
    bool operator==(const Stamp &) const = default;
};

struct Photo {
    QImage image;
    QSize original;
    Stamp stamp;
    QString error;
    QString saveBlock;
    QByteArray format;
};

enum class SaveOutcome { NotCommitted, Committed, Uncertain };

struct Saved {
    Stamp stamp;
    QString error;
    bool reloadRequired = false;
    SaveOutcome outcome = SaveOutcome::NotCommitted;
    QString durabilityWarning = {};

    bool committed() const { return outcome == SaveOutcome::Committed; }
};

Stamp stamp(const QString &path);
QStringList formats();
struct FolderSnapshot {
    QStringList entries;
    QString error;
};

FolderSnapshot folderImages(const QString &directory, const QStringList &formats,
                            const std::atomic_bool *stop = nullptr);
qsizetype folderInsertionIndex(const QStringList &entries, const QString &path);
Photo read(const QString &path, int edge);
Photo readCancellable(const QString &path, int edge, const std::atomic_bool *stop);
Photo readSpeculative(const QString &path, int edge, const std::atomic_bool *stop = nullptr);
int boundedDetailEdge(const QSize &source, int requested);
class SaveControl
{
public:
    bool cancel();
    bool cancelled() const;
    bool beginCommit();

private:
    enum class Phase { Preparing, Cancelled, Committing };
    std::atomic<Phase> m_phase{Phase::Preparing};
};

struct CommitOps {
    std::function<int(int)> syncStaged;
    std::function<int(int, const QByteArray &, const QByteArray &)> replace;
    std::function<Stamp(const QString &)> verify;
    std::function<int(int)> syncDirectory;
};

struct SaveOptions {
    std::shared_ptr<SaveControl> control = {};
    int helperTimeoutMs = 60000;
    const CommitOps *commitOps = nullptr;
};

Saved save(const QString &path, const Photo &source, int turns, const SaveOptions &options = {});
QImage rotate(const QImage &image, int turns);

}
