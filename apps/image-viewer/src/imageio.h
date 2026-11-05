#pragma once

#include <QImage>
#include <QStringList>

namespace ImageIO {

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

struct Saved {
    Stamp stamp;
    QString error;
    bool reloadRequired = false;
};

Stamp stamp(const QString &path);
QStringList formats();
QStringList folderImages(const QString &directory, const QStringList &formats);
Photo read(const QString &path, int edge);
Saved save(const QString &path, const Photo &source, int turns);
QImage rotate(const QImage &image, int turns);

}
