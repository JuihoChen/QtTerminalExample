#include "proxybrowser.h"

#include <QDir>
#include <QHostAddress>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>
#include <QVariantMap>

#include <algorithm>

namespace ProxyBrowser {

namespace {

bool onWsl()
{
    return !qEnvironmentVariableIsEmpty("WSL_DISTRO_NAME");
}

// Ubuntu's chromium-browser is a script that runs the snap, so the path alone
// doesn't always say so
bool isSnap(const QString &program)
{
    QString real = QFileInfo(program).canonicalFilePath();
    if (program.startsWith("/snap/") || real.startsWith("/snap/")) return true;
    return QFileInfo(program).fileName() == "chromium-browser" && QFileInfo::exists("/snap/bin/chromium");
}

// Windows' %LOCALAPPDATA%, asked from cmd.exe, for the profile of a Windows
// browser started from WSL (it can't keep a profile on a \\wsl$ path)
QString windowsLocalAppData()
{
    QProcess cmd;
    cmd.setWorkingDirectory("/mnt/c");  // cmd.exe warns about a UNC start folder
    cmd.start("cmd.exe", {"/c", "echo %LOCALAPPDATA%"});
    if (!cmd.waitForFinished(5000)) {
        cmd.kill();
        return QString();
    }
    QString out = QString::fromLocal8Bit(cmd.readAllStandardOutput()).trimmed();
    return out.contains('%') ? QString() : out;
}

QString settingsKey(int port)
{
    return QString("socksBrowser/%1/recentUrls").arg(port);
}

QString namesKey(int port)
{
    return QString("socksBrowser/%1/names").arg(port);
}

// https://10.0.0.5/ and nothing more: shown as just its host
bool isPlainHttps(const QUrl &url)
{
    return url.scheme() == "https" && (url.path().isEmpty() || url.path() == "/")
        && url.port() == -1 && !url.hasQuery() && !url.hasFragment();
}

} // namespace

Browser find()
{
    struct Candidate { const char *program; const char *name; };
    static const Candidate linuxBrowsers[] = {
        {"google-chrome", "Google Chrome"},
        {"google-chrome-stable", "Google Chrome"},
        {"chromium", "Chromium"},
        {"chromium-browser", "Chromium"},
        {"microsoft-edge", "Microsoft Edge"},
        {"microsoft-edge-stable", "Microsoft Edge"},
        {"brave-browser", "Brave"},
    };
    for (const Candidate &c : linuxBrowsers) {
        QString path = QStandardPaths::findExecutable(c.program);
        if (path.isEmpty()) continue;
        Browser b;
        b.program = path;
        b.name = c.name;
        b.snap = isSnap(path);
        return b;
    }

    if (onWsl()) {
        static const Candidate windows[] = {
            {"/mnt/c/Program Files/Google/Chrome/Application/chrome.exe", "Google Chrome (Windows)"},
            {"/mnt/c/Program Files (x86)/Google/Chrome/Application/chrome.exe", "Google Chrome (Windows)"},
            {"/mnt/c/Program Files (x86)/Microsoft/Edge/Application/msedge.exe", "Microsoft Edge (Windows)"},
            {"/mnt/c/Program Files/Microsoft/Edge/Application/msedge.exe", "Microsoft Edge (Windows)"},
        };
        for (const Candidate &c : windows) {
            if (!QFileInfo(c.program).isExecutable()) continue;
            Browser b;
            b.program = c.program;
            b.name = c.name;
            b.windows = true;
            return b;
        }
    }
    return Browser();
}

QString normalizeUrl(const QString &text)
{
    QString url = text.trimmed();
    if (url.isEmpty()) return url;
    if (!url.contains("://")) {
        // BMCs serve their web page over https, so that's the default
        url = "https://" + url;
        if (QUrl(url).path().isEmpty()) url += "/";
    }
    return url;
}

QStringList arguments(const QString &profileDir, int port, const QString &url)
{
    QStringList args;
    args << "--user-data-dir=" + profileDir
         << QString("--proxy-server=socks5://127.0.0.1:%1").arg(port)
         << "--no-first-run"
         << "--no-default-browser-check"
         << "--new-window";
    QString start = normalizeUrl(url);
    if (!start.isEmpty()) args << start;
    return args;
}

bool launch(int port, const QString &url, QString *error, QString *started)
{
    Browser browser = find();
    if (!browser.isValid()) {
        if (error) {
            *error = QString(
                "No Chrome-family browser was found to start, so it can't be pointed at the proxy.\n\n"
                "Install one, for example:\n"
                "  sudo apt install chromium   (or Google Chrome from google.com/chrome)\n\n"
                "Firefox works too, set up by hand: Settings > Network Settings > Manual proxy "
                "configuration, SOCKS Host 127.0.0.1, Port %1, SOCKS v5, and tick "
                "\"Proxy DNS when using SOCKS v5\".").arg(port);
        }
        return false;
    }

    QString profileDir;
    if (browser.windows) {
        QString appData = windowsLocalAppData();
        if (appData.isEmpty()) {
            if (error) *error = QString("Found %1, but couldn't ask Windows where to keep its "
                                        "profile (cmd.exe gave no %LOCALAPPDATA%).").arg(browser.name);
            return false;
        }
        profileDir = QString("%1\\QtTerminal\\socks-browser-%2").arg(appData).arg(port);
    } else {
        // A snap may only write inside its own folder under ~/snap
        QString base = browser.snap
            ? QDir::homePath() + "/snap/" + QFileInfo(browser.program).fileName().remove("-browser") + "/common/qtterminal"
            : QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
        profileDir = QString("%1/socks-browser-%2").arg(base).arg(port);
        if (!QDir().mkpath(profileDir)) {
            if (error) *error = QString("Couldn't create the browser profile folder %1").arg(profileDir);
            return false;
        }
    }

    QProcess process;
    process.setProgram(browser.program);
    process.setArguments(arguments(profileDir, port, url));
    // The browser outlives this app, and its log lines shouldn't land in our terminal
    process.setStandardOutputFile(QProcess::nullDevice());
    process.setStandardErrorFile(QProcess::nullDevice());
    if (browser.windows) process.setWorkingDirectory("/mnt/c");
    if (!process.startDetached()) {
        if (error) *error = QString("Couldn't start %1 (%2): %3")
                                .arg(browser.name, browser.program, process.errorString());
        return false;
    }
    if (started) *started = browser.name;
    return true;
}

QStringList recentUrls(int port)
{
    return QSettings().value(settingsKey(port)).toStringList();
}

void remember(int port, const QString &url, const QString &name)
{
    QString normalized = normalizeUrl(url);
    if (normalized.isEmpty()) return;
    QSettings settings;
    QStringList urls = recentUrls(port);
    urls.removeAll(normalized);
    urls.prepend(normalized);
    while (urls.size() > kMaxRecentUrls) urls.removeLast();
    settings.setValue(settingsKey(port), urls);

    // Names only for the addresses still listed, so the map can't grow forever
    QVariantMap names = settings.value(namesKey(port)).toMap();
    if (!name.trimmed().isEmpty()) names.insert(normalized, name.trimmed());
    for (const QString &key : names.keys()) {
        if (!urls.contains(key)) names.remove(key);
    }
    settings.setValue(namesKey(port), names);
}

QString nameFor(int port, const QString &url)
{
    return QSettings().value(namesKey(port)).toMap().value(normalizeUrl(url)).toString();
}

QStringList sortedByAddress(QStringList urls)
{
    std::stable_sort(urls.begin(), urls.end(), [](const QString &a, const QString &b) {
        QString hostA = QUrl(a).host(), hostB = QUrl(b).host();
        QHostAddress ipA, ipB;
        bool isIpA = ipA.setAddress(hostA) && ipA.protocol() == QAbstractSocket::IPv4Protocol;
        bool isIpB = ipB.setAddress(hostB) && ipB.protocol() == QAbstractSocket::IPv4Protocol;
        if (isIpA != isIpB) return isIpA;
        if (isIpA && ipA.toIPv4Address() != ipB.toIPv4Address()) return ipA.toIPv4Address() < ipB.toIPv4Address();
        if (!isIpA && hostA != hostB) return hostA.compare(hostB, Qt::CaseInsensitive) < 0;
        return a < b;
    });
    return urls;
}

QString label(const QString &url, const QString &name)
{
    QUrl parsed(url);
    QString shown = isPlainHttps(parsed) ? parsed.host() : url;
    if (name.trimmed().isEmpty() || name.trimmed() == shown) return shown;
    return QString("%1 · %2").arg(name.trimmed(), shown);
}

} // namespace ProxyBrowser
