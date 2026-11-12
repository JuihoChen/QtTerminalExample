#include <QtTest>

#include "proxybrowser.h"

class TestProxyBrowser : public QObject
{
    Q_OBJECT

private slots:
    void normalizeUrl_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("expected");
        QTest::newRow("empty") << "" << "";
        QTest::newRow("spaces") << "   " << "";
        QTest::newRow("ip") << "10.0.0.5" << "https://10.0.0.5/";
        QTest::newRow("ip with path") << "10.0.0.5/login" << "https://10.0.0.5/login";
        QTest::newRow("host:port") << " bmc-01:8443 " << "https://bmc-01:8443/";
        QTest::newRow("http kept") << "http://10.0.0.5/" << "http://10.0.0.5/";
        QTest::newRow("https kept") << "https://bmc/redfish" << "https://bmc/redfish";
    }

    void normalizeUrl()
    {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        QCOMPARE(ProxyBrowser::normalizeUrl(input), expected);
    }

    void arguments()
    {
        QStringList args = ProxyBrowser::arguments("/tmp/profile", 1080, "10.1.2.3");
        QVERIFY(args.contains("--user-data-dir=/tmp/profile"));
        QVERIFY(args.contains("--proxy-server=socks5://127.0.0.1:1080"));
        QCOMPARE(args.last(), QString("https://10.1.2.3/"));

        // No address: no URL argument, so the browser opens its blank start page
        args = ProxyBrowser::arguments("/tmp/profile", 2000, "");
        QVERIFY(args.contains("--proxy-server=socks5://127.0.0.1:2000"));
        for (const QString &a : args) QVERIFY(a.startsWith("--"));
    }

    void rememberPutsLatestFirst()
    {
        ProxyBrowser::remember(1234, "10.0.0.1");
        ProxyBrowser::remember(1234, "10.0.0.2");
        ProxyBrowser::remember(1234, "https://10.0.0.1/");  // Same as the first: moves to the front
        ProxyBrowser::remember(1234, "");                   // Ignored
        QCOMPARE(ProxyBrowser::recentUrls(1234),
                 QStringList({"https://10.0.0.1/", "https://10.0.0.2/"}));
        QVERIFY(ProxyBrowser::recentUrls(1235).isEmpty());  // Per port
    }

    void namesAndRackSizedList()
    {
        for (int i = 1; i <= 18; ++i)
            ProxyBrowser::remember(4000, QString("10.141.8.%1").arg(i), QString("bmc-%1").arg(i));
        QCOMPARE(ProxyBrowser::recentUrls(4000).size(), 18);  // A whole rack is kept
        QCOMPARE(ProxyBrowser::nameFor(4000, "https://10.141.8.7/"), QString("bmc-7"));
        ProxyBrowser::remember(4000, "10.141.8.7");  // No name given: the known one stays
        QCOMPARE(ProxyBrowser::nameFor(4000, "10.141.8.7"), QString("bmc-7"));

        for (int i = 0; i < ProxyBrowser::kMaxRecentUrls + 5; ++i)
            ProxyBrowser::remember(4001, QString("10.0.%1.1").arg(i), "n");
        QCOMPARE(ProxyBrowser::recentUrls(4001).size(), ProxyBrowser::kMaxRecentUrls);
        QVERIFY(ProxyBrowser::nameFor(4001, "10.0.0.1").isEmpty());  // Dropped with its address
    }

    void sortedByAddress()
    {
        QStringList urls = {"https://bmc-b/", "https://10.0.0.10/", "https://10.0.0.9/",
                            "https://bmc-a/", "http://10.0.0.9:8080/"};
        QCOMPARE(ProxyBrowser::sortedByAddress(urls),
                 QStringList({"http://10.0.0.9:8080/", "https://10.0.0.9/", "https://10.0.0.10/",
                              "https://bmc-a/", "https://bmc-b/"}));
    }

    void label()
    {
        QCOMPARE(ProxyBrowser::label("https://10.0.0.5/", "bmc-01"), QString("bmc-01 · 10.0.0.5"));
        QCOMPARE(ProxyBrowser::label("https://10.0.0.5/", ""), QString("10.0.0.5"));
        QCOMPARE(ProxyBrowser::label("http://10.0.0.5:8080/", ""), QString("http://10.0.0.5:8080/"));
        QCOMPARE(ProxyBrowser::label("https://bmc-01/", "bmc-01"), QString("bmc-01"));
    }

    void noBrowserGivesClearError()
    {
        qputenv("PATH", "/nonexistent");
        qunsetenv("WSL_DISTRO_NAME");
        QVERIFY(!ProxyBrowser::find().isValid());
        QString error;
        QVERIFY(!ProxyBrowser::launch(1080, "", &error));
        QVERIFY(error.contains("No Chrome-family browser"));
        QVERIFY(error.contains("1080"));
    }
};

QTEST_MAIN(TestProxyBrowser)
#include "tst_proxybrowser.moc"
