#ifndef SSHTUNNEL_H
#define SSHTUNNEL_H

#include <QString>
#include <QList>
#include <QJsonObject>
#include <QJsonArray>
#include <QMetaType>

// One SSH port forward, saved with its connection and added to the ssh
// command of the connection's terminal tab as -L, -R or -D
struct SSHTunnel {
    enum Type {
        Local,    // -L: a port on this PC leads to target host:port as seen from the server
        Remote,   // -R: a port on the server leads to target host:port as seen from this PC
        Dynamic   // -D: a SOCKS proxy on this PC; traffic goes out through the server
    };

    Type type;
    int listenPort;      // This PC's port for Local/Dynamic, the server's port for Remote
    QString targetHost;  // Not used for Dynamic
    int targetPort;      // Not used for Dynamic
    QString webPath;     // Local only: page "Open in browser" goes to, e.g. "/base-view"
    bool enabled;

    SSHTunnel() : type(Local), listenPort(8080), targetHost("localhost"), targetPort(80), enabled(true) {}

    bool operator==(const SSHTunnel &other) const {
        return type == other.type && listenPort == other.listenPort &&
               (type == Dynamic || (targetHost == other.targetHost && targetPort == other.targetPort)) &&
               webPath == other.webPath &&
               enabled == other.enabled;
    }

    // The port is opened on this PC (so it can collide with another program here)
    bool listensLocally() const { return type != Remote; }

    QString flag() const {
        switch (type) {
        case Remote:  return "-R";
        case Dynamic: return "-D";
        default:      return "-L";
        }
    }

    static QString typeName(Type t) {
        switch (t) {
        case Remote:  return "Remote (-R)";
        case Dynamic: return "SOCKS (-D)";
        default:      return "Local (-L)";
        }
    }

    // IPv6 addresses need brackets inside a forward spec
    static QString hostForSpec(const QString &host) {
        if (host.contains(':') && !host.startsWith('[')) return "[" + host + "]";
        return host;
    }

    // The argument after the flag, e.g. "8443:10.254.0.5:443" or "1080"
    QString spec() const {
        if (type == Dynamic) return QString::number(listenPort);
        return QString("%1:%2:%3").arg(listenPort).arg(hostForSpec(targetHost.trimmed())).arg(targetPort);
    }

    // Short human form, e.g. "localhost:8443 → 10.254.0.5:443"
    QString describe() const {
        switch (type) {
        case Dynamic:
            return QString("SOCKS proxy on localhost:%1").arg(listenPort);
        case Remote:
            return QString("server port %1 → %2:%3 (from this PC)").arg(listenPort).arg(targetHost).arg(targetPort);
        default:
            return QString("localhost:%1 → %2:%3 (from the server)").arg(listenPort).arg(targetHost).arg(targetPort);
        }
    }

    // The web path with a leading "/", or "/" when none is set
    QString urlPath() const {
        QString path = webPath.trimmed();
        if (path.isEmpty()) return "/";
        return path.startsWith('/') ? path : "/" + path;
    }

    // Web pages behind common TLS ports open as https, the rest as http
    bool looksLikeHttps() const {
        // 8081: BCM's CMDaemon, which serves Base View over https
        static const QList<int> httpsPorts = {443, 4443, 5001, 8006, 8081, 8443, 9443, 10443};
        return httpsPorts.contains(targetPort) || httpsPorts.contains(listenPort);
    }

    // An empty string when the tunnel can be used, otherwise what is wrong
    QString problem() const {
        if (listenPort < 1 || listenPort > 65535) return "the listen port must be 1-65535";
        if (type == Dynamic) return QString();
        QString host = targetHost.trimmed();
        if (host.isEmpty()) return "the destination host is empty";
        for (const QChar &ch : host) {
            if (ch.isSpace() || QString("'\"`$\\;&|<>()/").contains(ch)) {
                return QString("the destination host \"%1\" is not a valid host name or IP address").arg(host);
            }
        }
        if (targetPort < 1 || targetPort > 65535) return "the destination port must be 1-65535";
        for (const QChar &ch : webPath.trimmed()) {
            if (ch.isSpace() || ch == '"' || ch == '<' || ch == '>') {
                return QString("the web path \"%1\" can't contain spaces, quotes or < >").arg(webPath.trimmed());
            }
        }
        return QString();
    }

    QJsonObject toJson() const {
        QJsonObject obj;
        obj["type"] = type == Remote ? "R" : type == Dynamic ? "D" : "L";
        obj["listenPort"] = listenPort;
        if (type != Dynamic) {
            obj["targetHost"] = targetHost;
            obj["targetPort"] = targetPort;
        }
        if (type == Local && !webPath.trimmed().isEmpty()) obj["webPath"] = webPath.trimmed();
        obj["enabled"] = enabled;
        return obj;
    }

    static SSHTunnel fromJson(const QJsonObject &obj) {
        SSHTunnel t;
        QString letter = obj["type"].toString("L");
        t.type = letter == "R" ? Remote : letter == "D" ? Dynamic : Local;
        t.listenPort = obj["listenPort"].toInt(0);
        t.targetHost = obj["targetHost"].toString(t.type == Dynamic ? QString() : QString("localhost"));
        t.targetPort = obj["targetPort"].toInt(0);
        t.webPath = t.type == Local ? obj["webPath"].toString() : QString();
        t.enabled = obj["enabled"].toBool(true);
        return t;
    }

    static QJsonArray listToJson(const QList<SSHTunnel> &tunnels) {
        QJsonArray array;
        for (const SSHTunnel &t : tunnels) array.append(t.toJson());
        return array;
    }

    static QList<SSHTunnel> listFromJson(const QJsonArray &array) {
        QList<SSHTunnel> tunnels;
        for (const QJsonValue &value : array) tunnels << fromJson(value.toObject());
        return tunnels;
    }
};

Q_DECLARE_METATYPE(SSHTunnel)

#endif // SSHTUNNEL_H
