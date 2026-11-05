#include "translator.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QLocale>

Translator::Translator() : m_path(QDir::homePath() + "/.config/omarchy/language")
{
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, &Translator::reload);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, &Translator::reload);
    reload();
}

void Translator::reload()
{
    const QString parent = QFileInfo(m_path).absolutePath();
    if (QFileInfo::exists(parent) && !m_watcher.directories().contains(parent))
        m_watcher.addPath(parent);
    if (QFileInfo::exists(m_path) && !m_watcher.files().contains(m_path))
        m_watcher.addPath(m_path);

    QLocale locale = QLocale::system();
    QFile language(m_path);
    if (language.open(QIODevice::ReadOnly))
        locale = QLocale(QString::fromUtf8(language.readLine()).trimmed());
    QLocale::setDefault(locale);
    QGuiApplication::setLayoutDirection(locale.textDirection());

    QFile catalog(":/i18n/" + locale.name().section('_', 0, 0) + ".json");
    const QJsonObject strings = catalog.open(QIODevice::ReadOnly)
        ? QJsonDocument::fromJson(catalog.readAll()).object() : QJsonObject{};
    if (m_strings != strings) {
        m_strings = strings;
        emit changed();
    }
}

QString Translator::translate(const char *, const char *source, const char *, int) const
{
    return m_strings.value(QString::fromUtf8(source)).toString();
}
