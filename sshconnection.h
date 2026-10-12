
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

    // SSH key authentication
    bool useKeyAuth;       // Enable key-based authentication options below
    QString identityFile;  // Private key file passed as "ssh -i" (optional)
    bool useAgent;         // Allow keys from the running ssh-agent
    
    SSHConnection() : port(22), useKeyAuth(false), useAgent(true) {}
    SSHConnection(const QString &n, const QString &h, const QString &u, int p = 22, const QString &f = "", const QString &pass = "")
        : name(n), host(h), username(u), password(pass), port(p), folder(f),
          useKeyAuth(false), useAgent(true) {}
    
    // Add equality operator for QList::indexOf()
    bool operator==(const SSHConnection &other) const {
        return name == other.name && 
               host == other.host && 
               username == other.username && 
               password == other.password &&
               port == other.port && 
               folder == other.folder &&
               useKeyAuth == other.useKeyAuth &&
               identityFile == other.identityFile &&
               useAgent == other.useAgent;
    }
};

// Make SSHConnection available to QVariant system
Q_DECLARE_METATYPE(SSHConnection)

#endif // SSHCONNECTION_H