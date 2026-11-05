#pragma once

#include <QColor>
#include <QFileSystemWatcher>
#include <QObject>
#include <QProcess>
#include <QTimer>

class Theme : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QColor accent MEMBER m_accent NOTIFY changed)
    Q_PROPERTY(QColor foreground READ foreground NOTIFY changed)
    Q_PROPERTY(int radius MEMBER m_radius NOTIFY changed)

public:
    Theme();
    QColor foreground() const;

signals:
    void changed();

private:
    void reload();
    void refreshRounding();

    QFileSystemWatcher m_watcher;
    QProcess m_process;
    QTimer m_timeout;
    QColor m_accent = QColor("#ffd60a");
    int m_radius = 0;
};
