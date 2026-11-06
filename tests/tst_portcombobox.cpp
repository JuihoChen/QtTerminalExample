#include <QtTest>
#include <QLineEdit>

#include "portcombobox.h"

class TestPortComboBox : public QObject
{
    Q_OBJECT

private slots:
    void startsAt22()
    {
        PortComboBox box;
        QCOMPARE(box.value(), 22);
        QCOMPARE(box.currentText(), QString("22"));
    }

    void setValue()
    {
        PortComboBox box;
        box.setValue(8080);
        QCOMPARE(box.value(), 8080);
        QCOMPARE(box.currentText(), QString("8080"));  // Not "8080  (HTTP alt)"

        box.setValue(12345);  // Not in the list
        QCOMPARE(box.currentIndex(), -1);
        QCOMPARE(box.value(), 12345);
    }

    void pickingItemShowsNumber()
    {
        PortComboBox box;
        int index = box.findData(443);
        QVERIFY(index >= 0);
        box.setCurrentIndex(index);
        QCOMPARE(box.currentText(), QString("443"));
        QCOMPARE(box.value(), 443);
    }

    void value_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<int>("port");
        QTest::newRow("valid") << "2222" << 2222;
        QTest::newRow("spaces") << " 2222 " << 2222;
        QTest::newRow("min") << "1" << 1;
        QTest::newRow("max") << "65535" << 65535;
        QTest::newRow("empty") << "" << 0;
        QTest::newRow("zero") << "0" << 0;
        QTest::newRow("too big") << "65536" << 0;
        QTest::newRow("text") << "ssh" << 0;
    }

    // value() is 0 for anything that is not a usable port
    void value()
    {
        QFETCH(QString, text);
        QFETCH(int, port);
        PortComboBox box;
        box.setEditText(text);  // Bypasses the validator, like a pasted value
        QCOMPARE(box.value(), port);
    }

    void validatorLimitsTyping()
    {
        PortComboBox box;
        box.lineEdit()->clear();
        QTest::keyClicks(box.lineEdit(), "99999");
        QVERIFY(box.value() == 0 || box.value() <= 65535);
        QVERIFY(box.lineEdit()->text() != "99999" || !box.lineEdit()->hasAcceptableInput());
    }
};

QTEST_MAIN(TestPortComboBox)
#include "tst_portcombobox.moc"
