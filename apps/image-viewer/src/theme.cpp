#include "theme.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <cmath>

Theme::Theme(QString currentDirectory)
    : m_currentDirectory(currentDirectory.isEmpty() ? QDir::homePath() + "/.local/state/omarchy/current"
                                                    : currentDirectory),
      m_followCompositor(currentDirectory.isEmpty() && !qEnvironmentVariableIsEmpty("HYPRLAND_INSTANCE_SIGNATURE"))
{
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, &Theme::reload);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, &Theme::reload);
    connect(&m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        m_timeout.stop();
        const auto result = QJsonDocument::fromJson(m_process.readAllStandardOutput()).object();
        if (code == 0 && status == QProcess::NormalExit && result.value("int").isDouble()) {
            const int radius = qMax(0, result.value("int").toInt());
            if (m_radius != radius) {
                m_radius = radius;
                emit changed();
            }
        }
    });
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, &m_process, &QProcess::kill);
    if (m_followCompositor) {
        auto *poll = new QTimer(this);
        poll->setInterval(15000);
        connect(poll, &QTimer::timeout, this, &Theme::refreshRounding);
        poll->start();
    }
    reload();
}

QColor Theme::foreground() const
{
    const auto linear = [](qreal value) {
        return value <= .04045 ? value / 12.92 : std::pow((value + .055) / 1.055, 2.4);
    };
    const auto luminance = .2126 * linear(m_accent.redF()) + .7152 * linear(m_accent.greenF())
        + .0722 * linear(m_accent.blueF());
    return luminance > .179 ? Qt::black : Qt::white;
}

void Theme::refreshRounding()
{
    if (m_followCompositor && m_process.state() == QProcess::NotRunning) {
        m_process.start("hyprctl", {"-j", "getoption", "decoration:rounding"});
        m_timeout.start(750);
    }
}

void Theme::reload()
{
    const QString &current = m_currentDirectory;
    const QString filePath = current + "/theme/colors.toml";
    const auto old = m_watcher.directories() + m_watcher.files();
    if (!old.isEmpty())
        m_watcher.removePaths(old);
    for (const auto &path : {QFileInfo(current).absolutePath(), current, current + "/theme", filePath}) {
        if (QFileInfo::exists(path))
            m_watcher.addPath(path);
    }

    QColor accent("#ffd60a");
    QFile file(filePath);
    if (file.open(QIODevice::ReadOnly)) {
        static const QRegularExpression pattern(R"re(^\s*accent\s*=\s*["'](#[\da-fA-F]{6})["'])re",
                                                QRegularExpression::MultilineOption);
        const auto match = pattern.match(QString::fromUtf8(file.readAll()));
        if (match.hasMatch())
            accent = QColor(match.captured(1));
    }
    if (m_accent != accent) {
        m_accent = accent;
        emit changed();
    }
    refreshRounding();
}
