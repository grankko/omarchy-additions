#include "viewer-tests.h"
#include "backend.h"
#include "translator.h"
#include "instance.h"

#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QtTest>
#include <atomic>
#include <thread>

void ViewerTests::instanceForwardsLaunches()
{
    QTemporaryDir dir;
    QString error;
    const auto image = dir.filePath("Photo with spaces å.png");
    QCOMPARE(Instance::imagePath(QUrl::fromLocalFile(image).toString(), error), image);
    QVERIFY(error.isEmpty());
    QVERIFY(Instance::imagePath("https://example.invalid/photo.png", error).isEmpty());
    QVERIFY(!error.isEmpty());
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("VIEWER_INSTANCE_PROBE", "1");
    QProcess owner;
    owner.setProcessEnvironment(environment);
    owner.start(QCoreApplication::applicationFilePath(), {image});
    QVERIFY(owner.waitForStarted());
    const auto stop = qScopeGuard([&] { owner.kill(); owner.waitForFinished(); });
    QTRY_VERIFY(owner.canReadLine());
    QCOMPARE(QString::fromUtf8(owner.readLine()).trimmed(), image);
    QProcess follower;
    follower.setProcessEnvironment(environment);
    follower.start(QCoreApplication::applicationFilePath(), {dir.filePath("-next.png")});
    QTRY_COMPARE(follower.state(), QProcess::NotRunning);
    QCOMPARE(follower.exitCode(), 0);
    QTRY_VERIFY(owner.canReadLine());
    QCOMPARE(QString::fromUtf8(owner.readLine()).trimmed(), dir.filePath("-next.png"));
    follower.start(QCoreApplication::applicationFilePath(), {});
    QTRY_COMPARE(follower.state(), QProcess::NotRunning);
    QCOMPARE(follower.exitCode(), 0);
    QTRY_VERIFY(owner.canReadLine());
    QCOMPARE(owner.readLine().trimmed(), "activate");
    QCOMPARE(owner.state(), QProcess::Running);
    owner.kill();
    QVERIFY(owner.waitForFinished());
    follower.start(QCoreApplication::applicationFilePath(), {image});
    QTRY_VERIFY(follower.canReadLine());
    QCOMPARE(QString::fromUtf8(follower.readLine()).trimmed(), image);
    QCOMPARE(follower.state(), QProcess::Running);
    follower.kill();
    QVERIFY(follower.waitForFinished());

    QProcess firstLaunch, secondLaunch;
    firstLaunch.setProcessEnvironment(environment);
    secondLaunch.setProcessEnvironment(environment);
    firstLaunch.start(QCoreApplication::applicationFilePath(), {image});
    secondLaunch.start(QCoreApplication::applicationFilePath(), {dir.filePath("second.png")});
    QVERIFY(firstLaunch.waitForStarted());
    QVERIFY(secondLaunch.waitForStarted());
    const auto stopPair = qScopeGuard([&] {
        for (auto *process : {&firstLaunch, &secondLaunch}) {
            if (process->state() != QProcess::NotRunning) {
                process->kill();
                process->waitForFinished();
            }
        }
    });
    QTRY_VERIFY((firstLaunch.state() == QProcess::NotRunning)
                != (secondLaunch.state() == QProcess::NotRunning));
    auto *forwarded = firstLaunch.state() == QProcess::NotRunning ? &firstLaunch : &secondLaunch;
    auto *primary = forwarded == &firstLaunch ? &secondLaunch : &firstLaunch;
    QCOMPARE(forwarded->exitCode(), 0);
    QTRY_VERIFY(primary->bytesAvailable() > 0);
    QByteArray received;
    QTRY_VERIFY(([&] {
        received += primary->readAllStandardOutput();
        return received.count('\n') == 2;
    })());
    QVERIFY(received.contains(image.toUtf8()));
    QVERIFY(received.contains("second.png"));
}

class FakePortalRequest : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.portal.Request")

public:
    int closes = 0;

public slots:
    void Close() { ++closes; }
};

class FakePortal : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.portal.FileChooser")

public:
    explicit FakePortal(QDBusConnection connection) : bus(std::move(connection)) {}

    QDBusConnection bus;
    FakePortalRequest request;
    QString handle, client;
    int opens = 0;
    bool alternate = false;
    bool delayReply = false;
    QDBusMessage delayedReply;
    QStringList earlyUris;

    void respond(uint code, const QStringList &uris = {})
    {
        auto signal = QDBusMessage::createTargetedSignal(client, handle, "org.freedesktop.portal.Request", "Response");
        signal << code << QVariantMap{{"uris", uris}};
        bus.send(signal);
    }

public slots:
    QDBusObjectPath OpenFile(const QString &, const QString &, const QVariantMap &options)
    {
        client = message().service();
        QString sender = client.mid(1);
        sender.replace('.', '_');
        QString token = options.value("handle_token").toString();
        if (alternate)
            token += "_actual";
        handle = "/org/freedesktop/portal/desktop/request/" + sender + '/' + token;
        bus.registerObject(handle, &request, QDBusConnection::ExportAllSlots);
        ++opens;
        if (!earlyUris.isEmpty())
            respond(0, earlyUris);
        const QDBusObjectPath path(handle);
        if (delayReply) {
            setDelayedReply(true);
            delayedReply = message().createReply(QVariant::fromValue(path));
        }
        return path;
    }
};

void ViewerTests::catalogsKeepMatchingKeysAndPlaceholders()
{
    QJsonObject first;
    for (const auto *locale : {"sv", "da"}) {
        QFile file(QString(":/i18n/%1.json").arg(locale));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto catalog = QJsonDocument::fromJson(file.readAll()).object();
        QVERIFY(!catalog.isEmpty());
        if (first.isEmpty())
            first = catalog;
        else
            QCOMPARE(catalog.keys(), first.keys());
        const QRegularExpression parameter("%[1-9][0-9]*");
        const auto parameters = [&](const QString &text) {
            QStringList values;
            auto matches = parameter.globalMatch(text);
            while (matches.hasNext())
                values << matches.next().captured();
            values.sort();
            return values;
        };
        for (auto it = catalog.cbegin(); it != catalog.cend(); ++it) {
            QVERIFY2(it.value().isString() && !it.value().toString().isEmpty(), qPrintable(it.key()));
            QCOMPARE(parameters(it.key()), parameters(it.value().toString()));
        }
    }
}

void ViewerTests::translatorReloadsDuringLookups()
{
    QTemporaryDir dir;
    const auto write = [&](const QString &name, const QByteArray &data) {
        QSaveFile file(dir.filePath(name));
        return file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.commit();
    };
    QVERIFY(write("language", "sv"));
    QVERIFY(write("sv.json", R"({"Save":"Spara"})"));
    QVERIFY(write("da.json", R"({"Save":"Gem"})"));
    Translator translator(dir.filePath("language"), dir.path());
    const auto translate = [&] { return translator.translate("", "Save", nullptr, -1); };
    QCOMPARE(translate(), "Spara");
    QVERIFY(translator.translate("", "Missing", nullptr, -1).isNull());

    std::atomic<bool> invalid{false};
    std::atomic<int> lookups{0};
    std::jthread worker([&](std::stop_token stop) {
        while (!stop.stop_requested()) {
            const auto text = translate();
            if (text != "Spara" && text != "Gem" && !text.isNull())
                invalid = true;
            ++lookups;
        }
    });
    QTRY_VERIFY(lookups > 0);
    int changes = 0;
    connect(&translator, &Translator::changed, this, [&] {
        // Retranslation can immediately call back into the production translator.
        translate();
        ++changes;
    });
    for (int index = 0; index < 4; ++index) {
        QVERIFY(write("language", "da"));
        QTRY_COMPARE(translate(), "Gem");
        QVERIFY(write("language", "sv"));
        QTRY_COMPARE(translate(), "Spara");
    }
    QVERIFY(write("sv.json", "malformed"));
    QTRY_VERIFY(translate().isNull());
    QVERIFY(write("language", "en"));
    QVERIFY(!invalid);
    QVERIFY(changes >= 9);
}

void ViewerTests::portalRecoversFromCancelInvalidReplyAndServiceLoss()
{
    if (!qEnvironmentVariableIsSet("VIEWER_TEST_ISOLATED_BUS"))
        QSKIP("Portal tests require the private session bus.");
    QTemporaryDir dir;
    const auto path = dir.filePath("chosen.png");
    QVERIFY(fixture().save(path));
    const QStringList uris{QUrl::fromLocalFile(path).toString()};
    const QString connectionName = "viewer-portal-fixture";
    auto bus = QDBusConnection::connectToBus(QDBusConnection::SessionBus, connectionName);
    const auto disconnect = qScopeGuard([&] { QDBusConnection::disconnectFromBus(connectionName); });
    FakePortal portal(bus);
    QVERIFY(bus.registerService("org.freedesktop.portal.Desktop"));
    QVERIFY(bus.registerObject("/org/freedesktop/portal/desktop", &portal, QDBusConnection::ExportAllSlots));
    Backend backend;

    portal.alternate = true;
    portal.delayReply = true;
    backend.choose();
    QTRY_COMPARE(portal.opens, 1);
    QVERIFY(backend.picking());
    backend.cancelPicker();
    QVERIFY(!backend.picking());
    QVERIFY(bus.send(portal.delayedReply));
    QTRY_COMPARE(portal.request.closes, 1);

    portal.delayReply = false;
    portal.alternate = false;
    backend.choose();
    QTRY_COMPARE(portal.opens, 2);
    portal.respond(0);
    QTRY_VERIFY(!backend.picking());
    QVERIFY(backend.status().contains("did not return"));

    portal.earlyUris = uris;
    backend.choose();
    QTRY_COMPARE(portal.opens, 3);
    QTRY_COMPARE(backend.name(), "chosen.png");
    QVERIFY(!backend.picking());

    portal.earlyUris.clear();
    backend.choose();
    QTRY_COMPARE(portal.opens, 4);
    QVERIFY(bus.unregisterService("org.freedesktop.portal.Desktop"));
    QTRY_VERIFY(!backend.picking());
    QVERIFY(backend.status().contains("unavailable"));
    QCOMPARE(backend.name(), "chosen.png");
}

#include "integration-tests.moc"
