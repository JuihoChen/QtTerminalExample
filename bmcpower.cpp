// BMC power status and actions, run where the BMC can be reached (see bmcpower.h)
#include "bmcpower.h"
#include "inwindowdialog.h"
#include "sftpworker.h"

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPointer>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QThread>
#include <QVBoxLayout>

namespace BmcPower {

namespace {

const char *kSettingsGroup = "bmcCredentials";
const char *kSkipConfirmGroup = "bmcSkipConfirmation";

// Single-quoted for sh, whatever it holds
QString shellQuote(const QString &text)
{
    QString escaped = text;
    escaped.replace('\'', "'\\''");
    return '\'' + escaped + '\'';
}

// Inside double quotes of a curl config file
QString curlConfigQuote(const QString &text)
{
    QString escaped = text;
    escaped.replace('\\', "\\\\").replace('"', "\\\"");
    return '"' + escaped + '"';
}

QString settingsKey(const QString &host)
{
    // QSettings treats '/' and '\' in keys as groups
    QString key = host.trimmed().toLower();
    key.replace('/', '_').replace('\\', '_');
    return key;
}

QString describeBmc(const QString &host, const QString &name)
{
    return name.isEmpty() || name == host ? host : QString("%1 (%2)").arg(name, host);
}

// The BMCs with an action running, so a second click doesn't send another
QSet<QString> &busyHosts()
{
    static QSet<QString> hosts;
    return hosts;
}

// A message out of a Redfish error reply, if it has one
QString redfishErrorMessage(const QByteArray &body)
{
    QJsonObject root = QJsonDocument::fromJson(body).object();
    QJsonObject error = root.value("error").toObject();
    QJsonArray info = error.value("@Message.ExtendedInfo").toArray();
    if (info.isEmpty()) info = root.value("@Message.ExtendedInfo").toArray();
    for (const QJsonValue &item : info) {
        QString message = item.toObject().value("Message").toString().trimmed();
        if (!message.isEmpty()) return message;
    }
    return error.value("message").toString().trimmed();
}

QString httpProblem(const QString &code)
{
    if (code == "000" || code.isEmpty()) return QStringLiteral("no answer on https (port 443)");
    if (code == "401" || code == "403") return QStringLiteral("it refused the username or password");
    return QString("it answered HTTP %1").arg(code);
}

bool ipmiAuthFailure(const QString &text)
{
    return text.contains("RAKP", Qt::CaseInsensitive)
        || text.contains("invalid user name", Qt::CaseInsensitive)
        || text.contains("unauthorized name", Qt::CaseInsensitive);
}

// After a refused login: asks again, with the username filled in, and
// starts the action without confirming it a second time
void retryWithNewLogin(QWidget *parent, const SSHConnection &via, bool throughVia, const QString &host,
                       const QString &name, Action action, const QString &lastUser, const Hooks &hooks);

// Runs one script where the BMC can be reached and reports the parsed result
class Job : public QObject
{
public:
    Job(QWidget *parent, const SSHConnection &via, bool throughVia, const QString &host,
        const QString &name, Action action, const Credentials &credentials, const Hooks &hooks)
        : QObject(parent), m_parent(parent), m_via(via), m_throughVia(throughVia), m_host(host),
          m_name(name), m_action(action), m_credentials(credentials), m_hooks(hooks) {}

    ~Job() override
    {
        busyHosts().remove(m_host);
        if (m_worker) SftpWorker::retire(m_thread, m_worker);
        if (m_process) {
            m_process->disconnect();
            m_process->kill();
        }
    }

    void start()
    {
        busyHosts().insert(m_host);
        QByteArray input = script(m_host, m_action, m_credentials);
        QString where = m_throughVia ? m_via.name : QStringLiteral("this computer");
        status(QString("%1: %2 through %3...").arg(describeBmc(m_host, m_name), actionLabel(m_action), where));

        if (!m_throughVia) {
            m_process = new QProcess(this);
            connect(m_process, &QProcess::readyReadStandardOutput, this, [this]() {
                m_output += m_process->readAllStandardOutput();
            });
            connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
                    [this](int, QProcess::ExitStatus) {
                m_output += m_process->readAllStandardOutput();
                QString err = QString::fromUtf8(m_process->readAllStandardError()).trimmed();
                finish(err);
            });
            connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
                if (e == QProcess::FailedToStart) finish("Could not run /bin/sh on this computer.", true);
            });
            m_process->start("/bin/sh", QStringList() << "-s");
            m_process->write(input);
            m_process->closeWriteChannel();
            return;
        }

        m_thread = new QThread(this);
        m_worker = new SftpWorker(m_via);
        m_worker->moveToThread(m_thread);
        connect(m_worker, &SftpWorker::commandOutput, this, [this](const QByteArray &data) { m_output += data; });
        connect(m_worker, &SftpWorker::commandErrorOutput, this, [this](const QByteArray &data) {
            m_stderr = (m_stderr + data).right(2048);
        });
        connect(m_worker, &SftpWorker::step, this, [this](const QString &step) {
            if (step == "Running command") m_started = true;
        });
        connect(m_worker, &SftpWorker::commandFinished, this, [this](int, const QString &error) {
            SftpWorker::retire(m_thread, m_worker);
            m_worker = nullptr;
            m_thread = nullptr;
            if (!error.isEmpty() && !m_started) {
                finish(QString("Could not log in to %1 (%2) to reach the BMC:\n%3")
                           .arg(m_via.name, m_via.host, error), true);
            } else {
                finish(error.isEmpty() ? QString::fromUtf8(m_stderr).trimmed() : error, !error.isEmpty());
            }
        });
        m_thread->start();
        QMetaObject::invokeMethod(m_worker, "runCommandWithInput", Qt::QueuedConnection,
                                  Q_ARG(QString, QStringLiteral("sh -s")), Q_ARG(QByteArray, input));
    }

private:
    void status(const QString &text) { if (m_hooks.status) m_hooks.status(text); }

    void finish(const QString &problem, bool failedToRun = false)
    {
        if (m_finished) return;
        m_finished = true;
        busyHosts().remove(m_host);
        QString bmc = describeBmc(m_host, m_name);
        QString where = m_throughVia ? QString("%1 (%2)").arg(m_via.name, m_via.host)
                                     : QStringLiteral("this computer");

        Result result;
        if (failedToRun) {
            result.error = problem;
        } else {
            result = parseOutput(m_output, m_action);
            if (!result.ok && result.error.isEmpty()) {
                result.error = problem.isEmpty() ? QStringLiteral("The script ended without an answer.")
                                                 : problem;
            }
        }
        if (!result.powerState.isEmpty() && m_hooks.powerState) m_hooks.powerState(result.powerState);

        QString title = QString("BMC %1").arg(actionLabel(m_action));
        if (result.ok) {
            QString state = result.powerState.isEmpty() ? QString() : stateLabel(result.powerState);
            QString text;
            if (m_action == Status) {
                text = QString("%1 is %2.").arg(bmc, state);
                status(QString("%1 is %2").arg(bmc, state));
            } else {
                text = QString("%1 accepted %2 (%3).").arg(bmc, actionLabel(m_action).toLower(), result.resetType);
                if (!state.isEmpty()) text += QString("\n\nPower state a few seconds later: %1.").arg(state);
                status(QString("%1: %2 sent").arg(bmc, actionLabel(m_action)));
            }
            text += QString("\n\nThrough %1 on %2").arg(result.method, where);
            if (!result.system.isEmpty()) text += QString(", system %1").arg(result.system);
            text += ".";
            if (m_parent) InWindowDialog::information(m_parent, title, text);
        } else {
            status(QString("%1: %2 failed").arg(bmc, actionLabel(m_action)));
            QString text = QString("%1 for %2 didn't work.\n\n%3").arg(actionLabel(m_action), bmc, result.error);
            if (result.authFailed) {
                forgetCredentials(m_host);
                text += "\n\nThe saved BMC login was removed. Try again with another username or password?";
                if (m_parent && InWindowDialog::question(m_parent, title, text) == QMessageBox::Yes) {
                    QPointer<QWidget> parent = m_parent;
                    SSHConnection via = m_via;
                    bool throughVia = m_throughVia;
                    QString host = m_host, name = m_name;
                    Action action = m_action;
                    Hooks hooks = m_hooks;
                    QString user = m_credentials.username;
                    deleteLater();
                    if (parent) retryWithNewLogin(parent, via, throughVia, host, name, action, user, hooks);
                    return;
                }
            } else if (m_parent) {
                InWindowDialog::warning(m_parent, title, text);
            }
        }
        deleteLater();
    }

    QPointer<QWidget> m_parent;
    SSHConnection m_via;
    bool m_throughVia;
    QString m_host;
    QString m_name;
    Action m_action;
    Credentials m_credentials;
    Hooks m_hooks;
    QThread *m_thread = nullptr;
    SftpWorker *m_worker = nullptr;
    QProcess *m_process = nullptr;
    QByteArray m_output;
    QByteArray m_stderr;
    bool m_started = false;
    bool m_finished = false;
};

// Asks for the BMC's username and password; false on Cancel
bool askCredentials(QWidget *parent, const QString &host, const QString &name, Credentials *credentials,
                    const QString &lastUser = QString())
{
    Credentials saved = savedCredentials(host);
    if (!saved.isValid()) saved.username = lastUser;
    QDialog dialog(parent);
    dialog.setWindowTitle("BMC Login");
    dialog.setMinimumWidth(420);
    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    QLabel *intro = new QLabel(QString("Log in to the BMC <b>%1</b>. This is the BMC's own account "
                                       "(Redfish / IPMI), not an SSH login.")
                                   .arg(describeBmc(host, name).toHtmlEscaped()), &dialog);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    QFormLayout *form = new QFormLayout();
    QLineEdit *user = new QLineEdit(saved.username, &dialog);
    user->setPlaceholderText("e.g. admin");
    QLineEdit *password = new QLineEdit(&dialog);
    password->setEchoMode(QLineEdit::Password);
    form->addRow("Username:", user);
    form->addRow("Password:", password);
    layout->addLayout(form);

    QCheckBox *remember = new QCheckBox("Remember for this BMC", &dialog);
    remember->setChecked(true);
    remember->setToolTip("Saved in this app's settings for this BMC address only, in plain text "
                         "like saved SSH passwords. Forget it from the same menu.");
    layout->addWidget(remember);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QPushButton *ok = buttons->button(QDialogButtonBox::Ok);
    ok->setText("Continue");
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    auto update = [=]() { ok->setEnabled(!user->text().trimmed().isEmpty()); };
    QObject::connect(user, &QLineEdit::textChanged, &dialog, update);
    update();
    (user->text().isEmpty() ? user : password)->setFocus();

    if (InWindowDialog::exec(&dialog) != QDialog::Accepted) return false;
    credentials->username = user->text().trimmed();
    credentials->password = password->text();
    if (remember->isChecked()) saveCredentials(host, *credentials);
    return true;
}

void retryWithNewLogin(QWidget *parent, const SSHConnection &via, bool throughVia, const QString &host,
                       const QString &name, Action action, const QString &lastUser, const Hooks &hooks)
{
    Credentials credentials;
    if (!askCredentials(parent, host, name, &credentials, lastUser)) return;
    Job *job = new Job(parent, via, throughVia, host, name, action, credentials, hooks);
    job->start();
}

QString confirmText(Action action, const QString &bmc)
{
    switch (action) {
    case GracefulOff:
        return QString("Really shut down %1?\n\nThe BMC asks the operating system to shut down "
                       "(like pressing the power button).").arg(bmc);
    case ForceOff:
        return QString("Really power off %1?\n\nThe node loses power at once, without shutting down "
                       "its operating system.").arg(bmc);
    case Cycle:
        return QString("Really power cycle %1?\n\nThe node is turned off and on again at once, without "
                       "shutting down its operating system.").arg(bmc);
    case Reset:
        return QString("Really reset %1?\n\nThe node restarts at once, without shutting down its "
                       "operating system.").arg(bmc);
    default:
        return QString();
    }
}

// "Really ...?" with No as the default and a "Don't ask again" box
bool confirm(QWidget *parent, Action action, const QString &host, const QString &bmc)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(actionLabel(action));
    dialog.setMinimumWidth(420);
    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    QLabel *text = new QLabel(confirmText(action, bmc), &dialog);
    text->setWordWrap(true);
    layout->addWidget(text);
    QCheckBox *dontAsk = new QCheckBox("Don't ask again for this BMC", &dialog);
    dontAsk->setToolTip("Shut down, power off, power cycle and reset then start at once for this BMC. "
                        "Turn the question back on from its BMC power menu.");
    layout->addWidget(dontAsk);
    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Yes | QDialogButtonBox::No, &dialog);
    QPushButton *no = buttons->button(QDialogButtonBox::No);
    no->setDefault(true);
    no->setFocus();
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    if (InWindowDialog::exec(&dialog) != QDialog::Accepted) return false;
    if (dontAsk->isChecked()) setSkipsConfirmation(host, true);
    return true;
}

} // namespace

bool skipsConfirmation(const QString &host)
{
    QSettings settings;
    settings.beginGroup(kSkipConfirmGroup);
    return settings.value(settingsKey(host), false).toBool();
}

void setSkipsConfirmation(const QString &host, bool skip)
{
    QSettings settings;
    settings.beginGroup(kSkipConfirmGroup);
    if (skip) settings.setValue(settingsKey(host), true);
    else settings.remove(settingsKey(host));
}

QString actionLabel(Action action)
{
    switch (action) {
    case Status: return QStringLiteral("Power status");
    case On: return QStringLiteral("Power on");
    case GracefulOff: return QStringLiteral("Shut down");
    case ForceOff: return QStringLiteral("Power off");
    case Cycle: return QStringLiteral("Power cycle");
    case Reset: return QStringLiteral("Reset");
    }
    return QString();
}

bool needsConfirmation(Action action)
{
    return action == GracefulOff || action == ForceOff || action == Cycle || action == Reset;
}

QStringList redfishResetTypes(Action action)
{
    switch (action) {
    case On: return {"On", "ForceOn"};
    case GracefulOff: return {"GracefulShutdown"};
    case ForceOff: return {"ForceOff"};
    case Cycle: return {"PowerCycle"};
    case Reset: return {"ForceRestart"};
    case Status: break;
    }
    return QStringList();
}

QString ipmitoolCommand(Action action)
{
    switch (action) {
    case Status: return QStringLiteral("status");
    case On: return QStringLiteral("on");
    case GracefulOff: return QStringLiteral("soft");
    case ForceOff: return QStringLiteral("off");
    case Cycle: return QStringLiteral("cycle");
    case Reset: return QStringLiteral("reset");
    }
    return QString();
}

Credentials savedCredentials(const QString &host)
{
    QSettings settings;
    settings.beginGroup(kSettingsGroup);
    settings.beginGroup(settingsKey(host));
    Credentials credentials;
    credentials.username = settings.value("username").toString();
    credentials.password = settings.value("password").toString();
    return credentials;
}

void saveCredentials(const QString &host, const Credentials &credentials)
{
    QSettings settings;
    settings.beginGroup(kSettingsGroup);
    settings.beginGroup(settingsKey(host));
    settings.setValue("username", credentials.username);
    settings.setValue("password", credentials.password);
}

void forgetCredentials(const QString &host)
{
    QSettings settings;
    settings.beginGroup(kSettingsGroup);
    settings.remove(settingsKey(host));
}

QByteArray script(const QString &host, Action action, const Credentials &credentials)
{
    // A line break would end the curl config line or the here-doc early
    QString user = credentials.username;
    QString password = credentials.password;
    user.remove('\n').remove('\r');
    password.remove('\n').remove('\r');

    QString s;
    s += "H=" + shellQuote(host.trimmed()) + "\n";
    s += "U=" + shellQuote(user) + "\n";
    s += "A=" + shellQuote(action == Status ? QStringLiteral("status") : QStringLiteral("action")) + "\n";
    s += "RTS=" + shellQuote(redfishResetTypes(action).join(' ')) + "\n";
    s += "IA=" + shellQuote(ipmitoolCommand(action)) + "\n";
    s += R"SH(T=$(mktemp 2>/dev/null) || T=/tmp/bmcpower.$$
trap 'rm -f "$T"' EXIT
one() { tr '\r\n\t' '   '; }
rf() {
  m=$1; p=$2
  if [ $# -ge 3 ]; then
    set -- -X "$m" -H 'Content-Type: application/json' --data "$3" "https://$H$p"
  else
    set -- -X "$m" "https://$H$p"
  fi
  curl -sk --noproxy '*' --connect-timeout 8 --max-time 40 -H 'Accept: application/json' \
    -o "$T" -w '%{http_code}' -K - "$@" <<'CFG'
)SH";
    s += "user = " + curlConfigQuote(user + ':' + password) + "\n";
    s += R"SH(CFG
}
done=0
nofb=0
if command -v curl >/dev/null 2>&1; then
  c=$(rf GET /redfish/v1/Systems)
  echo "RF_SYSTEMS $c $(head -c 600 "$T" | one)"
  if [ "$c" = 200 ]; then
    ms=$(one < "$T" | grep -o '"@odata.id" *: *"/redfish/v1/Systems/[^"]*"' | sed 's/.*"\(\/redfish[^"]*\)"$/\1/' | head -8)
    sys=''
    for s in $ms; do
      c=$(rf GET "$s")
      if [ "$c" = 200 ] && grep -q '"PowerState"' "$T"; then sys=$s; break; fi
    done
    if [ -z "$sys" ]; then
      echo "RF_NOSYSTEM"
    else
      j=$(one < "$T")
      echo "RF_SYSTEM $sys $j"
      if [ "$A" = status ]; then
        done=1
      else
        tg=$(printf '%s' "$j" | grep -o '"target" *: *"[^"]*ComputerSystem\.Reset[^"]*"' | head -1 | sed 's/.*"\([^"]*\)"$/\1/')
        [ -n "$tg" ] || tg="$sys/Actions/ComputerSystem.Reset"
        al=$(printf '%s' "$j" | grep -o '"ResetType@Redfish.AllowableValues" *: *\[[^]]*\]' | head -1)
        rt=''
        for r in $RTS; do
          if [ -z "$al" ] || printf '%s' "$al" | grep -q "\"$r\""; then rt=$r; break; fi
        done
        if [ -z "$rt" ]; then
          echo "RF_NOTALLOWED $al"
        else
          c=$(rf POST "$tg" "{\"ResetType\":\"$rt\"}")
          echo "RF_POST $c $rt $(head -c 1500 "$T" | one)"
          case "$c" in
            200|202|204)
              done=1
              sleep 6
              c=$(rf GET "$sys")
              [ "$c" = 200 ] && echo "RF_AFTER $(one < "$T")" ;;
            000) nofb=1 ;;
          esac
        fi
      fi
    fi
  fi
else
  echo NOCURL
fi
if [ "$done" != 1 ] && [ "$nofb" != 1 ]; then
  if command -v ipmitool >/dev/null 2>&1; then
)SH";
    s += "    IPMI_PASSWORD=" + shellQuote(password) + "; export IPMI_PASSWORD\n";
    s += R"SH(    if [ "$A" != status ]; then
      o=$(ipmitool -I lanplus -H "$H" -U "$U" -E -N 5 -R 2 chassis power "$IA" 2>&1); e=$?
      echo "IPMI_ACTION $e $IA $(printf '%s' "$o" | one)"
      [ "$e" = 0 ] && sleep 6
    fi
    o=$(ipmitool -I lanplus -H "$H" -U "$U" -E -N 5 -R 2 chassis power status 2>&1); e=$?
    echo "IPMI_STATUS $e $(printf '%s' "$o" | one)"
  else
    echo NOIPMITOOL
  fi
fi
echo END
)SH";
    return s.toUtf8();
}

Result parseOutput(const QByteArray &output, Action action)
{
    Result result;
    QStringList problems;
    bool sawEnd = false;
    bool redfishAuth = false, ipmiAuth = false;
    bool ipmiRan = false, noIpmitool = false;
    QString ipmiActionError;
    bool ipmiActionOk = false;
    bool redfishActionOk = false;
    QString redfishState, ipmiState;

    for (const QByteArray &raw : output.split('\n')) {
        QString line = QString::fromUtf8(raw).trimmed();
        if (line.isEmpty()) continue;
        QString tag = line.section(' ', 0, 0);
        if (tag == "END") {
            sawEnd = true;
        } else if (tag == "NOCURL") {
            problems << "Redfish: curl is not installed there.";
        } else if (tag == "NOIPMITOOL") {
            noIpmitool = true;
        } else if (tag == "RF_SYSTEMS") {
            QString code = line.section(' ', 1, 1);
            if (code != "200") {
                if (code == "401" || code == "403") redfishAuth = true;
                QString message = redfishErrorMessage(line.section(' ', 2).toUtf8());
                problems << QString("Redfish: %1%2.").arg(httpProblem(code),
                                                            message.isEmpty() ? QString() : " (" + message + ")");
            }
        } else if (tag == "RF_NOSYSTEM") {
            problems << "Redfish: the BMC lists no system with a power state.";
        } else if (tag == "RF_SYSTEM" || tag == "RF_AFTER") {
            int jsonStart = tag == "RF_SYSTEM" ? 2 : 1;
            if (tag == "RF_SYSTEM") result.system = line.section(' ', 1, 1);
            QJsonObject system = QJsonDocument::fromJson(line.section(' ', jsonStart).toUtf8()).object();
            QString state = system.value("PowerState").toString();
            if (!state.isEmpty()) redfishState = state;
        } else if (tag == "RF_NOTALLOWED") {
            problems << QString("Redfish: the BMC doesn't offer %1 (it allows %2).")
                            .arg(redfishResetTypes(action).join(" or "),
                                 line.section(' ', 1).section('[', 1).section(']', 0, 0).remove('"').simplified());
        } else if (tag == "RF_POST") {
            QString code = line.section(' ', 1, 1);
            QString resetType = line.section(' ', 2, 2);
            if (code == "200" || code == "202" || code == "204") {
                redfishActionOk = true;
                result.resetType = resetType;
            } else if (code == "000") {
                problems << QString("Redfish: sent %1, but the BMC didn't answer in time. It may still "
                                    "have done it: check the power status before trying again.").arg(resetType);
            } else {
                if (code == "401" || code == "403") redfishAuth = true;
                QString message = redfishErrorMessage(line.section(' ', 3).toUtf8());
                problems << QString("Redfish: %1 was refused, %2%3.").arg(resetType, httpProblem(code),
                                                                         message.isEmpty() ? QString() : ": " + message);
            }
        } else if (tag == "IPMI_ACTION") {
            ipmiRan = true;
            QString command = line.section(' ', 2, 2);
            QString text = line.section(' ', 3).simplified();
            if (line.section(' ', 1, 1) == "0") {
                ipmiActionOk = true;
                result.resetType = "ipmitool power " + command;
            } else {
                ipmiActionError = text;
                if (ipmiAuthFailure(text)) ipmiAuth = true;
            }
        } else if (tag == "IPMI_STATUS") {
            ipmiRan = true;
            QString text = line.section(' ', 2).simplified();
            if (line.section(' ', 1, 1) == "0") {
                QRegularExpressionMatch m = QRegularExpression("Power is (\\w+)", QRegularExpression::CaseInsensitiveOption).match(text);
                if (m.hasMatch()) {
                    QString word = m.captured(1).toLower();
                    ipmiState = word == "on" ? QStringLiteral("On") : word == "off" ? QStringLiteral("Off") : m.captured(1);
                }
            } else if (ipmiActionError.isEmpty() || action == Status) {
                if (ipmiAuthFailure(text)) ipmiAuth = true;
                if (action == Status) ipmiActionError = text;
            }
        }
    }

    if (action == Status) {
        if (!redfishState.isEmpty()) {
            result.ok = true;
            result.method = "Redfish";
            result.powerState = redfishState;
        } else if (!ipmiState.isEmpty()) {
            result.ok = true;
            result.method = "IPMI (ipmitool)";
            result.powerState = ipmiState;
            result.system.clear();
        }
    } else if (redfishActionOk) {
        result.ok = true;
        result.method = "Redfish";
        result.powerState = redfishState;
    } else if (ipmiActionOk) {
        result.ok = true;
        result.method = "IPMI (ipmitool)";
        result.powerState = ipmiState;
        result.system.clear();
    }

    if (!result.ok) {
        if (ipmiRan && !ipmiActionError.isEmpty()) {
            problems << "IPMI: " + ipmiActionError;
        } else if (noIpmitool) {
            problems << "IPMI: ipmitool is not installed there, so there was nothing to fall back to.";
        }
        if (!sawEnd && problems.isEmpty()) problems << "The script was cut off before it finished.";
        result.authFailed = redfishAuth || ipmiAuth;
        result.error = problems.join("\n");
    }
    return result;
}

QString stateLabel(const QString &powerState)
{
    QString s = powerState.trimmed();
    if (s.compare("On", Qt::CaseInsensitive) == 0) return QStringLiteral("🟢 On");
    if (s.compare("Off", Qt::CaseInsensitive) == 0) return QStringLiteral("⚪ Off");
    if (s.startsWith("Powering", Qt::CaseInsensitive) || s == "Paused") return "🟡 " + s;
    if (s.isEmpty()) return QStringLiteral("❔ Unknown");
    return "❔ " + s;
}

void run(QWidget *parent, const SSHConnection &via, bool throughVia, const QString &host,
         const QString &name, Action action, const Hooks &hooks)
{
    QString bmc = describeBmc(host, name);
    if (busyHosts().contains(host)) {
        InWindowDialog::information(parent, "BMC Power",
                                    QString("%1 is still busy with the last power command.").arg(bmc));
        return;
    }
    if (needsConfirmation(action) && !skipsConfirmation(host) && !confirm(parent, action, host, bmc)) return;
    Credentials credentials = savedCredentials(host);
    if (!credentials.isValid() && !askCredentials(parent, host, name, &credentials)) return;

    Job *job = new Job(parent, via, throughVia, host, name, action, credentials, hooks);
    job->start();
}

void addActions(QMenu *menu, QWidget *parent, const SSHConnection &via, bool throughVia,
                const QString &host, const QString &name, const Hooks &hooks)
{
    QPointer<QWidget> guard = parent;
    auto add = [&](const QString &text, Action action) {
        QAction *a = menu->addAction(text, parent, [=]() {
            if (guard) run(guard, via, throughVia, host, name, action, hooks);
        });
        a->setEnabled(!busyHosts().contains(host));
        return a;
    };
    add("🔎 Power status", Status);
    menu->addSeparator();
    add("▶ Power on", On);
    // "..." means a question comes first; without it the action starts at once
    const QString more = skipsConfirmation(host) ? QString() : QStringLiteral("...");
    add("⏻ Shut down (graceful)" + more, GracefulOff);
    add("⏹ Power off (force)" + more, ForceOff);
    add("🔁 Power cycle" + more, Cycle);
    add("↻ Reset" + more, Reset);
    menu->addSeparator();
    QString where = throughVia ? QString("Runs on %1 (Redfish, then ipmitool)").arg(via.name)
                               : QStringLiteral("Runs on this computer (Redfish, then ipmitool)");
    QAction *note = menu->addAction(where);
    note->setEnabled(false);
    if (savedCredentials(host).isValid()) {
        QString user = savedCredentials(host).username;
        menu->addAction(QString("Forget saved BMC login (%1)").arg(user), parent, [host]() {
            forgetCredentials(host);
        });
    }
    if (skipsConfirmation(host)) {
        menu->addAction("Ask before shut down, power off, cycle and reset again", parent, [host]() {
            setSkipsConfirmation(host, false);
        });
    }
}

} // namespace BmcPower
