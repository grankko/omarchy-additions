#pragma once

#include <QFileSystemWatcher>
#include <QJsonObject>
#include <QTranslator>

class Translator : public QTranslator
{
    Q_OBJECT

public:
    Translator();
    bool isEmpty() const override { return false; }
    QString translate(const char *, const char *source, const char *, int) const override;

signals:
    void changed();

private:
    void reload();
    QString m_path;
    QJsonObject m_strings;
    QFileSystemWatcher m_watcher;
};
