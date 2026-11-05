#pragma once

#include "imageio.h"

#include <QCache>
#include <QFutureWatcher>
#include <QObject>
#include <QSet>
#include <QUrl>
#include <optional>

class Backend : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QImage frame READ frame NOTIFY frameChanged)
    Q_PROPERTY(QSize original READ original NOTIFY frameChanged)
    Q_PROPERTY(QString name READ name NOTIFY stateChanged)
    Q_PROPERTY(QString status READ status NOTIFY stateChanged)
    Q_PROPERTY(QString saveBlock READ saveBlock NOTIFY stateChanged)
    Q_PROPERTY(int turns READ turns NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(bool operating READ operating NOTIFY stateChanged)
    Q_PROPERTY(bool canSave READ canSave NOTIFY stateChanged)
    Q_PROPERTY(bool previous READ previous NOTIFY stateChanged)
    Q_PROPERTY(bool next READ next NOTIFY stateChanged)
    Q_PROPERTY(bool canSend READ canSend CONSTANT)

public:
    explicit Backend(QObject *parent = nullptr);

    QImage frame() const { return m_photo.image; }
    QSize original() const { return m_photo.original; }
    QString name() const;
    QString status() const { return m_status; }
    QString saveBlock() const { return m_photo.saveBlock; }
    int turns() const { return m_turns; }
    bool busy() const { return m_loading || operating(); }
    bool operating() const { return m_operating || !m_portalRequest.isEmpty(); }
    bool canSave() const { return m_turns && !busy() && m_photo.saveBlock.isEmpty(); }
    bool previous() const { return neighbor(-1) >= 0; }
    bool next() const { return neighbor(1) >= 0; }
    bool canSend() const { return !m_localSend.isEmpty(); }

    Q_INVOKABLE void open(const QString &path);
    Q_INVOKABLE void choose();
    Q_INVOKABLE void navigate(int delta);
    Q_INVOKABLE void refresh(bool reloadPhoto = false);
    Q_INVOKABLE void refreshIfStale();
    Q_INVOKABLE void setPreviewEdge(int edge);
    Q_INVOKABLE void detail(int edge);
    Q_INVOKABLE void rotate(int delta);
    Q_INVOKABLE void discard();
    Q_INVOKABLE void save();
    Q_INVOKABLE void copy();
    Q_INVOKABLE void send();
    Q_INVOKABLE void dismiss();

signals:
    void frameChanged();
    void photoChanged();
    void stateChanged();
    void saveFinished(bool success);

private slots:
    void picked(uint response, const QVariantMap &results);

private:
    struct Load {
        QString path;
        int edge = 0;
        quint64 serial = 0;
        bool detail = false;
    };

    struct Speculation {
        QString path;
        int edge = 0;
        quint64 generation = 0;
        ImageIO::Stamp expected;
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
    void speculate();
    void cache(const QString &path, const ImageIO::Photo &photo);
    void clearCache();
    void runOperation(const QString &action);

    ImageIO::Photo m_photo;
    QString m_path, m_target, m_status;
    QString m_directory, m_scanDirectory, m_scannedDirectory;
    QString m_portalRequest, m_localSend;
    QStringList m_siblings, m_formats;
    QSet<QString> m_prefetchTried;
    QCache<QString, ImageIO::Photo> m_cache{48 * 1024 * 1024};
    QFutureWatcher<ImageIO::Photo> m_loader;
    QFutureWatcher<QStringList> m_scanner;
    QFutureWatcher<SpecResult> m_speculator;
    QFutureWatcher<Operation> m_operation;
    std::optional<Load> m_requested, m_reading;
    std::optional<Speculation> m_speculating;
    QString m_operationType;
    ImageIO::Stamp m_check;
    qint64 m_lastScan = 0;
    quint64 m_serial = 0, m_generation = 0;
    int m_turns = 0, m_edge = 1536, m_direction = 1;
    bool m_loading = false, m_operating = false, m_scanning = false, m_scanPending = false;
};
