#ifndef PROXYBROWSER_H
#define PROXYBROWSER_H

// Starts a separate Chrome-family browser window whose traffic goes through a
// SOCKS tunnel's local port, the way you would by hand with
//   google-chrome --user-data-dir=/tmp/x --proxy-server="socks5://127.0.0.1:1080"
// It gets its own profile folder (one per port), so it doesn't touch the
// user's normal browser and two proxies can be open side by side.

#include <QString>
#include <QStringList>

namespace ProxyBrowser {

struct Browser {
    QString program;     // Full path of the executable
    QString name;        // For messages: "Google Chrome", "Chromium", ...
    bool windows = false;  // A Windows .exe started from WSL
    bool snap = false;     // A snap, which can only write inside ~/snap/<name>
    bool isValid() const { return !program.isEmpty(); }
};

// The first browser found: Linux Chrome, Chromium, Edge, Brave, then (on WSL)
// Windows Chrome or Edge. Invalid if there is none.
Browser find();

// Command line arguments for a browser started on port with its own profile.
// url may be empty for a blank start page; a bare host or IP gets https://
QStringList arguments(const QString &profileDir, int port, const QString &url);

// "10.0.0.5" -> "https://10.0.0.5/"; full URLs are left alone; empty stays empty
QString normalizeUrl(const QString &text);

// Starts it. On failure returns false with a message for the user in *error.
// *started names the browser that was started.
bool launch(int port, const QString &url, QString *error, QString *started = nullptr);

// Start addresses offered for a port: the last one used first. remember() puts
// one at the front (the scan does this for the BMCs it routes through a proxy),
// with a name such as the BMC's host name when one is known. A whole rack fits.
const int kMaxRecentUrls = 64;
QStringList recentUrls(int port);
void remember(int port, const QString &url, const QString &name = QString());
QString nameFor(int port, const QString &url);

// Sorted the way a rack reads: by IPv4 address (10.0.0.9 before 10.0.0.10),
// host names after them alphabetically
QStringList sortedByAddress(QStringList urls);

// A menu label: "name · 10.0.0.5" for a plain https://<host>/ with a name,
// otherwise the host (or the full URL when it has a path or another scheme)
QString label(const QString &url, const QString &name);

} // namespace ProxyBrowser

#endif // PROXYBROWSER_H
