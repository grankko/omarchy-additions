#include "instance.h"

#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDir>
#include <QUrl>
#include <utility>

namespace {
constexpr auto Service = "io.github.grankko.OmarchyImageViewer";
constexpr auto Object = "/io/github/grankko/OmarchyImageViewer";
}

Instance::Instance(QObject *parent) : QObject(parent), m_bus(QDBusConnection::sessionBus()) {}

Instance::~Instance()
{
    stop();
    m_bus.unregisterObject(Object);
}

QString Instance::imagePath(const QString &input, QString &error)
{
    if (input.isEmpty())
        return {};
    QString path = input;
    if (input.startsWith("file:")) {
        QUrl url(input);
        if (!url.isValid() || !url.isLocalFile() || (!url.host().isEmpty() && url.host() != "localhost")) {
            error = "Specify a local image file.";
            return {};
        }
        url.setHost({});
        path = url.toLocalFile();
    } else if (!QDir::isAbsolutePath(input) && input.contains("://")) {
        error = "Specify a local image file.";
        return {};
    }
    // Resolving against the sender's directory does not stat a possibly blocked mount.
    path = QDir::cleanPath(QDir::current().absoluteFilePath(path));
    if (path.size() > 65536 || path.contains(QChar::Null)) {
        error = "The image path is invalid.";
        return {};
    }
    return path;
}

Instance::Result Instance::start(const QString &path, const QString &token)
{
    if (!m_bus.isConnected() || !m_bus.registerObject(Object, this, QDBusConnection::ExportAllSlots)) {
        m_error = "The desktop session bus is unavailable.";
        return Result::Failed;
    }
    auto *bus = m_bus.interface();
    for (int attempt = 0; attempt < 2; ++attempt) {
        const auto registration = bus->registerService(Service, QDBusConnectionInterface::DontQueueService,
                                                       QDBusConnectionInterface::DontAllowReplacement);
        if (registration.isValid() && registration.value() == QDBusConnectionInterface::ServiceRegistered) {
            m_owner = true;
            m_pending = Request{path, token};
            return Result::Owner;
        }
        const auto owner = bus->serviceOwner(Service);
        if (!owner.isValid() || owner.value().isEmpty())
            continue;
        auto call = QDBusMessage::createMethodCall(owner.value(), Object, Service, "Open");
        call << uint(1) << path << token;
        const QDBusReply<bool> reply = m_bus.call(call, QDBus::Block, 5000);
        if (reply.isValid() && reply.value())
            return Result::Forwarded;
        // An unresponsive owner must not cause a second GUI to appear.
        if (bus->isServiceRegistered(Service).value()) {
            m_error = "The running Image Viewer could not accept this request. Close it and try again.";
            return Result::Failed;
        }
    }
    m_error = "Image Viewer could not acquire its desktop session service. Try again.";
    return Result::Failed;
}

bool Instance::Open(uint protocol, const QString &path, const QString &token)
{
    if (m_stopping || protocol != 1 || path.size() > 65536 || token.size() > 4096
        || path.contains(QChar::Null) || token.contains(QChar::Null)
        || (!path.isEmpty() && !QDir::isAbsolutePath(path))) {
        if (calledFromDBus())
            sendErrorReply(QDBusError::InvalidArgs, "Unsupported viewer request.");
        return false;
    }
    if (m_ready)
        emit openRequested(path, token);
    else
        m_pending = Request{path, token};
    return true;
}

void Instance::setReady()
{
    m_ready = true;
    if (m_pending) {
        const auto request = std::exchange(m_pending, std::nullopt);
        emit openRequested(request->path, request->token);
    }
}

void Instance::stop()
{
    m_stopping = true;
    if (m_owner) {
        m_bus.unregisterService(Service);
        m_owner = false;
    }
}
