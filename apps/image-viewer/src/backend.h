#pragma once

#include "imageio.h"

#include <QCache>
#include <QFutureWatcher>
#include <QElapsedTimer>
#include <QDBusMessage>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QThreadPool>
#include <QUrl>
#include <optional>
#include <functional>
#include <memory>

class Backend : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QImage frame READ frame NOTIFY frameChanged)
    Q_PROPERTY(uint displaySerial READ displaySerial NOTIFY frameChanged)
    Q_PROPERTY(QSize original READ original NOTIFY frameChanged)
    Q_PROPERTY(QString name READ name NOTIFY stateChanged)
    Q_PROPERTY(QString status READ status NOTIFY stateChanged)
    Q_PROPERTY(bool statusPersistent READ statusPersistent NOTIFY stateChanged)
    Q_PROPERTY(bool statusSuccess READ statusSuccess NOTIFY stateChanged)
    Q_PROPERTY(QString saveBlock READ saveBlock NOTIFY stateChanged)
    Q_PROPERTY(int turns READ turns NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(bool operating READ operating NOTIFY stateChanged)
    Q_PROPERTY(bool picking READ picking NOTIFY stateChanged)
    Q_PROPERTY(bool photoActionable READ photoActionable NOTIFY stateChanged)
    Q_PROPERTY(bool canSave READ canSave NOTIFY stateChanged)
    Q_PROPERTY(bool previous READ previous NOTIFY stateChanged)
    Q_PROPERTY(bool next READ next NOTIFY stateChanged)
    Q_PROPERTY(bool canSend READ canSend CONSTANT)

public:
    struct Jobs {
        std::function<ImageIO::Photo(const QString &, int, const std::atomic_bool *)> read = ImageIO::readCancellable;
        std::function<ImageIO::Photo(const QString &, int, const std::atomic_bool *)> prefetch = ImageIO::readSpeculative;
        std::function<ImageIO::FolderSnapshot(const QString &, const QStringList &, const std::atomic_bool *)> scan = ImageIO::folderImages;
    };

    explicit Backend(QObject *parent = nullptr);
    explicit Backend(Jobs jobs, QObject *parent = nullptr);
    ~Backend() override;

    QImage frame() const { return m_detailFrame.isNull() ? m_photo.image : m_detailFrame; }
    uint displaySerial() const { return m_displaySerial; }
    QSize original() const { return m_photo.original; }
    QString name() const;
    QString status() const;
    bool statusPersistent() const { return m_statusKind == StatusKind::Persistent; }
    bool statusSuccess() const { return m_statusKind == StatusKind::Success; }
    QString saveBlock() const;
    int turns() const { return m_turns; }
    bool busy() const { return m_loading || operating(); }
    bool operating() const { return m_operating || picking(); }
    bool picking() const { return bool(m_picker); }
    bool photoActionable() const { return !busy() && !m_presentationFailed && !m_photo.image.isNull(); }
    bool canSave() const { return m_turns && photoActionable() && m_photo.saveBlock.isEmpty(); }
    bool previous() const { return neighbor(-1) >= 0; }
    bool next() const { return neighbor(1) >= 0; }
    bool canSend() const { return !m_localSend.isEmpty(); }

    Q_INVOKABLE void open(const QString &path);
    Q_INVOKABLE void choose();
    Q_INVOKABLE void cancelPicker();
    Q_INVOKABLE void navigate(int delta);
    Q_INVOKABLE void refresh(bool reloadPhoto = false);
    Q_INVOKABLE void refreshIfStale();
    Q_INVOKABLE void setPreviewEdge(int edge);
    Q_INVOKABLE void detail(int edge);
    Q_INVOKABLE void rotate(int delta);
    Q_INVOKABLE void discard();
    Q_INVOKABLE void save();
    Q_INVOKABLE bool cancelSave();
    Q_INVOKABLE void copy();
    Q_INVOKABLE void send();
    Q_INVOKABLE void dismiss();
    Q_INVOKABLE void presentationFailed();

signals:
    void frameChanged();
    void photoChanged();
    void stateChanged();
    void saveFinished(bool success);

private slots:
    void picked(uint response, const QVariantMap &results, const QDBusMessage &message);

private:
    enum class OperationKind { Save, Copy, Send };
    enum class StatusKind { Error, Success, Persistent };
    void setStatus(QString text, StatusKind kind = StatusKind::Error, QString fileName = {});
    void finishLoad();
    void finishScan();
    void finishBackground();
    void finishOperation();

    struct Picker {
        QString handle;
        bool cancelled = false;
    };

    void finishPicker(uint response, const QVariantMap &results = {});
    struct Load {
        QString path;
        int edge = 0;
        quint64 serial = 0;
    };

    struct Speculation {
        enum class Kind { Detail, Freshness, Neighbor };
        Kind kind;
        QString path;
        int edge = 0;
        quint64 generation = 0;
        ImageIO::Stamp expected;
        quint64 detailSerial = 0;
    };

    struct SpecResult {
        ImageIO::Photo photo;
        bool unchanged = false;
    };

    struct Operation {
        QString error;
        ImageIO::Saved saved;
        QImage image;
    };

    qsizetype neighbor(int delta) const;
    void request(const QString &path, bool allowCache);
    void show(const QString &path, const ImageIO::Photo &photo);
    void startLoad();
    void scan(const QString &directory);
    void startScan();
    void locateCursor();
    void speculate();
    void dropDetail();
    void cache(const QString &path, const ImageIO::Photo &photo);
    void clearCache();
    void runOperation(OperationKind action);

    const Jobs m_jobs;
    ImageIO::Photo m_photo;
    QImage m_detailFrame;
    QString m_path, m_cursor, m_status, m_statusFile;
    QString m_directory, m_scanDirectory, m_scannedDirectory;
    QString m_localSend;
    std::shared_ptr<Picker> m_picker;
    int m_pickerHandshakes = 0;
    QStringList m_siblings, m_formats;
    QHash<QString, qsizetype> m_folderIndex;
    qsizetype m_cursorIndex = 0;
    bool m_cursorPresent = false;
    QSet<QString> m_prefetchTried;
    QCache<QString, ImageIO::Photo> m_cache{48 * 1024 * 1024};
    // These drain after watchers are destroyed, outside the running app loop.
    QThreadPool m_foregroundPool, m_scanPool, m_backgroundPool, m_operationPool;
    QFutureWatcher<ImageIO::Photo> m_loader;
    QFutureWatcher<ImageIO::FolderSnapshot> m_scanner;
    QFutureWatcher<SpecResult> m_speculator;
    QFutureWatcher<Operation> m_operation;
    std::optional<Load> m_requested, m_reading, m_detailRequest;
    std::optional<Speculation> m_speculating;
    OperationKind m_operationType = OperationKind::Save;
    StatusKind m_statusKind = StatusKind::Error;
    std::shared_ptr<ImageIO::SaveControl> m_saveControl;
    std::shared_ptr<std::atomic_bool> m_stopForeground, m_stopScan, m_stopBackground;
    ImageIO::Stamp m_check;
    QElapsedTimer m_lastScan;
    quint64 m_serial = 0, m_generation = 0, m_detailSerial = 0;
    uint m_displaySerial = 0;
    int m_failedDetailEdge = 0;
    int m_turns = 0, m_edge = 1536, m_direction = 1;
    bool m_presentationFailed = false;
    bool m_loading = false, m_operating = false, m_scanning = false, m_scanPending = false;
};
