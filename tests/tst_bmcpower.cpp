#include <QtTest>
#include <QProcess>
#include <QTemporaryDir>

#include "bmcpower.h"

using namespace BmcPower;

namespace {

// Stand-ins for curl and ipmitool: a tiny Redfish BMC whose power state is a
// file, and an ipmitool that checks IPMI_PASSWORD. Both log their arguments.
const char *kFakeCurl = R"SH(#!/bin/sh
out=''; method=GET; url=''; data=''
while [ $# -gt 0 ]; do
  case "$1" in
    -o) out=$2; shift ;;
    -X) method=$2; shift ;;
    --data) data=$2; shift ;;
    -w|-H|--connect-timeout|--max-time|--noproxy) shift ;;
    https://*) url=$1 ;;
  esac
  shift
done
cfg=$(cat)
echo "curl $method $url $data" >> "$FAKE/log"
mode=$(cat "$FAKE/mode" 2>/dev/null)
[ "$mode" = unreachable ] && { printf 000; exit 28; }
if [ "$cfg" != "$(cat "$FAKE/expected_cfg")" ]; then
  echo '{"error":{"code":"Base.1.0.GeneralError","message":"Unauthorized"}}' > "$out"; printf 401; exit 0
fi
path=${url#https://*/}
allowed=$(cat "$FAKE/allowed" 2>/dev/null || echo '"On","ForceOff","GracefulShutdown","PowerCycle","ForceRestart"')
case "$method $path" in
  "GET redfish/v1/Systems")
    echo '{"@odata.id": "/redfish/v1/Systems", "Members": [{"@odata.id": "/redfish/v1/Systems/System_0"}]}' > "$out"; printf 200 ;;
  "GET redfish/v1/Systems/System_0")
    printf '{"@odata.id": "/redfish/v1/Systems/System_0",\n "PowerState": "%s",\n "Actions": {"#ComputerSystem.Reset": {"target": "/redfish/v1/Systems/System_0/Actions/ComputerSystem.Reset",\n "ResetType@Redfish.AllowableValues": [%s]}}}\n' "$(cat "$FAKE/state")" "$allowed" > "$out"
    printf 200 ;;
  "POST redfish/v1/Systems/System_0/Actions/ComputerSystem.Reset")
    [ "$mode" = posttimeout ] && { printf 000; exit 28; }
    case "$data" in
      *ForceOff*|*GracefulShutdown*) echo Off > "$FAKE/state" ;;
      *) echo On > "$FAKE/state" ;;
    esac
    : > "$out"; printf 204 ;;
  *) echo '{"error":{"@Message.ExtendedInfo":[{"Message":"No such resource"}]}}' > "$out"; printf 404 ;;
esac
)SH";

const char *kFakeIpmitool = R"SH(#!/bin/sh
echo "ipmitool $*" >> "$FAKE/log"
if [ "$IPMI_PASSWORD" != "$(cat "$FAKE/password")" ]; then
  echo "Error: Unable to establish IPMI v2 / RMCP+ session"; echo "RAKP 2 HMAC is invalid" >&2; exit 1
fi
for last; do :; done
case "$last" in
  status) echo "Chassis Power is $(tr 'A-Z' 'a-z' < "$FAKE/state")" ;;
  off|soft) echo Off > "$FAKE/state"; echo "Chassis Power Control: Down/Off" ;;
  *) echo On > "$FAKE/state"; echo "Chassis Power Control: $last" ;;
esac
)SH";

} // namespace

class TestBmcPower : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    QString m_password = QStringLiteral("p@ss\"w'd $x\\y");

    void write(const QString &name, const QByteArray &content)
    {
        QFile f(m_dir.filePath(name));
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(content);
    }

    QByteArray read(const QString &name)
    {
        QFile f(m_dir.filePath(name));
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    }

    // Runs the script the way the BCM would ("sh -s"), with the fakes on PATH
    QByteArray runScript(Action action, const QString &password, bool withIpmitool = true)
    {
        QFile::remove(m_dir.filePath("ipmitool"));
        if (withIpmitool) {
            write("ipmitool", kFakeIpmitool);
            QFile::setPermissions(m_dir.filePath("ipmitool"), QFile::permissions(m_dir.filePath("curl")));
        }
        Credentials creds{"admin", password};
        QByteArray script = BmcPower::script("10.141.8.101", action, creds);
        script.replace("sleep 6", "sleep 0");
        QProcess p;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert("PATH", m_dir.path() + ":/usr/bin:/bin");
        env.insert("FAKE", m_dir.path());
        p.setProcessEnvironment(env);
        p.start("/bin/sh", {"-s"});
        p.write(script);
        p.closeWriteChannel();
        p.waitForFinished(20000);
        return p.readAllStandardOutput();
    }

    // Runs a script with an ipmitool that logs its arguments and the password
    // file's content, and fails "sol activate" while FAKE/busy exists
    QByteArray runConsole(const QString &file, const QByteArray &input = QByteArray())
    {
        write("ipmitool", R"SH(#!/bin/sh
echo "ipmitool $*" >> "$FAKE/log"
while [ $# -gt 0 ]; do
  case "$1" in -f) echo "pwfile $(cat "$2") $(stat -c %a "$2")" >> "$FAKE/log"; shift ;; esac
  last=$1; shift
done
if [ "$last" = activate ] && [ -e "$FAKE/busy" ]; then echo "Info: SOL payload already active on another session"; rm -f "$FAKE/busy"; exit 1; fi
echo "[SOL Session operational.  Use ~? for help]"
)SH");
        QFile::setPermissions(m_dir.filePath("ipmitool"), QFile::permissions(m_dir.filePath("curl")));
        QProcess p;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert("PATH", m_dir.path() + ":/usr/bin:/bin");
        env.insert("FAKE", m_dir.path());
        env.insert("TMPDIR", m_dir.path());
        p.setProcessEnvironment(env);
        p.start("/bin/sh", {"-c", consoleCommand(file)});
        p.write(input);
        p.closeWriteChannel();
        p.waitForFinished(20000);
        return p.readAllStandardOutput();
    }

    QString makePasswordFile(const QString &name = QString())
    {
        QProcess p;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert("TMPDIR", m_dir.path());
        p.setProcessEnvironment(env);
        p.start("/bin/sh", {"-s"});
        p.write(consoleSetupScript(m_password, consoleScript("10.141.8.101", name, "admin")));
        p.closeWriteChannel();
        p.waitForFinished(10000);
        QString out = QString::fromUtf8(p.readAllStandardOutput());
        int at = out.indexOf("FILE ");
        return at < 0 ? QString() : out.mid(at + 5).trimmed();
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        write("curl", kFakeCurl);
        QFile::setPermissions(m_dir.filePath("curl"), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        QString cfg = m_password;
        cfg.replace("\\", "\\\\").replace("\"", "\\\"");
        write("expected_cfg", QString("user = \"admin:%1\"").arg(cfg).toUtf8());
        write("password", m_password.toUtf8());
    }

    void init()
    {
        write("state", "On\n");
        write("log", "");
        QFile::remove(m_dir.filePath("mode"));
        QFile::remove(m_dir.filePath("allowed"));
        QFile::remove(m_dir.filePath("busy"));
    }

    void statusThroughRedfish()
    {
        Result r = parseOutput(runScript(Status, m_password), Status);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.method, QString("Redfish"));
        QCOMPARE(r.powerState, QString("On"));
        QCOMPARE(r.system, QString("/redfish/v1/Systems/System_0"));
        QByteArray log = read("log");
        QVERIFY(log.contains("curl GET https://10.141.8.101/redfish/v1/Systems/System_0"));
        QVERIFY(!log.contains("ipmitool"));
    }

    void powerCycleThroughRedfish()
    {
        write("state", "Off\n");
        Result r = parseOutput(runScript(Cycle, m_password), Cycle);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.resetType, QString("PowerCycle"));
        QCOMPARE(r.powerState, QString("On"));  // Read again after the action
        QVERIFY(read("log").contains("curl POST https://10.141.8.101/redfish/v1/Systems/System_0/Actions/"
                                     "ComputerSystem.Reset {\"ResetType\":\"PowerCycle\"}"));
    }

    void forceOffThroughRedfish()
    {
        Result r = parseOutput(runScript(ForceOff, m_password), ForceOff);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.resetType, QString("ForceOff"));
        QCOMPARE(r.powerState, QString("Off"));
    }

    void picksAnAllowedResetType()
    {
        write("state", "Off\n");
        write("allowed", "\"ForceOn\", \"ForceOff\"");
        Result r = parseOutput(runScript(On, m_password), On);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.resetType, QString("ForceOn"));
    }

    void notAllowedFallsBackToIpmi()
    {
        write("allowed", "\"On\", \"ForceOff\"");
        Result r = parseOutput(runScript(Cycle, m_password), Cycle);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.method, QString("IPMI (ipmitool)"));
        QCOMPARE(r.resetType, QString("ipmitool power cycle"));
        QVERIFY(read("log").contains("ipmitool -I lanplus -H 10.141.8.101 -U admin -E"));
    }

    void unreachableRedfishFallsBackToIpmi()
    {
        write("mode", "unreachable");
        Result r = parseOutput(runScript(Status, m_password), Status);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.method, QString("IPMI (ipmitool)"));
        QCOMPARE(r.powerState, QString("On"));
    }

    void postWithoutAnswerIsNotRepeatedOverIpmi()
    {
        write("mode", "posttimeout");
        Result r = parseOutput(runScript(Reset, m_password), Reset);
        QVERIFY(!r.ok);
        QVERIFY(r.error.contains("may still have done it"));
        QVERIFY(!read("log").contains("ipmitool"));
    }

    void wrongPasswordIsReported()
    {
        Result r = parseOutput(runScript(Status, "wrong"), Status);
        QVERIFY(!r.ok);
        QVERIFY(r.authFailed);
        QVERIFY2(r.error.contains("refused the username or password"), qPrintable(r.error));
        QVERIFY2(r.error.contains("RAKP") || r.error.contains("IPMI"), qPrintable(r.error));
    }

    void noIpmitoolSaysSo()
    {
        write("mode", "unreachable");
        Result r = parseOutput(runScript(Status, m_password, false), Status);
        QVERIFY(!r.ok);
        QVERIFY2(r.error.contains("no answer"), qPrintable(r.error));
        QVERIFY2(r.error.contains("ipmitool is not installed"), qPrintable(r.error));
    }

    void passwordNeverOnACommandLine()
    {
        runScript(Cycle, m_password);
        write("allowed", "\"On\"");
        runScript(Cycle, m_password);
        QByteArray log = read("log");
        QVERIFY(log.contains("ipmitool"));
        QVERIFY(!log.contains(m_password.toUtf8()));
        QVERIFY(!log.contains("p@ss"));
    }

    void redfishErrorMessageShown()
    {
        QByteArray out = "RF_SYSTEMS 200 {}\n"
                         "RF_SYSTEM /redfish/v1/Systems/1 {\"PowerState\":\"On\"}\n"
                         "RF_POST 400 PowerCycle {\"error\":{\"@Message.ExtendedInfo\":[{\"Message\":\"Host is busy\"}]}}\n"
                         "NOIPMITOOL\nEND\n";
        Result r = parseOutput(out, Cycle);
        QVERIFY(!r.ok);
        QVERIFY2(r.error.contains("Host is busy"), qPrintable(r.error));
        QVERIFY2(r.error.contains("HTTP 400"), qPrintable(r.error));
    }

    void consolePasswordFileIsPrivate()
    {
        QString file = makePasswordFile();
        QVERIFY(!file.isEmpty());
        QVERIFY(file.startsWith(m_dir.path() + "/bmcsol."));
        QFile f(file);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(f.readAll()), m_password + "\n");
        QCOMPARE(f.permissions() & (QFile::ReadGroup | QFile::ReadOther | QFile::WriteGroup | QFile::WriteOther),
                 QFile::Permissions());
        f.remove();
        QFile::remove(file + ".sh");
    }

    void consoleRunsSolWithThePasswordFile()
    {
        QString file = makePasswordFile("gb-bmc");
        QVERIFY(QFile::exists(file + ".sh"));
        QByteArray out = runConsole(file);
        QVERIFY2(out.contains("Serial console of gb-bmc (10.141.8.101)"), out.constData());
        QVERIFY(out.contains("~."));
        QVERIFY(out.contains("SOL Session operational"));
        QByteArray log = read("log");
        QVERIFY2(log.contains("ipmitool -I lanplus -H 10.141.8.101 -U admin -f " + file.toUtf8() + " sol activate"),
                 log.constData());
        QVERIFY(log.contains("pwfile " + m_password.toUtf8() + " 600"));
        QVERIFY(!QFile::exists(file));  // Removed when the console ends
        QVERIFY(!QFile::exists(file + ".sh"));
        // The password itself never on ipmitool's command line
        for (const QByteArray &line : log.split('\n')) {
            if (line.startsWith("ipmitool")) QVERIFY(!line.contains("p@ss"));
        }
    }

    void consoleOffersToTakeOverABusySession()
    {
        write("busy", "");
        QString file = makePasswordFile();
        QByteArray out = runConsole(file, "y\n");
        QVERIFY2(out.contains("close that one and try again"), out.constData());
        QByteArray log = read("log");
        QVERIFY(log.contains("sol deactivate"));
        QCOMPARE(log.count("sol activate"), 2);
        QVERIFY(!QFile::exists(file));
    }

    void consoleBusySessionLeftAloneOnNo()
    {
        write("busy", "");
        QString file = makePasswordFile();
        runConsole(file, "\n");
        QVERIFY(!read("log").contains("sol deactivate"));
        QVERIFY(!QFile::exists(file));
    }

    void stateLabels()
    {
        QCOMPARE(stateLabel("On"), QString("🟢 On"));
        QCOMPARE(stateLabel("Off"), QString("⚪ Off"));
        QCOMPARE(stateLabel("PoweringOn"), QString("🟡 PoweringOn"));
        QCOMPARE(stateLabel(""), QString("❔ Unknown"));
    }

    void confirmationOnlyForDisruptiveActions()
    {
        QVERIFY(!needsConfirmation(Status));
        QVERIFY(!needsConfirmation(On));
        QVERIFY(needsConfirmation(GracefulOff));
        QVERIFY(needsConfirmation(ForceOff));
        QVERIFY(needsConfirmation(Cycle));
        QVERIFY(needsConfirmation(Reset));
    }

    void savedCredentialsPerBmc()
    {
        QCoreApplication::setOrganizationName("tst_bmcpower");
        forgetCredentials("10.0.0.1");
        QVERIFY(!savedCredentials("10.0.0.1").isValid());
        saveCredentials("10.0.0.1", Credentials{"root", "secret"});
        QCOMPARE(savedCredentials("10.0.0.1").username, QString("root"));
        QCOMPARE(savedCredentials("10.0.0.1").password, QString("secret"));
        QVERIFY(!savedCredentials("10.0.0.2").isValid());
        forgetCredentials("10.0.0.1");
        QVERIFY(!savedCredentials("10.0.0.1").isValid());
    }

    void dontAskAgainPerBmc()
    {
        QCoreApplication::setOrganizationName("tst_bmcpower");
        setSkipsConfirmation("10.0.0.1", false);
        QVERIFY(!skipsConfirmation("10.0.0.1"));
        setSkipsConfirmation("10.0.0.1", true);
        QVERIFY(skipsConfirmation("10.0.0.1"));
        QVERIFY(!skipsConfirmation("10.0.0.2"));
        setSkipsConfirmation("10.0.0.1", false);
        QVERIFY(!skipsConfirmation("10.0.0.1"));
    }
};

QTEST_MAIN(TestBmcPower)
#include "tst_bmcpower.moc"
