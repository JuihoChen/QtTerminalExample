#include <QtTest>
#include <QTemporaryFile>

#include "commandsafety.h"
#include "connectionvalidator.h"
#include "ssherrorhandler.h"

class TestCommandSafety : public QObject
{
    Q_OBJECT

private slots:
    void escapeShellArgument_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("expected");
        QTest::newRow("empty") << "" << "''";
        QTest::newRow("plain") << "server01" << "server01";
        QTest::newRow("path") << "/home/user/file.txt" << "/home/user/file.txt";
        QTest::newRow("space") << "my file" << "'my file'";
        QTest::newRow("dollar") << "$HOME" << "'$HOME'";
        QTest::newRow("semicolon") << "a;rm -rf /" << "'a;rm -rf /'";
        QTest::newRow("backtick") << "`id`" << "'`id`'";
        QTest::newRow("single quote") << "it's" << "'it'\"'\"'s'";
        QTest::newRow("tilde") << "~/x" << "'~/x'";
    }

    void escapeShellArgument()
    {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        QCOMPARE(CommandSafetyHelper::escapeShellArgument(input), expected);
    }

    // The escaped text must come back unchanged through a real shell
    void escapeShellArgumentRoundTrip_data()
    {
        QTest::addColumn<QString>("input");
        QTest::newRow("quotes") << "a'b\"c";
        QTest::newRow("specials") << "x; echo $(id) `id` | & > < * ? ~ #";
        QTest::newRow("newline") << "line1\nline2";
        QTest::newRow("backslash") << "C:\\path\\to";
    }

    void escapeShellArgumentRoundTrip()
    {
        QFETCH(QString, input);
        QProcess sh;
        sh.start("/bin/sh", QStringList() << "-c"
                 << "printf '%s' " + CommandSafetyHelper::escapeShellArgument(input));
        QVERIFY(sh.waitForFinished(5000));
        QCOMPARE(QString::fromUtf8(sh.readAllStandardOutput()), input);
    }

    void expandHomePath()
    {
        QCOMPARE(CommandSafetyHelper::expandHomePath("~"), QDir::homePath());
        QCOMPARE(CommandSafetyHelper::expandHomePath("~/.ssh/id_rsa"), QDir::homePath() + "/.ssh/id_rsa");
        QCOMPARE(CommandSafetyHelper::expandHomePath("  ~/k  "), QDir::homePath() + "/k");
        QCOMPARE(CommandSafetyHelper::expandHomePath("/etc/key"), QString("/etc/key"));
        QCOMPARE(CommandSafetyHelper::expandHomePath("~other/key"), QString("~other/key"));
        QCOMPARE(CommandSafetyHelper::expandHomePath(""), QString());
    }

    void sshCommandPlain()
    {
        SSHConnection c("Web", "web.example.com", "alice");
        QCOMPARE(CommandSafetyHelper::buildSafeSSHCommand(c),
                 QString("ssh -o ServerAliveInterval=60 -o ServerAliveCountMax=3 "
                         "-o StrictHostKeyChecking=accept-new alice@web.example.com"));
    }

    void sshCommandPortAndRemoteCommand()
    {
        SSHConnection c("Web", "10.0.0.5", "bob", 2222);
        QString cmd = CommandSafetyHelper::buildSafeSSHCommand(c, "ls -la /tmp");
        QVERIFY2(cmd.contains("-p 2222 "), qPrintable(cmd));
        QVERIFY2(cmd.endsWith("bob@10.0.0.5 'ls -la /tmp'"), qPrintable(cmd));
    }

    void tunnelOptions()
    {
        SSHConnection c("BCM", "10.254.192.18", "root");
        SSHTunnel bmc;
        bmc.listenPort = 8443; bmc.targetHost = "10.254.196.33"; bmc.targetPort = 443;
        SSHTunnel socks;
        socks.type = SSHTunnel::Dynamic; socks.listenPort = 1080;
        SSHTunnel back;
        back.type = SSHTunnel::Remote; back.listenPort = 18000; back.targetPort = 8000;
        SSHTunnel off = bmc;
        off.listenPort = 9443; off.enabled = false;
        SSHTunnel v6 = bmc;
        v6.listenPort = 9444; v6.targetHost = "fe80::1";
        SSHTunnel bad = bmc;
        bad.listenPort = 9445; bad.targetHost = "x;rm -rf /";

        QString cmd = CommandSafetyHelper::buildSafeSSHCommand(c, QString(),
            {bmc, socks, back, off, v6, bad});
        QVERIFY2(cmd.contains("-o ExitOnForwardFailure=no -L 8443:10.254.196.33:443 -D 1080 "
                              "-R 18000:localhost:8000 -L '9444:[fe80::1]:443' root@"), qPrintable(cmd));
        QVERIFY2(!cmd.contains("9443"), qPrintable(cmd));   // switched off
        QVERIFY2(!cmd.contains("rm -rf"), qPrintable(cmd)); // invalid host left out
        QVERIFY(!bad.problem().isEmpty());

        // No tunnels: the command is unchanged
        QVERIFY(!CommandSafetyHelper::buildSafeSSHCommand(c).contains("Forward"));
    }

    // The password goes through the environment, never the command line
    void passwordStaysOffCommandLine()
    {
        SSHConnection c("Db", "db", "root", 22, "", "s3cr3t pass");
        QString cmd = CommandSafetyHelper::buildSafeSSHCommand(c);
        QVERIFY(cmd.startsWith("sshpass -e ssh "));
        QVERIFY(!cmd.contains("s3cr3t"));
        QCOMPARE(CommandSafetyHelper::passwordEnvironment(c),
                 QStringList() << "SSHPASS=s3cr3t pass");

        SSHConnection noPass("Db", "db", "root");
        QVERIFY(CommandSafetyHelper::buildPasswordPrefix(noPass).isEmpty());
        QVERIFY(CommandSafetyHelper::passwordEnvironment(noPass).isEmpty());
    }

    void keyAuthOptions()
    {
        SSHConnection c("K", "h", "u");
        c.useKeyAuth = true;
        c.identityFile = "~/.ssh/my key";
        c.useAgent = true;
        QString opts = CommandSafetyHelper::buildCommonOptions(c);
        QVERIFY2(opts.contains(QString("-i '%1/.ssh/my key' ").arg(QDir::homePath())), qPrintable(opts));
        QVERIFY(!opts.contains("IdentityAgent=none"));
        QVERIFY(!opts.contains("IdentitiesOnly"));

        c.useAgent = false;
        opts = CommandSafetyHelper::buildCommonOptions(c);
        QVERIFY(opts.contains("-o IdentityAgent=none "));
        QVERIFY(opts.contains("-o IdentitiesOnly=yes "));

        // Agent off with no key file: no IdentitiesOnly, which would offer nothing
        c.identityFile.clear();
        opts = CommandSafetyHelper::buildCommonOptions(c);
        QVERIFY(!opts.contains("-i "));
        QVERIFY(opts.contains("-o IdentityAgent=none "));
        QVERIFY(!opts.contains("IdentitiesOnly"));

        // Key settings are ignored while key authentication is off
        c.useKeyAuth = false;
        c.identityFile = "/k";
        opts = CommandSafetyHelper::buildCommonOptions(c);
        QVERIFY(!opts.contains("-i ") && !opts.contains("IdentityAgent"));
    }

    void scpCommand()
    {
        SSHConnection c("S", "host", "user", 2200);
        QString up = CommandSafetyHelper::buildSafeSCPCommand(c, "/tmp/a b", "/srv/x", true);
        QVERIFY2(up.startsWith("scp "), qPrintable(up));
        QVERIFY2(up.contains("-P 2200 "), qPrintable(up));
        QVERIFY2(up.endsWith("'/tmp/a b' user@host:/srv/x"), qPrintable(up));

        QString down = CommandSafetyHelper::buildSafeSCPCommand(c, "/tmp/a", "/srv/my file", false);
        QVERIFY2(down.endsWith("user@host:'/srv/my file' /tmp/a"), qPrintable(down));
    }

    void validatorRejects_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<QString>("host");
        QTest::addColumn<QString>("user");
        QTest::addColumn<int>("port");
        QTest::addColumn<QString>("error");
        QTest::newRow("no name") << " " << "h" << "u" << 22 << "Connection name cannot be empty";
        QTest::newRow("no host") << "n" << "" << "u" << 22 << "Host cannot be empty";
        QTest::newRow("no user") << "n" << "h" << "" << 22 << "Username cannot be empty";
        QTest::newRow("port 0") << "n" << "h" << "u" << 0 << "Port must be between 1 and 65535";
        QTest::newRow("port 65536") << "n" << "h" << "u" << 65536 << "Port must be between 1 and 65535";
    }

    void validatorRejects()
    {
        QFETCH(QString, name);
        QFETCH(QString, host);
        QFETCH(QString, user);
        QFETCH(int, port);
        QFETCH(QString, error);
        auto r = ConnectionValidator::validateConnection(SSHConnection(name, host, user, port));
        QVERIFY(!r.isValid);
        QCOMPARE(r.errorMessage, error);
    }

    void validatorWarnings()
    {
        SSHConnection c("n", "h", "u", 65535, "", "pw");
        auto r = ConnectionValidator::validateConnection(c);
        QVERIFY(r.isValid);
        QCOMPARE(r.warnings, QStringList() << "Using non-standard SSH port: 65535");

        c = SSHConnection("n", "h", "u", 22);
        r = ConnectionValidator::validateConnection(c);
        QVERIFY(r.isValid);
        QCOMPARE(r.warnings.size(), 1);
        QVERIFY(r.warnings.first().startsWith("No password set"));

        // A readable key file gives no warning, a missing one does
        QTemporaryFile key;
        QVERIFY(key.open());
        c.useKeyAuth = true;
        c.identityFile = key.fileName();
        QVERIFY(ConnectionValidator::validateConnection(c).warnings.isEmpty());
        c.identityFile = "/nonexistent/key";
        r = ConnectionValidator::validateConnection(c);
        QCOMPARE(r.warnings.size(), 1);
        QVERIFY(r.warnings.first().startsWith("SSH key file not readable"));

        c.identityFile.clear();
        c.useAgent = false;
        r = ConnectionValidator::validateConnection(c);
        QCOMPARE(r.warnings.size(), 1);
        QVERIFY(r.warnings.first().contains("SSH agent is disabled"));
    }

    void errorDescriptions()
    {
        QCOMPARE(SSHErrorHandler::getErrorDescription(0), QString("Operation completed successfully"));
        QVERIFY(SSHErrorHandler::getErrorDescription(255).startsWith("Authentication failed"));
        QCOMPARE(SSHErrorHandler::getErrorDescription(7), QString("Unknown error (code: 7)"));
    }
};

QTEST_GUILESS_MAIN(TestCommandSafety)
#include "tst_commandsafety.moc"
