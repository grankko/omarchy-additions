#include "backend.h"
#include "trace.h"

#include <QClipboard>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
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

void closePortalRequest(const QString &handle)
{
    if (!handle.isEmpty())
        QDBusConnection::sessionBus().asyncCall(QDBusMessage::createMethodCall(Portal, handle, Request, "Close"), 3000);
}
}

Backend::Backend(QObject *parent) : Backend(Jobs{}, parent)
{
}

Backend::Backend(Jobs jobs, QObject *parent)
    : QObject(parent), m_jobs(std::move(jobs)), m_localSend(QStandardPaths::findExecutable("localsend")),
      m_formats(ImageIO::formats())
{
    for (auto *lane : {&m_foregroundPool, &m_scanPool, &m_backgroundPool, &m_operationPool})
        lane->setMaxThreadCount(1);

    if (m_localSend.isEmpty())
        m_localSend = QStandardPaths::findExecutable("localsend_app");

    auto *portal = new QDBusServiceWatcher(Portal, QDBusConnection::sessionBus(),
                                          QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(portal, &QDBusServiceWatcher::serviceOwnerChanged, this,
            [this](const QString &, const QString &previous, const QString &current) {
        if (m_picker && !previous.isEmpty() && previous != current)
            finishPicker(2);
    });

    connect(&m_loader, &QFutureWatcherBase::finished, this, &Backend::finishLoad);
    connect(&m_scanner, &QFutureWatcherBase::finished, this, &Backend::finishScan);
    connect(&m_speculator, &QFutureWatcherBase::finished, this, &Backend::finishBackground);
    connect(&m_operation, &QFutureWatcherBase::finished, this, &Backend::finishOperation);
}

Backend::~Backend()
{
    for (const auto &stop : {m_stopForeground, m_stopScan, m_stopBackground}) {
        if (stop)
            stop->store(true);
    }
}

void Backend::finishLoad()
{
    const Load job = *m_reading;
    m_reading.reset();
    ImageIO::Photo result;
    try {
        result = m_loader.future().takeResult();
    } catch (...) {
        result.error = "The image could not be read.";
    }
    viewerTrace("decode-complete", job.serial, result.image.sizeInBytes());
    // The visible photo and requested photo can differ while I/O is pending.
    if (job.serial == m_serial) {
        if (result.image.isNull()) {
            m_loading = false;
            setStatus(result.error, StatusKind::Error, QFileInfo(job.path).fileName());
            clearCache();
            emit stateChanged();
        } else {
            show(job.path, result);
        }
    }
    startLoad();
}

void Backend::finishScan()
{
    m_scanning = false;
    ImageIO::FolderSnapshot snapshot;
    try {
        snapshot = m_scanner.future().takeResult();
    } catch (...) {
        snapshot.error = "The folder could not be read completely.";
    }
    if (m_scannedDirectory == m_directory && !m_scanPending) {
        if (snapshot.error.isEmpty()) {
            m_siblings = snapshot.entries;
            m_folderIndex.clear();
            m_folderIndex.reserve(m_siblings.size());
            for (qsizetype i = 0; i < m_siblings.size(); ++i)
                m_folderIndex.insert(m_siblings.at(i), i);
            locateCursor();
            m_lastScan.start();
        } else if (m_status.isEmpty()) {
            setStatus(snapshot.error);
        }
        emit stateChanged();
        speculate();
    }
    startScan();
}

void Backend::finishBackground()
{
    const Speculation job = *m_speculating;
    m_speculating.reset();
    SpecResult result;
    try {
        result = m_speculator.future().takeResult();
    } catch (...) {
        // Treat background failures as an unavailable result for that job.
    }
    if (job.generation == m_generation && !m_stopBackground->load()) {
        if (job.kind == Speculation::Kind::Detail) {
            if (!busy() && job.detailSerial == m_detailSerial && job.path == m_path) {
                if (result.photo.image.isNull() || result.photo.stamp != m_photo.stamp
                    || result.photo.image.sizeInBytes() > ImageIO::DetailLimitBytes) {
                    m_failedDetailEdge = job.edge;
                    setStatus("Higher-resolution detail is unavailable. The preview is still shown.");
                    emit stateChanged();
                } else {
                    m_detailFrame = result.photo.image;
                    emit frameChanged();
                }
            }
        } else if (job.kind == Speculation::Kind::Freshness) {
            if (!result.unchanged && job.path == m_path && job.expected == m_photo.stamp) {
                if (result.photo.image.isNull()) {
                    clearCache();
                    setStatus("The source is unavailable. The last preview is still shown.");
                } else if (!m_turns && !busy()) {
                    dropDetail();
                    m_failedDetailEdge = 0;
                    m_photo = result.photo;
                    m_presentationFailed = false;
                    emit frameChanged();
                    emit photoChanged();
                } else {
                    m_photo.saveBlock = "The file changed on disk. Reload it before saving.";
                    setStatus(m_photo.saveBlock, StatusKind::Persistent);
                }
                emit stateChanged();
            }
        } else if (!result.photo.image.isNull() && job.path != m_path) {
            cache(job.path, result.photo);
        }
    }
    speculate();
}

void Backend::finishOperation()
{
    Operation result;
    try {
        result = m_operation.future().takeResult();
    } catch (...) {
        result.error = "The image operation could not be completed.";
        if (m_operationType == OperationKind::Save)
            result.saved.reloadRequired = true;
    }
    m_saveControl.reset();
    m_operating = false;
    setStatus(result.error);
    if (m_operationType == OperationKind::Save) {
        if (result.saved.committed()) {
            m_photo.image = result.image;
            m_photo.original = m_turns % 2 ? m_photo.original.transposed() : m_photo.original;
            m_photo.stamp = result.saved.stamp;
            m_turns = 0;
            clearCache();
            if (result.saved.durabilityWarning.isEmpty())
                setStatus("Saved", StatusKind::Success);
            else
                setStatus(result.saved.durabilityWarning, StatusKind::Persistent);
            emit frameChanged();
        } else if (result.saved.reloadRequired) {
            m_photo.saveBlock = result.error;
            setStatus(result.error, StatusKind::Persistent);
        }
        emit stateChanged();
        emit saveFinished(result.saved.committed());
    } else {
        if (!result.image.isNull()) {
            QGuiApplication::clipboard()->setImage(result.image);
            setStatus("Image copied", StatusKind::Success);
        }
        emit stateChanged();
    }
    speculate();
}

QString Backend::name() const
{
    return QFileInfo(m_path).fileName();
}

void Backend::locateCursor()
{
    const auto index = m_folderIndex.constFind(m_cursor);
    m_cursorPresent = index != m_folderIndex.cend();
    m_cursorIndex = m_cursorPresent ? index.value() : ImageIO::folderInsertionIndex(m_siblings, m_cursor);
}

qsizetype Backend::neighbor(int delta) const
{
    if (m_cursor.isEmpty())
        return -1;
    // When the cursor was deleted, its insertion position is the next entry.
    const auto target = m_cursorIndex + (m_cursorPresent || delta < 0 ? delta : 0);
    return target >= 0 && target < m_siblings.size() ? target : -1;
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
        m_folderIndex.clear();
        m_lastScan.invalidate();
        clearCache();
        scan(directory);
    }
    ++m_serial;
    if (m_stopForeground)
        m_stopForeground->store(true);
    if (m_stopBackground)
        m_stopBackground->store(true);
    viewerTrace("request", m_serial);
    dropDetail();
    m_failedDetailEdge = 0;
    m_check = {};
    m_cursor = path;
    locateCursor();
    setStatus({});
    m_requested.reset();
    if (allowCache) {
        if (const auto *hit = m_cache.object(path); hit && !hit->image.isNull()) {
            viewerTrace("cache-hit", m_serial, m_cache.totalCost());
            const auto photo = *hit;
            m_cache.remove(path);
            m_check = photo.stamp;
            show(path, photo);
            return;
        }
        if (path == m_path && !m_photo.image.isNull()) {
            viewerTrace("already-visible", m_serial);
            m_loading = false;
            emit stateChanged();
            speculate();
            return;
        }
    }
    viewerTrace("cache-miss", m_serial, m_cache.totalCost());
    m_loading = true;
    m_requested = Load{path, m_edge, m_serial};
    emit stateChanged();
    startLoad();
}

void Backend::show(const QString &path, const ImageIO::Photo &photo)
{
    if (!m_path.isEmpty() && m_path != path && QFileInfo(m_path).absolutePath() == m_directory)
        cache(m_path, m_photo);
    m_path = path;
    m_photo = photo;
    m_presentationFailed = false;
    m_prefetchTried.clear();
    m_loading = false;
    m_turns = 0;
    m_displaySerial = uint(m_serial);
    viewerTrace("frame-installed", m_displaySerial, m_photo.image.sizeInBytes());
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
    const auto stop = m_stopForeground = std::make_shared<std::atomic_bool>(false);
    m_loader.setFuture(QtConcurrent::run(&m_foregroundPool, [job, read = m_jobs.read, stop] {
        return read(job.path, job.edge, stop.get());
    }));
}

void Backend::scan(const QString &directory)
{
    m_scanDirectory = directory;
    if (m_stopScan)
        m_stopScan->store(true);
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
    const auto stop = m_stopScan = std::make_shared<std::atomic_bool>(false);
    m_scanner.setFuture(QtConcurrent::run(&m_scanPool,
        [scan = m_jobs.scan, path = m_scannedDirectory, formats = m_formats, stop] {
            return scan(path, formats, stop.get());
        }));
}

void Backend::refresh(bool reloadPhoto)
{
    if (busy() || m_directory.isEmpty() || (reloadPhoto && (m_turns || m_path.isEmpty())))
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
    if (!m_scanning && !m_directory.isEmpty() && (!m_lastScan.isValid() || m_lastScan.hasExpired(30000)))
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

void Backend::dropDetail()
{
    ++m_detailSerial;
    if (m_stopBackground && m_speculating && m_speculating->kind == Speculation::Kind::Detail)
        m_stopBackground->store(true);
    m_detailRequest.reset();
    if (!m_detailFrame.isNull()) {
        m_detailFrame = {};
        viewerTrace("detail-drop", m_displaySerial);
        emit frameChanged();
    }
}

void Backend::detail(int edge)
{
    const int fit = qMax(m_photo.image.width(), m_photo.image.height());
    edge = ImageIO::boundedDetailEdge(m_photo.original, edge);
    if (!fit || edge <= fit * 1.15) {
        dropDetail();
        m_failedDetailEdge = 0;
        return;
    }
    if (busy() || m_presentationFailed || (m_failedDetailEdge && edge >= m_failedDetailEdge))
        return;
    const int current = qMax(m_detailFrame.width(), m_detailFrame.height());
    if (current && edge >= current * .75 && edge <= current * 1.15)
        return;
    if (m_speculating && m_speculating->kind == Speculation::Kind::Detail
        && m_speculating->edge == edge && m_speculating->detailSerial == m_detailSerial)
        return;
    m_detailRequest = Load{m_path, edge, ++m_detailSerial};
    speculate();
}

void Backend::cache(const QString &path, const ImageIO::Photo &photo)
{
    const auto bytes = photo.image.sizeInBytes();
    if (bytes > 0 && bytes <= m_cache.maxCost()
        && qMax(photo.image.width(), photo.image.height()) <= m_edge) {
        m_cache.insert(path, new ImageIO::Photo(photo), bytes);
        viewerTrace("cache-bytes", m_displaySerial, m_cache.totalCost());
    }
}

void Backend::clearCache()
{
    ++m_generation;
    if (m_stopBackground)
        m_stopBackground->store(true);
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
        m_speculating = Speculation{Speculation::Kind::Freshness, m_path, m_edge, m_generation, m_check};
        m_check = {};
    } else if (m_detailRequest) {
        const auto detail = *m_detailRequest;
        m_detailRequest.reset();
        viewerTrace("detail-start", m_displaySerial, detail.edge);
        m_speculating = Speculation{Speculation::Kind::Detail, detail.path, detail.edge, m_generation,
                                    m_photo.stamp, detail.serial};
    } else {
        for (const int delta : {m_direction, -m_direction}) {
            const auto index = neighbor(delta);
            if (index >= 0 && !m_cache.contains(m_siblings.at(index))
                && !m_prefetchTried.contains(m_siblings.at(index))) {
                m_prefetchTried.insert(m_siblings.at(index));
                m_speculating = Speculation{Speculation::Kind::Neighbor, m_siblings.at(index), m_edge, m_generation, {}};
                break;
            }
        }
    }
    if (!m_speculating)
        return;
    const auto job = *m_speculating;
    const auto stop = m_stopBackground = std::make_shared<std::atomic_bool>(false);
    m_speculator.setFuture(QtConcurrent::run(&m_backgroundPool, [job, read = m_jobs.read, prefetch = m_jobs.prefetch, stop] {
        if (stop->load())
            return SpecResult{};
        if (job.kind == Speculation::Kind::Freshness && ImageIO::stamp(job.path) == job.expected)
            return SpecResult{{}, true};
        const auto &decode = job.kind == Speculation::Kind::Neighbor ? prefetch : read;
        return SpecResult{decode(job.path, job.edge, stop.get()), false};
    }));
}

void Backend::rotate(int delta)
{
    if (!busy() && !m_presentationFailed && !m_photo.image.isNull() && (delta == 1 || delta == -1)) {
        m_turns = (m_turns + delta + 4) % 4;
        emit stateChanged();
    }
}

void Backend::discard()
{
    if (!busy()) {
        m_turns = 0;
        setStatus({});
        emit stateChanged();
    }
}

void Backend::save()
{
    if (canSave()) {
        runOperation(OperationKind::Save);
    } else if (m_turns && !busy() && !m_photo.saveBlock.isEmpty()) {
        setStatus(m_photo.saveBlock, StatusKind::Persistent);
        emit stateChanged();
    }
}

bool Backend::cancelSave()
{
    if (!m_saveControl)
        return false;
    const bool accepted = m_saveControl->cancel();
    setStatus(accepted ? "Cancelling save…" : "Waiting for the save to finish safely…");
    emit stateChanged();
    return accepted;
}

void Backend::copy()
{
    if (!busy() && !m_presentationFailed && !m_path.isEmpty())
        runOperation(OperationKind::Copy);
}

void Backend::send()
{
    if (!busy() && !m_presentationFailed && !m_path.isEmpty() && canSend())
        runOperation(OperationKind::Send);
}

void Backend::runOperation(OperationKind action)
{
    m_operating = true;
    setStatus({});
    m_operationType = action;
    if (action == OperationKind::Save)
        m_saveControl = std::make_shared<ImageIO::SaveControl>();
    const auto control = m_saveControl;
    // Discard any detail read that finishes after a copy or overwrite starts.
    ++m_serial;
    if (m_stopBackground)
        m_stopBackground->store(true);
    dropDetail();
    m_requested.reset();
    const auto path = m_path;
    const auto source = m_photo;
    const auto turns = m_turns;
    const auto localSend = m_localSend;
    emit stateChanged();
    m_operation.setFuture(QtConcurrent::run(&m_operationPool, [action, path, source, turns, localSend, control] {
        Operation result;
        if (action == OperationKind::Save) {
            result.image = ImageIO::rotate(source.image, turns);
            if (result.image.isNull()) {
                result.error = "The rotated preview could not be allocated.";
                return result;
            }
            result.saved = ImageIO::save(path, source, turns, {control});
            result.error = result.saved.error;
        } else if (ImageIO::stamp(path) != source.stamp) {
            result.error = "The image changed or is unavailable. Reload it and try again.";
        } else if (action == OperationKind::Copy) {
            const auto full = ImageIO::read(path, 0);
            if (full.stamp == source.stamp && !full.image.isNull())
                result.image = ImageIO::rotate(full.image, turns);
            else
                result.error = full.error.isEmpty() ? "The image changed on disk." : full.error;
        } else if (!QProcess::startDetached(localSend, {path})) {
            result.error = "LocalSend could not be started.";
        }
        return result;
    }));
}

void Backend::presentationFailed()
{
    if (!m_detailFrame.isNull()) {
        m_failedDetailEdge = qMax(m_detailFrame.width(), m_detailFrame.height());
        dropDetail();
        setStatus("Higher-resolution detail is unavailable. The preview is still shown.");
    } else {
        m_presentationFailed = true;
        setStatus("The image could not be displayed. Reload it to try again.", StatusKind::Persistent);
    }
    emit stateChanged();
}

namespace {
QString translateMessage(const QString &source)
{
    if (source.isEmpty())
        return {};
    return QCoreApplication::translate("ViewerMessage", source.toUtf8().constData());
}
}

QString Backend::status() const
{
    const auto text = translateMessage(m_status);
    return m_statusFile.isEmpty() ? text : tr("%1 — %2").arg(m_statusFile, text);
}

QString Backend::saveBlock() const
{
    return translateMessage(m_photo.saveBlock);
}

void Backend::setStatus(QString text, StatusKind kind, QString fileName)
{
    m_status = std::move(text);
    m_statusFile = std::move(fileName);
    m_statusKind = kind;
}

void Backend::dismiss()
{
    setStatus({});
    emit stateChanged();
}

void Backend::choose()
{
    if (operating() || m_turns)
        return;
    if (m_pickerHandshakes >= 4) {
        setStatus("The desktop file picker is still responding to earlier requests.");
        emit stateChanged();
        return;
    }
    auto bus = QDBusConnection::sessionBus();
    QString sender = bus.baseService().mid(1);
    sender.replace('.', '_');
    const QString token = "viewer_" + QUuid::createUuid().toString(QUuid::Id128);
    const auto picker = std::make_shared<Picker>();
    picker->handle = QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2").arg(sender, token);
    m_picker = picker;
    if (!bus.connect(Portal, picker->handle, Request, "Response", this,
                     SLOT(picked(uint,QVariantMap,QDBusMessage)))) {
        finishPicker(2);
        return;
    }
    auto call = QDBusMessage::createMethodCall(Portal, "/org/freedesktop/portal/desktop",
                                               "org.freedesktop.portal.FileChooser", "OpenFile");
    QVariantMap options{{"handle_token", token}};
    if (!m_path.isEmpty())
        options.insert("current_folder", QFile::encodeName(QFileInfo(m_path).absolutePath()) + '\0');
    call << QString() << tr("Open image") << options;
    ++m_pickerHandshakes;
    auto *pending = new QDBusPendingCallWatcher(bus.asyncCall(call, 10000), this);
    connect(pending, &QDBusPendingCallWatcher::finished, this, [this, bus, pending, picker]() mutable {
        const QDBusPendingReply<QDBusObjectPath> reply = *pending;
        pending->deleteLater();
        --m_pickerHandshakes;
        const QString returned = reply.isError() ? QString{} : reply.value().path();
        if (m_picker != picker) {
            if (picker->cancelled && (!m_picker || returned != m_picker->handle))
                closePortalRequest(returned);
            return;
        }
        if (returned.isEmpty()) {
            finishPicker(2);
        } else if (returned != picker->handle) {
            bus.disconnect(Portal, picker->handle, Request, "Response", this,
                           SLOT(picked(uint,QVariantMap,QDBusMessage)));
            picker->handle = returned;
            if (!bus.connect(Portal, returned, Request, "Response", this,
                             SLOT(picked(uint,QVariantMap,QDBusMessage)))) {
                closePortalRequest(returned);
                finishPicker(2);
            }
        }
    });
    emit stateChanged();
}

void Backend::cancelPicker()
{
    if (!m_picker)
        return;
    m_picker->cancelled = true;
    closePortalRequest(m_picker->handle);
    // An application-side Close has no Response signal.
    finishPicker(1);
}

void Backend::picked(uint response, const QVariantMap &results, const QDBusMessage &message)
{
    if (m_picker && message.path() == m_picker->handle)
        finishPicker(response, results);
}

void Backend::finishPicker(uint response, const QVariantMap &results)
{
    QDBusConnection::sessionBus().disconnect(Portal, m_picker->handle, Request, "Response", this,
                                             SLOT(picked(uint,QVariantMap,QDBusMessage)));
    m_picker.reset();
    if (response == 0) {
        const auto uris = qdbus_cast<QStringList>(results.value("uris"));
        const QUrl selected(uris.value(0), QUrl::StrictMode);
        if (uris.size() != 1 || !selected.isValid() || selected.isEmpty())
            setStatus("The file picker did not return an image.");
        else if (!selected.isLocalFile() || !QDir::isAbsolutePath(selected.toLocalFile()))
            setStatus("Open a file from a locally mounted folder.");
        else
            open(selected.toLocalFile());
    } else if (response != 1) {
        setStatus("The desktop file picker is unavailable.");
    }
    emit stateChanged();
}
