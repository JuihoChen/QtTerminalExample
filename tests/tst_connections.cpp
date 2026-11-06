#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>

#include "sshconnection.h"

// The connections.json format: each entry is written by SSHConnection::toJson()
// and read back by SSHConnection::fromJson(), which TerminalWindow uses for
// the whole file.
class TestConnections : public QObject
{
    Q_OBJECT

private slots:
    void defaults()
    {
        SSHConnection c;
        QCOMPARE(c.port, 22);
        QVERIFY(!c.useKeyAuth);
        QVERIFY(c.useAgent);
    }

    void roundTrip()
    {
        SSHConnection c("Prod web", "10.1.2.3", "deploy", 2222, "Production", "pä$$ 'w\"");
        c.useKeyAuth = true;
        c.identityFile = "~/.ssh/id_ed25519";
        c.useAgent = false;

        // Through text as well, as when saved to disk and loaded again
        QByteArray text = QJsonDocument(c.toJson()).toJson();
        SSHConnection back = SSHConnection::fromJson(QJsonDocument::fromJson(text).object());
        QVERIFY(back == c);
    }

    void keysWritten()
    {
        QJsonObject obj = SSHConnection("n", "h", "u").toJson();
        const QStringList expected = {"name", "host", "username", "password", "port",
                                      "folder", "useKeyAuth", "identityFile", "useAgent"};
        for (const QString &key : expected) {
            QVERIFY2(obj.contains(key), qPrintable(key));
        }
        QCOMPARE(obj.size(), expected.size());
    }

    // A file from an early version, before folders, ports, passwords and SSH
    // keys were saved, must load with the same defaults as a new connection
    void oldFileLoads()
    {
        const QByteArray oldFile = R"({
            "version": "1.0",
            "connections": [
                {"name": "Old box", "host": "old.example.com", "username": "pi"},
                {"name": "With port", "host": "h", "username": "u", "port": 2022, "folder": "Lab"}
            ]
        })";
        QJsonArray list = QJsonDocument::fromJson(oldFile).object()["connections"].toArray();
        QCOMPARE(list.size(), 2);

        SSHConnection a = SSHConnection::fromJson(list[0].toObject());
        QCOMPARE(a.name, QString("Old box"));
        QCOMPARE(a.host, QString("old.example.com"));
        QCOMPARE(a.username, QString("pi"));
        QCOMPARE(a.port, 22);
        QVERIFY(a.folder.isEmpty());
        QVERIFY(a.password.isEmpty());
        QVERIFY(!a.useKeyAuth);
        QVERIFY(a.identityFile.isEmpty());
        QVERIFY(a.useAgent);

        SSHConnection b = SSHConnection::fromJson(list[1].toObject());
        QCOMPARE(b.port, 2022);
        QCOMPARE(b.folder, QString("Lab"));
    }

    // Wrong types fall back to defaults rather than garbage
    void badTypes()
    {
        QJsonObject obj;
        obj["name"] = 5;
        obj["port"] = "2222";
        obj["useAgent"] = "no";
        SSHConnection c = SSHConnection::fromJson(obj);
        QVERIFY(c.name.isEmpty());
        QCOMPARE(c.port, 22);
        QVERIFY(c.useAgent);
    }

    void equality()
    {
        SSHConnection a("n", "h", "u");
        SSHConnection b = a;
        QVERIFY(a == b);
        b.useAgent = false;
        QVERIFY(!(a == b));
        b = a;
        b.identityFile = "/k";
        QVERIFY(!(a == b));
    }
};

QTEST_GUILESS_MAIN(TestConnections)
#include "tst_connections.moc"
