#pragma once

#include <QFileSystemWatcher>
#include <QJsonObject>
#include <QReadWriteLock>
#include <QTranslator>

class Translator : public QTranslator
{
    Q_OBJECT

public:
    explicit Translator(QString languagePath = {}, QString catalogDirectory = ":/i18n");
    bool isEmpty() const override { return false; }
    QString translate(const char *, const char *source, const char *, int) const override;

signals:
    void changed();

private:
    void reload();
    QString m_path, m_catalogDirectory;
    mutable QReadWriteLock m_catalogLock;
    QJsonObject m_strings;
    QFileSystemWatcher m_watcher;
};
