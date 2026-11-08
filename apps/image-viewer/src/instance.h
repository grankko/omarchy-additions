#pragma once

#include <QDBusConnection>
#include <QDBusContext>
#include <QObject>
#include <optional>

class Instance : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "io.github.grankko.OmarchyImageViewer")

public:
    enum class Result { Owner, Forwarded, Failed };
    explicit Instance(QObject *parent = nullptr);
    ~Instance() override;
    Result start(const QString &path, const QString &token);
    void setReady();
    void stop();
    QString error() const { return m_error; }
    static QString imagePath(const QString &input, QString &error);

public slots:
    bool Open(uint protocol, const QString &path, const QString &token);

signals:
    void openRequested(const QString &path, const QString &token, bool activate);

private:
    struct Request { QString path, token; };
    QDBusConnection m_bus;
    std::optional<Request> m_pending;
    QString m_error;
    bool m_owner = false;
    bool m_ready = false;
    bool m_stopping = false;
};
