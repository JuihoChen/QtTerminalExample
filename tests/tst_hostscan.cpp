#include <QtTest>
#include <QTcpServer>

#include "hostscantab.h"

class TestHostScan : public QObject
{
    Q_OBJECT

private slots:
    void parseIPv4_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<bool>("ok");
        QTest::addColumn<quint32>("address");
        QTest::newRow("private") << "192.168.1.10" << true << quint32(0xC0A8010A);
        QTest::newRow("zero") << "0.0.0.0" << true << quint32(0);
        QTest::newRow("broadcast") << "255.255.255.255" << true << quint32(0xFFFFFFFF);
        QTest::newRow("leading zero") << "010.0.0.1" << true << quint32(0x0A000001);
        QTest::newRow("octet too big") << "192.168.1.256" << false << quint32(0);
        QTest::newRow("three parts") << "10.0.1" << false << quint32(0);
        QTest::newRow("five parts") << "1.2.3.4.5" << false << quint32(0);
        QTest::newRow("empty part") << "1..3.4" << false << quint32(0);
        QTest::newRow("trailing dot") << "1.2.3.4." << false << quint32(0);
        QTest::newRow("letters") << "1.2.3.a" << false << quint32(0);
        QTest::newRow("negative") << "1.2.3.-4" << false << quint32(0);
        QTest::newRow("four digits") << "1.2.3.0004" << false << quint32(0);
        QTest::newRow("hostname") << "example.com" << false << quint32(0);
        QTest::newRow("empty") << "" << false << quint32(0);
        QTest::newRow("cidr") << "10.0.0.0/24" << false << quint32(0);
    }

    void parseIPv4()
    {
        QFETCH(QString, text);
        QFETCH(bool, ok);
        QFETCH(quint32, address);
        quint32 result = 12345;
        QCOMPARE(HostScanTab::parseIPv4(text, &result), ok);
        if (ok) {
            QCOMPARE(result, address);
        } else {
            QCOMPARE(result, quint32(12345));  // Left alone on failure
        }
    }

    void formatIPv4()
    {
        QCOMPARE(HostScanTab::formatIPv4(0xC0A8010A), QString("192.168.1.10"));
        QCOMPARE(HostScanTab::formatIPv4(0), QString("0.0.0.0"));
        QCOMPARE(HostScanTab::formatIPv4(0xFFFFFFFF), QString("255.255.255.255"));
    }

    void formatParseRoundTrip()
    {
        for (quint32 a : {0x01020304u, 0x7F000001u, 0xAC10FE01u, 0x0A0000FFu}) {
            quint32 back = 0;
            QVERIFY(HostScanTab::parseIPv4(HostScanTab::formatIPv4(a), &back));
            QCOMPARE(back, a);
        }
    }

    void scriptHeader()
    {
        QString script = HostScanTab::scanScript(0x0A000001, 0x0A0000FE, 2222, 3);
        QVERIFY2(script.startsWith(QString("S=%1 E=%2 P=2222 T=3 J=")
                                   .arg(0x0A000001u).arg(0x0A0000FEu)), qPrintable(script.left(80)));
        QVERIFY(script.trimmed().endsWith("echo END"));
    }

    void scriptIsValidShell()
    {
        QProcess sh;
        sh.start("/bin/sh", QStringList() << "-n" << "-c"
                 << HostScanTab::scanScript(1, 2, 22, 1));
        QVERIFY(sh.waitForFinished(5000));
        QCOMPARE(sh.exitCode(), 0);
    }

    // Runs the scan script on this machine against 127.0.0.1 and a port
    // that the test listens on, then against one nothing listens on
    void scriptFindsLocalPort()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QString open = runScan(server.serverPort());
        if (open == "noportcheck") {
            QSKIP("Neither bash with timeout nor nc is available");
        }
        QCOMPARE(open, QString("OPEN"));

        // Close it again: the same port now refuses (or is reported closed
        // where neither bash nor nc can tell the difference)
        quint16 port = server.serverPort();
        server.close();
        QString state = runScan(port);
        QVERIFY2(state == "REFUSED" || state == "CLOSED", qPrintable(state));
    }

private:
    // The script's port state for 127.0.0.1, or "" when it printed none
    QString runScan(quint16 port)
    {
        const quint32 localhost = 0x7F000001;
        QProcess sh;
        // The script itself reaches the port while this test's event loop is
        // not running, which is fine: the kernel accepts the connection
        sh.start("/bin/bash", QStringList() << "-c"
                 << HostScanTab::scanScript(localhost, localhost, port, 2));
        if (!sh.waitForFinished(20000)) return "hung";
        QString out = QString::fromUtf8(sh.readAllStandardOutput());
        if (out.contains("NOPORTCHECK")) return "noportcheck";
        if (!out.contains("DONE 127.0.0.1") || !out.contains("\nEND\n")) {
            qWarning().noquote() << out;
            return "incomplete";
        }
        for (const QString &line : out.split('\n')) {
            if (line.endsWith(" 127.0.0.1") && !line.startsWith("UP") && !line.startsWith("DONE")) {
                return line.section(' ', 0, 0);
            }
        }
        return QString();
    }
};

QTEST_MAIN(TestHostScan)
#include "tst_hostscan.moc"
