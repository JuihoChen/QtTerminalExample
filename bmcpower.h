#ifndef BMCPOWER_H
#define BMCPOWER_H

// Power status and power actions for a BMC, run on the host that can reach
// it (a BCM head node, through its saved SSH connection) since this PC
// usually can't. It asks the BMC's Redfish API with curl first:
//   GET  /redfish/v1/Systems/<id>                        -> PowerState
//   POST /redfish/v1/Systems/<id>/Actions/ComputerSystem.Reset {"ResetType": ...}
// and falls back to "ipmitool -I lanplus ... power <cmd>" when Redfish fails.
// The script goes to "sh -s" on stdin, so the BMC password never shows on a
// command line there; curl reads it from a config here-doc and ipmitool from
// IPMI_PASSWORD (-E).

#include <QByteArray>
#include <QString>
#include <functional>

#include "sshconnection.h"

class QMenu;
class QWidget;

namespace BmcPower {

enum Action { Status, On, GracefulOff, ForceOff, Cycle, Reset };

struct Credentials {
    QString username;
    QString password;
    bool isValid() const { return !username.isEmpty(); }
};

// Menu text, e.g. "Power cycle"
QString actionLabel(Action action);
// Off, Cycle and Reset ask "Really ...?" first
bool needsConfirmation(Action action);
// Redfish ResetType values to try, best first (the BMC's AllowableValues
// picks among them), and the ipmitool "power" subcommand
QStringList redfishResetTypes(Action action);
QString ipmitoolCommand(Action action);

// BMC logins, saved per BMC address apart from the SSH connections
Credentials savedCredentials(const QString &host);
void saveCredentials(const QString &host, const Credentials &credentials);
void forgetCredentials(const QString &host);

// "Don't ask again for this BMC" on the "Really ...?" question, per BMC
bool skipsConfirmation(const QString &host);
void setSkipsConfirmation(const QString &host, bool skip);

// The script for "sh -s" on the host that reaches the BMC
QByteArray script(const QString &host, Action action, const Credentials &credentials);

struct Result {
    bool ok = false;
    bool authFailed = false;     // The BMC refused the username or password
    QString method;              // "Redfish" or "IPMI"
    QString system;              // Redfish system path, e.g. /redfish/v1/Systems/System_0
    QString powerState;          // "On", "Off", ... as the BMC reports it (empty: unknown)
    QString resetType;           // The ResetType or ipmitool command sent
    QString error;               // What went wrong, for the user
};
Result parseOutput(const QByteArray &output, Action action);

// "🟢 On", "⚪ Off", "🟡 PoweringOn", "❔ Unknown"
QString stateLabel(const QString &powerState);

struct Hooks {
    std::function<void(const QString &)> status;      // Progress for a status bar
    std::function<void(const QString &)> powerState;  // The state once known
    // Set by the window: opens the BMC's serial console in a new tab. The
    // menu offers "Serial console" only when this is set.
    std::function<void()> openConsole;
};

// Serial console (IPMI Serial over LAN). consoleSetupScript(), run over
// stdin where ipmitool will run, writes the BMC password to a file only the
// user can read and the console script beside it (<file>.sh), then prints
// "FILE <file>". consoleCommand(<file>) runs it in a terminal: "ipmitool
// -I lanplus -C 17 ... -f <file> sol deactivate", then "sol activate" (taking the
// console over from another session), removing both files when it ends.
// Leave the console with ~. at the start of a line.
QString consoleScript(const QString &host, const QString &name, const QString &username);
QByteArray consoleSetupScript(const QString &password, const QString &script);
QString consoleCommand(const QString &passwordFile);
// Asks for the BMC login if none is saved, writes the files where the
// console runs, then calls ready with the command to run in a terminal there
void prepareConsole(QWidget *parent, const SSHConnection &via, bool throughVia, const QString &host,
                    const QString &name, std::function<void(const QString &command)> ready,
                    const Hooks &hooks = Hooks());

// Fills menu with Power status / on / off / cycle / reset for the BMC at
// host. With throughVia the commands run on via over SSH, otherwise on this
// computer. Dialogs and results open over parent.
void addActions(QMenu *menu, QWidget *parent, const SSHConnection &via, bool throughVia,
                const QString &host, const QString &name, const Hooks &hooks = Hooks());

// Starts one action (asks for confirmation and the BMC login as needed)
void run(QWidget *parent, const SSHConnection &via, bool throughVia, const QString &host,
         const QString &name, Action action, const Hooks &hooks = Hooks());

} // namespace BmcPower

#endif // BMCPOWER_H
