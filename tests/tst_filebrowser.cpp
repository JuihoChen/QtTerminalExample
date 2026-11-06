#include <QtTest>
#include <QMimeData>

#include "remotefilemodel.h"
#include "sftpworker.h"

class TestFileBrowser : public QObject
{
    Q_OBJECT

private slots:
    void parentPath_data()
    {
        QTest::addColumn<QString>("path");
        QTest::addColumn<QString>("parent");
        QTest::newRow("root") << "/" << "/";
        QTest::newRow("top level") << "/home" << "/";
        QTest::newRow("nested") << "/home/user/docs" << "/home/user";
        QTest::newRow("trailing slash") << "/home/user/" << "/home";
        QTest::newRow("many slashes") << "/var/log///" << "/var";
    }

    void parentPath()
    {
        QFETCH(QString, path);
        QFETCH(QString, parent);
        QCOMPARE(RemoteFileModel::parentPath(path), parent);
    }

    void formatPermissions_data()
    {
        QTest::addColumn<quint32>("mode");
        QTest::addColumn<bool>("isDir");
        QTest::addColumn<bool>("isLink");
        QTest::addColumn<QString>("text");
        QTest::newRow("file 644") << quint32(0644) << false << false << "-rw-r--r--";
        QTest::newRow("dir 755") << quint32(0755) << true << false << "drwxr-xr-x";
        QTest::newRow("link 777") << quint32(0777) << false << true << "lrwxrwxrwx";
        QTest::newRow("link to dir") << quint32(0777) << true << true << "lrwxrwxrwx";
        QTest::newRow("none") << quint32(0) << false << false << "----------";
        QTest::newRow("key 600") << quint32(0600) << false << false << "-rw-------";
        QTest::newRow("type bits ignored") << quint32(0100640) << false << false << "-rw-r-----";
    }

    void formatPermissions()
    {
        QFETCH(quint32, mode);
        QFETCH(bool, isDir);
        QFETCH(bool, isLink);
        QFETCH(QString, text);
        QCOMPARE(SftpWorker::formatPermissions(mode, isDir, isLink), text);
    }

    void dragDataRoundTrip()
    {
        RemoteEntry a;
        a.name = "notes.txt";
        a.path = "/home/u/notes.txt";
        a.size = 1234567890123LL;
        a.modified = QDateTime::fromSecsSinceEpoch(1700000000);
        a.permissions = 0640;
        a.isDir = false;
        a.isLink = false;
        RemoteEntry b;
        b.name = "src";
        b.path = "/home/u/src";
        b.isDir = true;
        b.isLink = true;

        QScopedPointer<QMimeData> data(RemoteFileModel::encodeEntries({a, b}, quintptr(0xBEEF)));
        QVERIFY(data->hasFormat(REMOTE_ENTRIES_MIME));

        quintptr source = 0;
        QVector<RemoteEntry> back = RemoteFileModel::decodeEntries(data.data(), &source);
        QCOMPARE(source, quintptr(0xBEEF));
        QCOMPARE(back.size(), 2);
        QCOMPARE(back[0].name, a.name);
        QCOMPARE(back[0].path, a.path);
        QCOMPARE(back[0].size, a.size);
        QCOMPARE(back[0].modified, a.modified);
        QCOMPARE(back[0].permissions, a.permissions);
        QCOMPARE(back[1].isDir, true);
        QCOMPARE(back[1].isLink, true);
    }

    // Drag data from elsewhere decodes to nothing rather than crashing
    void foreignDragData()
    {
        QMimeData data;
        data.setText("hello");
        quintptr source = 1;
        QVERIFY(RemoteFileModel::decodeEntries(&data, &source).isEmpty());
        QCOMPARE(source, quintptr(0));

        data.setData(REMOTE_ENTRIES_MIME, QByteArray("\x00\x01", 2));  // truncated
        QVERIFY(RemoteFileModel::decodeEntries(&data, nullptr).size() <= 1);
    }

    // Every folder but "/" gets a ".." row pointing at its parent
    void upEntry()
    {
        RemoteFileModel model;
        RemoteEntry f;
        f.name = "a";
        f.path = "/srv/a";
        model.setEntries("/srv", {f});
        QCOMPARE(model.rowCount(), 2);
        RemoteEntry up = model.entry(model.index(0, 0));
        QVERIFY(RemoteFileModel::isParentEntry(up));
        QCOMPARE(up.path, QString("/"));
        QVERIFY(up.isDir);

        model.setEntries("/", {f});
        QCOMPARE(model.rowCount(), 1);
        QVERIFY(!RemoteFileModel::isParentEntry(model.entry(model.index(0, 0))));
    }
};

QTEST_MAIN(TestFileBrowser)
#include "tst_filebrowser.moc"
