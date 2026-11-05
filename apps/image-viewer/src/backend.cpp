#include "backend.h"

#include <QClipboard>
#include <QDateTime>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QProcess>
#include <QStandardPaths>
#include <QUuid>
#include <QtConcurrentRun>
#include <algorithm>

namespace {
constexpr auto Portal = "org.freedesktop.portal.Desktop";
constexpr auto Request = "org.freedesktop.portal.Request";
}

Backend::Backend(QObject *parent)
    : QObject(parent), m_localSend(QStandardPaths::findExecutable("localsend")),
      m_formats(ImageIO::formats())
{
    if (m_localSend.isEmpty())
        m_localSend = QStandardPaths::findExecutable("localsend_app");

    connect(&m_loader, &QFutureWatcherBase::finished, this, [this] {
        const Load job = *m_reading;
        m_reading.reset();
        const auto result = m_loader.future().takeResult();
        // The visible photo and requested photo can differ while I/O is pending.
        if (job.serial == m_serial) {
            if (job.detail) {
                if (!busy() && result.stamp == m_photo.stamp && !result.image.isNull()
                    && result.image.width() >= m_photo.image.width()) {
                    m_photo.image = result.image;
                    emit frameChanged();
                }
            } else if (result.image.isNull()) {
                m_loading = false;
                m_target = m_path;
                m_status = tr("%1 — %2").arg(QFileInfo(job.path).fileName(), result.error);
                clearCache();
                if (!m_path.isEmpty())
                    m_directory = QFileInfo(m_path).absolutePath();
                scan(m_directory);
                emit stateChanged();
            } else {
                show(job.path, result);
            }
        }
        startLoad();
    });
    connect(&m_scanner, &QFutureWatcherBase::finished, this, [this] {
        m_scanning = false;
        if (m_scannedDirectory == m_directory && !m_scanPending) {
            m_siblings = m_scanner.future().takeResult();
            for (const auto &path : {m_path, m_target}) {
                if (!path.isEmpty() && QFileInfo(path).absolutePath() == m_directory
                    && !m_siblings.contains(path))
                    m_siblings.append(path);
            }
            m_lastScan = QDateTime::currentMSecsSinceEpoch();
            emit stateChanged();
            speculate();
        }
        startScan();
    });
    connect(&m_speculator, &QFutureWatcherBase::finished, this, [this] {
        const Speculation job = *m_speculating;
        m_speculating.reset();
        const auto result = m_speculator.future().takeResult();
        if (job.generation == m_generation) {
            if (job.expected.valid) {
                if (!result.unchanged && job.path == m_path && job.expected == m_photo.stamp) {
                    if (result.photo.image.isNull()) {
                        clearCache();
                        m_status = tr("The source is unavailable. The last preview is still shown.");
                    } else if (!m_turns && !busy()) {
                        m_photo = result.photo;
                        emit frameChanged();
                        emit photoChanged();
                    } else {
                        m_photo.saveBlock = tr("The file changed on disk. Reload it before saving.");
                        m_status = m_photo.saveBlock;
                    }
                    emit stateChanged();
                }
            } else if (!result.photo.image.isNull() && job.path != m_path) {
                cache(job.path, result.photo);
            }
        }
        speculate();
    });
    connect(&m_operation, &QFutureWatcherBase::finished, this, [this] {
        const auto result = m_operation.future().takeResult();
        m_operating = false;
        m_status = result.error;
        if (m_operationType == "save") {
            if (result.saved.stamp.valid) {
                m_photo.image = result.image;
                m_photo.original = m_turns % 2 ? m_photo.original.transposed() : m_photo.original;
                m_photo.stamp = result.saved.stamp;
                m_turns = 0;
                clearCache();
                m_status = tr("Saved");
                emit frameChanged();
            } else if (result.saved.reloadRequired) {
                m_photo.saveBlock = result.error;
            }
            emit stateChanged();
            emit saveFinished(result.saved.stamp.valid);
        } else {
            if (!result.image.isNull()) {
                QGuiApplication::clipboard()->setImage(result.image);
                m_status = tr("Image copied");
            }
            emit stateChanged();
        }
        speculate();
    });
}

QString Backend::name() const
{
    return QFileInfo(m_path).fileName();
}

qsizetype Backend::neighbor(int delta) const
{
    const auto current = m_siblings.indexOf(m_loading ? m_target : m_path);
    const auto target = current + delta;
    return current >= 0 && target >= 0 && target < m_siblings.size() ? target : -1;
}

void Backend::open(const QString &path)
{
    if (operating() || m_turns || path.isEmpty())
        return;
    request(QDir::cleanPath(QFileInfo(path).absoluteFilePath()), false);
}

void Backend::navigate(int delta)
{
    if (operating() || m_turns || (delta != -1 && delta != 1))
        return;
    const auto target = neighbor(delta);
    if (target < 0)
        return;
    m_direction = delta;
    request(m_siblings.at(target), true);
}

void Backend::request(const QString &path, bool allowCache)
{
    const QString directory = QFileInfo(path).absolutePath();
    if (directory != m_directory) {
        m_directory = directory;
        m_siblings.clear();
        clearCache();
        scan(directory);
    }
    ++m_serial;
    m_check = {};
    m_target = path;
    m_status.clear();
    m_requested.reset();
    if (allowCache) {
        if (const auto *hit = m_cache.object(path); hit && !hit->image.isNull()) {
            const auto photo = *hit;
            m_cache.remove(path);
            m_check = photo.stamp;
            show(path, photo);
            return;
        }
        if (path == m_path && !m_photo.image.isNull()) {
            m_loading = false;
            emit stateChanged();
            speculate();
            return;
        }
    }
    m_loading = true;
    m_requested = Load{path, m_edge, m_serial};
    emit stateChanged();
    startLoad();
}

void Backend::show(const QString &path, const ImageIO::Photo &photo)
{
    if (!m_path.isEmpty() && m_path != path && QFileInfo(m_path).absolutePath() == m_directory)
        cache(m_path, m_photo);
    m_path = m_target = path;
    m_photo = photo;
    m_prefetchTried.clear();
    m_loading = false;
    m_turns = 0;
    if (qEnvironmentVariableIsSet("IMAGE_VIEWER_TRACE"))
        qInfo().noquote() << "photo-ready" << QDateTime::currentMSecsSinceEpoch();
    emit photoChanged();
    emit frameChanged();
    emit stateChanged();
    speculate();
}

void Backend::startLoad()
{
    if (m_reading || !m_requested)
        return;
    m_reading = m_requested;
    m_requested.reset();
    const auto job = *m_reading;
    m_loader.setFuture(QtConcurrent::run([job] { return ImageIO::read(job.path, job.edge); }));
}

void Backend::scan(const QString &directory)
{
    m_scanDirectory = directory;
    m_scanPending = !directory.isEmpty();
    startScan();
}

void Backend::startScan()
{
    if (m_scanning || !m_scanPending)
        return;
    m_scanPending = false;
    m_scanning = true;
    m_scannedDirectory = m_scanDirectory;
    m_scanner.setFuture(QtConcurrent::run(ImageIO::folderImages, m_scannedDirectory, m_formats));
}

void Backend::refresh(bool reloadPhoto)
{
    if (busy() || m_path.isEmpty() || (reloadPhoto && m_turns))
        return;
    clearCache();
    scan(m_directory);
    if (reloadPhoto)
        request(m_path, false);
    else {
        m_check = m_photo.stamp;
        speculate();
    }
}

void Backend::refreshIfStale()
{
    if (m_lastScan && QDateTime::currentMSecsSinceEpoch() - m_lastScan > 30000)
        refresh();
}

void Backend::setPreviewEdge(int edge)
{
    edge = qBound(512, ((edge + 255) / 256) * 256, 4096);
    if (edge != m_edge) {
        m_edge = edge;
        clearCache();
        speculate();
    }
}

void Backend::detail(int edge)
{
    const int current = qMax(m_photo.image.width(), m_photo.image.height());
    const int original = qMax(m_photo.original.width(), m_photo.original.height());
    edge = std::min({edge, original, 8192});
    if (busy() || m_path.isEmpty() || current == 0 || edge <= current * 1.15
        || (m_reading && m_reading->detail && m_reading->edge >= edge))
        return;
    m_requested = Load{m_path, edge, m_serial, true};
    startLoad();
}

void Backend::cache(const QString &path, const ImageIO::Photo &photo)
{
    const auto bytes = photo.image.sizeInBytes();
    if (bytes > 0 && bytes <= m_cache.maxCost()
        && qMax(photo.image.width(), photo.image.height()) <= m_edge)
        m_cache.insert(path, new ImageIO::Photo(photo), bytes);
}

void Backend::clearCache()
{
    ++m_generation;
    m_cache.clear();
    m_prefetchTried.clear();
    m_check = {};
}

void Backend::speculate()
{
    if (busy() || m_speculating || m_path.isEmpty()
        || QFileInfo(m_path).absolutePath() != m_directory)
        return;
    if (m_check.valid) {
        m_speculating = Speculation{m_path, m_edge, m_generation, m_check};
        m_check = {};
    } else {
        for (const int delta : {m_direction, -m_direction}) {
            const auto index = neighbor(delta);
            if (index >= 0 && !m_cache.contains(m_siblings.at(index))
                && !m_prefetchTried.contains(m_siblings.at(index))) {
                m_prefetchTried.insert(m_siblings.at(index));
                m_speculating = Speculation{m_siblings.at(index), m_edge, m_generation, {}};
                break;
            }
        }
    }
    if (!m_speculating)
        return;
    const auto job = *m_speculating;
    m_speculator.setFuture(QtConcurrent::run([job] {
        if (job.expected.valid && ImageIO::stamp(job.path) == job.expected)
            return SpecResult{{}, true};
        return SpecResult{ImageIO::read(job.path, job.edge), false};
    }));
}

void Backend::rotate(int delta)
{
    if (!busy() && !m_photo.image.isNull() && (delta == 1 || delta == -1)) {
        m_turns = (m_turns + delta + 4) % 4;
        emit stateChanged();
    }
}

void Backend::discard()
{
    if (!busy()) {
        m_turns = 0;
        m_status.clear();
        emit stateChanged();
    }
}

void Backend::save()
{
    if (canSave()) {
        runOperation("save");
    } else if (m_turns && !busy() && !m_photo.saveBlock.isEmpty()) {
        m_status = m_photo.saveBlock;
        emit stateChanged();
    }
}

void Backend::copy()
{
    if (!busy() && !m_path.isEmpty())
        runOperation("copy");
}

void Backend::send()
{
    if (!busy() && !m_path.isEmpty() && canSend())
        runOperation("send");
}

void Backend::runOperation(const QString &action)
{
    m_operating = true;
    m_status.clear();
    m_operationType = action;
    // Discard any detail read that finishes after a copy or overwrite starts.
    ++m_serial;
    m_requested.reset();
    const auto path = m_path;
    const auto source = m_photo;
    const auto turns = m_turns;
    const auto localSend = m_localSend;
    emit stateChanged();
    m_operation.setFuture(QtConcurrent::run([action, path, source, turns, localSend] {
        Operation result;
        if (action == "save") {
            result.saved = ImageIO::save(path, source, turns);
            result.error = result.saved.error;
            if (result.saved.stamp.valid)
                result.image = ImageIO::rotate(source.image, turns);
        } else if (ImageIO::stamp(path) != source.stamp) {
            result.error = tr("The image changed or is unavailable. Reload it and try again.");
        } else if (action == "copy") {
            const auto full = ImageIO::read(path, 0);
            if (full.stamp == source.stamp && !full.image.isNull())
                result.image = ImageIO::rotate(full.image, turns);
            else
                result.error = full.error.isEmpty() ? tr("The image changed on disk.") : full.error;
        } else if (!QProcess::startDetached(localSend, {path})) {
            result.error = tr("LocalSend could not be started.");
        }
        return result;
    }));
}

void Backend::dismiss()
{
    m_status.clear();
    emit stateChanged();
}

void Backend::choose()
{
    if (operating() || m_turns)
        return;
    auto bus = QDBusConnection::sessionBus();
    QString sender = bus.baseService().mid(1);
    sender.replace('.', '_');
    const QString token = "viewer_" + QUuid::createUuid().toString(QUuid::Id128);
    m_portalRequest = QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2").arg(sender, token);
    if (!bus.connect(Portal, m_portalRequest, Request, "Response", this, SLOT(picked(uint,QVariantMap)))) {
        picked(2, {});
        return;
    }
    auto call = QDBusMessage::createMethodCall(Portal, "/org/freedesktop/portal/desktop",
                                               "org.freedesktop.portal.FileChooser", "OpenFile");
    QVariantMap options{{"handle_token", token}};
    if (!m_path.isEmpty())
        options.insert("current_folder", QFile::encodeName(QFileInfo(m_path).absolutePath()) + '\0');
    call << QString() << tr("Open image") << options;
    auto *pending = new QDBusPendingCallWatcher(bus.asyncCall(call), this);
    connect(pending, &QDBusPendingCallWatcher::finished, this, [this, bus, pending]() mutable {
        const QDBusPendingReply<QDBusObjectPath> reply = *pending;
        pending->deleteLater();
        if (m_portalRequest.isEmpty())
            return;
        if (reply.isError()) {
            picked(2, {});
        } else if (reply.value().path() != m_portalRequest) {
            bus.disconnect(Portal, m_portalRequest, Request, "Response", this, SLOT(picked(uint,QVariantMap)));
            m_portalRequest = reply.value().path();
            bus.connect(Portal, m_portalRequest, Request, "Response", this, SLOT(picked(uint,QVariantMap)));
        }
    });
    emit stateChanged();
}

void Backend::picked(uint response, const QVariantMap &results)
{
    QDBusConnection::sessionBus().disconnect(Portal, m_portalRequest, Request, "Response",
                                             this, SLOT(picked(uint,QVariantMap)));
    m_portalRequest.clear();
    if (qEnvironmentVariableIsSet("IMAGE_VIEWER_TRACE"))
        qInfo() << "file-chooser-response" << response;
    if (response == 0) {
        const auto uris = qdbus_cast<QStringList>(results.value("uris"));
        const QUrl selected(uris.value(0));
        if (selected.isLocalFile())
            open(selected.toLocalFile());
        else
            m_status = tr("Open a file from a locally mounted folder.");
    } else if (response != 1) {
        m_status = tr("The desktop file picker is unavailable.");
    }
    emit stateChanged();
}
