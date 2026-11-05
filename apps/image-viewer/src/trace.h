#pragma once

#include <QDebug>
#include <QElapsedTimer>

inline void viewerTrace(const char *event, quint64 request, qint64 value = 0)
{
    static const bool enabled = qEnvironmentVariableIsSet("IMAGE_VIEWER_TRACE");
    if (!enabled)
        return;
    static const QElapsedTimer clock = [] {
        QElapsedTimer timer;
        timer.start();
        return timer;
    }();
    qInfo().noquote() << "viewer-trace" << clock.nsecsElapsed() << event << request << value;
}
