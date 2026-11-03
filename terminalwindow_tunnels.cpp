// SSH port forwarding: which tunnels a new SSH tab starts, and the status bar
// button and tab menu that show them with "Open in browser" / "Copy address"
#include "terminalwindow.h"
#include "inwindowdialog.h"

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QHostAddress>
#include <QMenu>
#include <QProcess>
#include <QStatusBar>
#include <QTcpServer>
#include <QToolButton>
#include <QUrl>

namespace {

// Whether this PC can open a listening socket on the port, the way ssh does
// for -L and -D (it binds the loopback address unless told otherwise)
bool localPortFree(int port)
{
    QTcpServer probe;
    if (!probe.listen(QHostAddress::LocalHost, static_cast<quint16>(port))) return false;
    probe.close();
    return true;
}

// WSL usually has no Linux browser for xdg-open to start, so hand the URL to
// Windows there; elsewhere use the desktop's default browser
void openInBrowser(const QString &url)
{
    if (!qEnvironmentVariableIsEmpty("WSL_DISTRO_NAME")
        && QProcess::startDetached("explorer.exe", {url}))
        return;
    QDesktopServices::openUrl(QUrl(url));
}

QString webUrl(const SSHTunnel &tunnel, bool https)
{
    return QString("%1://localhost:%2/").arg(https ? "https" : "http").arg(tunnel.listenPort);
}

} // namespace

// The tunnels a new SSH tab for this connection can start. A local port that
// is already taken (by another program, or by another tab of the same
// connection) is left out and described in *skipped, so the login still works.
QList<SSHTunnel> TerminalWindow::startableTunnels(const SSHConnection &connection, QStringList *skipped)
{
    QList<SSHTunnel> result;
    for (const SSHTunnel &tunnel : connection.tunnels) {
        if (!tunnel.enabled) continue;
        QString problem = tunnel.problem();
        if (!problem.isEmpty()) {
            if (skipped) *skipped << QString("%1: %2").arg(tunnel.describe(), problem);
            continue;
        }
        if (tunnel.listensLocally() && !localPortFree(tunnel.listenPort)) {
            if (skipped) {
                *skipped << QString("%1: local port %2 is %3")
                    .arg(tunnel.describe()).arg(tunnel.listenPort)
                    .arg(tunnel.listenPort < 1024
                         ? "below 1024, which needs admin rights, or is already in use"
                         : "already in use by another program or another SSH tab");
            }
            continue;
        }
        result << tunnel;
    }
    return result;
}

QList<SSHTunnel> TerminalWindow::activeTunnels(QTermWidget *terminal) const
{
    if (!terminal || !terminal->property("isSSHTerminal").toBool()) return QList<SSHTunnel>();
    return terminal->property("activeTunnels").value<QList<SSHTunnel>>();
}

void TerminalWindow::setupTunnelsButton()
{
    tunnelsButton = new QToolButton(this);
    tunnelsButton->setAutoRaise(true);
    tunnelsButton->setPopupMode(QToolButton::InstantPopup);
    tunnelsButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    QMenu *menu = new QMenu(tunnelsButton);
    connect(menu, &QMenu::aboutToShow, this, [this, menu]() {
        menu->clear();
        addTunnelActions(menu, getCurrentTerminal());
    });
    tunnelsButton->setMenu(menu);
    tunnelsButton->hide();
    statusBar()->addPermanentWidget(tunnelsButton);
}

// Shows the current tab's tunnels in the status bar, or hides the button
void TerminalWindow::updateTunnelsButton()
{
    if (!tunnelsButton) return;
    QList<SSHTunnel> tunnels = activeTunnels(getCurrentTerminal());
    if (tunnels.isEmpty()) {
        tunnelsButton->hide();
        return;
    }
    tunnelsButton->setText(tunnels.size() == 1 ? QString("🔀 1 tunnel")
                                               : QString("🔀 %1 tunnels").arg(tunnels.size()));
    QStringList lines;
    for (const SSHTunnel &t : tunnels) lines << t.describe();
    tunnelsButton->setToolTip("Tunnels of this SSH tab:\n" + lines.join("\n") +
                              "\n\nClick to open or copy their addresses.");
    tunnelsButton->show();
}

// One section per tunnel of the tab: its state, then what can be done with it
void TerminalWindow::addTunnelActions(QMenu *menu, QTermWidget *terminal)
{
    QList<SSHTunnel> tunnels = activeTunnels(terminal);
    if (tunnels.isEmpty()) {
        QAction *none = menu->addAction("No tunnels in this tab");
        none->setEnabled(false);
        return;
    }

    for (const SSHTunnel &tunnel : tunnels) {
        // Disabled menu rows are drawn grey, so the state is spelled out in words
        QString state;
        bool listening = false;
        if (tunnel.type == SSHTunnel::Remote) {
            state = "↩ On the server";  // This PC can't check a port opened over there
        } else {
            listening = !localPortFree(tunnel.listenPort);
            state = listening ? "🟢 Listening" : "⚪ Not listening";
        }
        QAction *title = menu->addAction(QString("%1 (%2) · %3").arg(state, tunnel.flag(), tunnel.describe()));
        title->setEnabled(false);
        if (tunnel.type != SSHTunnel::Remote && !listening) {
            title->setToolTip("Nothing is listening on this port: the login hasn't finished, "
                              "or ssh could not open the tunnel (see the terminal)");
        }

        if (tunnel.type == SSHTunnel::Local) {
            bool https = tunnel.looksLikeHttps();
            menu->addAction(QString("    🌐 Open %1").arg(webUrl(tunnel, https)), this, [tunnel, https]() {
                openInBrowser(webUrl(tunnel, https));
            });
            menu->addAction(QString("    🌐 Open %1").arg(webUrl(tunnel, !https)), this, [tunnel, https]() {
                openInBrowser(webUrl(tunnel, !https));
            });
            QString address = QString("localhost:%1").arg(tunnel.listenPort);
            menu->addAction(QString("    📋 Copy address %1").arg(address), this, [this, address]() {
                QApplication::clipboard()->setText(address);
                statusBar()->showMessage("Copied " + address, 3000);
            });
        } else if (tunnel.type == SSHTunnel::Dynamic) {
            QString proxy = QString("socks5://localhost:%1").arg(tunnel.listenPort);
            menu->addAction(QString("    📋 Copy proxy address %1").arg(proxy), this, [this, proxy]() {
                QApplication::clipboard()->setText(proxy);
                statusBar()->showMessage("Copied " + proxy, 3000);
            });
            int port = tunnel.listenPort;
            menu->addAction("    ❓ How to use this SOCKS proxy...", this, [this, port]() {
                InWindowDialog::information(this, "SOCKS Proxy",
                    QString("This tab runs a SOCKS5 proxy on localhost:%1. Traffic sent through it "
                            "leaves from the server, so a browser can open any address the server "
                            "can reach, such as each BMC by its own IP.\n\n"
                            "Firefox: Settings > Network Settings > Manual proxy configuration. "
                            "Set SOCKS Host to localhost, Port to %1, choose SOCKS v5 and tick "
                            "\"Proxy DNS when using SOCKS v5\".\n\n"
                            "Chrome or Edge: start a separate window with\n"
                            "  --proxy-server=\"socks5://localhost:%1\" --user-data-dir=<new folder>\n\n"
                            "Command line: curl --socks5-hostname localhost:%1 https://10.x.y.z/")
                        .arg(port));
            });
        } else {
            QString address = QString("localhost:%1").arg(tunnel.listenPort);
            menu->addAction(QString("    📋 Copy server-side address %1").arg(address), this, [this, address]() {
                QApplication::clipboard()->setText(address);
                statusBar()->showMessage("Copied " + address + " (use it on the server)", 3000);
            });
        }
        menu->addSeparator();
    }
    QAction *note = menu->addAction("Tunnels close when the SSH session ends");
    note->setEnabled(false);
}
