
#ifndef SSHCONNECTION_H
#define SSHCONNECTION_H

#include <QString>
#include <QMetaType>

// Structure to hold connection data
struct SSHConnection {
    QString name;
    QString host;
    QString username;
    QString password;  // Added password field
    int port;
    QString folder;  // Which folder this connection belongs to
    
    SSHConnection() : port(22) {}
    SSHConnection(const QString &n, const QString &h, const QString &u, int p = 22, const QString &f = "", const QString &pass = "")
        : name(n), host(h), username(u), password(pass), port(p), folder(f) {}
    
    // Add equality operator for QList::indexOf()
    bool operator==(const SSHConnection &other) const {
        return name == other.name && 
               host == other.host && 
               username == other.username && 
               password == other.password &&
               port == other.port && 
               folder == other.folder;
    }
};

// Make SSHConnection available to QVariant system
Q_DECLARE_METATYPE(SSHConnection)

#endif // SSHCONNECTION_H