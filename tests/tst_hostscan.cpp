#include <QtTest>
#include <QTcpServer>
#include <QClipboard>
#include <QGuiApplication>
#include <QTreeWidget>

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

    void scriptIsValidShell_data()
    {
        QTest::addColumn<bool>("findBmcs");
        QTest::newRow("ssh only") << false;
        QTest::newRow("find BMCs") << true;
    }

    void scriptIsValidShell()
    {
        QFETCH(bool, findBmcs);
        QString script = HostScanTab::scanScript(1, 2, 22, 1, findBmcs);
        QCOMPARE(script.section('\n', 0, 0).endsWith(findBmcs ? " W=1" : " W="), true);
        QProcess sh;
        sh.start("/bin/sh", QStringList() << "-n" << "-c" << script);
        QVERIFY(sh.waitForFinished(5000));
        QCOMPARE(sh.exitCode(), 0);
    }

    // The service roots of a few real BMCs, shortened
    void redfishSummary_data()
    {
        QTest::addColumn<QByteArray>("json");
        QTest::addColumn<QString>("summary");
        QTest::newRow("vendor and product")
            << QByteArray(R"({"@odata.id":"/redfish/v1","Vendor":"NVIDIA","Product":"GB300 NVL72","RedfishVersion":"1.17.0"})")
            << "NVIDIA GB300 NVL72";
        QTest::newRow("product repeats vendor")
            << QByteArray(R"({"Vendor":"Dell","Product":"Dell iDRAC9"})") << "Dell iDRAC9";
        QTest::newRow("version only")
            << QByteArray(R"({"@odata.id":"/redfish/v1","RedfishVersion":"1.6.0"})") << "Redfish 1.6.0";
        QTest::newRow("not json") << QByteArray("<html>login</html>") << QString();
    }

    void redfishSummary()
    {
        QFETCH(QByteArray, json);
        QFETCH(QString, summary);
        QCOMPARE(HostScanTab::redfishSummary(json), summary);
    }

    void pickLocalPorts()
    {
        QSet<int> taken = {18443, 18445};
        auto all = [](int) { return true; };
        QCOMPARE(HostScanTab::pickLocalPorts(3, 18443, taken, all), (QList<int>{18444, 18446, 18447}));
        auto noOdd = [](int port) { return port % 2 == 0; };
        QCOMPARE(HostScanTab::pickLocalPorts(2, 18443, taken, noOdd), (QList<int>{18444, 18446}));
        // Never below 1024, which needs admin rights
        QCOMPARE(HostScanTab::pickLocalPorts(1, 443, QSet<int>(), all), QList<int>{1024});
    }

    // The IP cell can carry a "(saved)" mark; copies get just the address
    void cellTextDropsSavedMark()
    {
        QTreeWidgetItem row;
        row.setText(0, "10.254.192.16  (saved)");
        row.setData(0, Qt::UserRole, quint32(0x0AFEC010));
        row.setText(1, "00:e0:4c:36:00:0b");
        QCOMPARE(HostScanTab::cellText(&row, 0), QString("10.254.192.16"));
        QCOMPARE(HostScanTab::cellText(&row, 1), QString("00:e0:4c:36:00:0b"));
    }

    // Ctrl+C copies the clicked column of the selected rows, in list order
    void copySelectedAddresses()
    {
        HostScanTab tab{QList<SSHConnection>(), QStringList()};
        QTreeWidget *list = tab.findChild<QTreeWidget *>();
        QVERIFY(list);
        QList<QTreeWidgetItem *> rows;
        for (quint32 last : {1u, 16u, 32u}) {
            QTreeWidgetItem *row = new QTreeWidgetItem(list);
            row->setText(0, HostScanTab::formatIPv4(0x0AFEC000 + last));
            row->setData(0, Qt::UserRole, 0x0AFEC000 + last);
            row->setText(1, QString("00:00:00:00:00:%1").arg(last, 2, 16, QChar('0')));
            row->setText(2, "Yes");
            rows << row;
        }
        rows[2]->setSelected(true);
        rows[0]->setSelected(true);
        list->setCurrentItem(rows[2], 1, QItemSelectionModel::NoUpdate);
        tab.copySelection();
        QCOMPARE(QGuiApplication::clipboard()->text(), QString("00:00:00:00:00:01\n00:00:00:00:00:20"));
        list->setCurrentItem(rows[2], 0, QItemSelectionModel::NoUpdate);
        tab.copySelection();
        QCOMPARE(QGuiApplication::clipboard()->text(), QString("10.254.192.1\n10.254.192.32"));
        list->setCurrentItem(rows[2], 2, QItemSelectionModel::NoUpdate);
        tab.copySelection();
        QCOMPARE(QGuiApplication::clipboard()->text(),
                 QString("10.254.192.1\t00:00:00:00:00:01\tYes\n10.254.192.32\t00:00:00:00:00:20\tYes"));
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
