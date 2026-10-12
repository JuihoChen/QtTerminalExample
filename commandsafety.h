#ifndef COMMANDSAFETY_H
#define COMMANDSAFETY_H

#include <QDir>
#include <QString>
#include "sshconnection.h"

class CommandSafetyHelper {
public:
    // Safely escape arguments for shell commands - Qt version compatible
    static QString escapeShellArgument(const QString &argument) {
        if (argument.isEmpty()) {
            return "''";
        }
        
        // Check if argument needs quoting
        bool needsQuoting = false;
        static const QString specialChars = " \t\n\r\"'`$\\|&;<>(){}[]?*~#";
        
        for (const QChar &ch : argument) {
            if (specialChars.contains(ch)) {
                needsQuoting = true;
                break;
            }
        }
        
        if (!needsQuoting) {
            return argument;
        }
        
        // Escape single quotes and wrap in single quotes
        QString escaped = argument;
        escaped.replace("'", "'\"'\"'");
        return "'" + escaped + "'";
    }
    
    // Expand a leading "~/" so the path still works once it is quoted
    static QString expandHomePath(const QString &path) {
        QString trimmed = path.trimmed();
        if (trimmed == "~") {
            return QDir::homePath();
        }
        if (trimmed.startsWith("~/")) {
            return QDir::homePath() + trimmed.mid(1);
        }
        return trimmed;
    }

    // Prefix for password authentication (empty when no password is saved)
    static QString buildPasswordPrefix(const SSHConnection &connection) {
        if (connection.password.isEmpty()) {
            return QString();
        }
        return QString("sshpass -p %1 ").arg(escapeShellArgument(connection.password));
    }

    // Options shared by ssh and scp, including SSH key authentication
    static QString buildCommonOptions(const SSHConnection &connection) {
        QString opts("-o ServerAliveInterval=60 -o ServerAliveCountMax=3 "
                     "-o StrictHostKeyChecking=accept-new ");

        if (connection.useKeyAuth) {
            QString keyFile = expandHomePath(connection.identityFile);
            if (!keyFile.isEmpty()) {
                opts += QString("-i %1 ").arg(escapeShellArgument(keyFile));
            }
            if (!connection.useAgent) {
                // Ignore the ssh-agent; offer only the key file given above
                opts += "-o IdentityAgent=none ";
                if (!keyFile.isEmpty()) {
                    opts += "-o IdentitiesOnly=yes ";
                }
            }
        }

        return opts;
    }

    // Build safe SCP command (localToRemote: upload, otherwise download)
    static QString buildSafeSCPCommand(const SSHConnection &connection,
                                       const QString &localFile,
                                       const QString &remoteFile,
                                       bool localToRemote) {
        QString cmd = buildPasswordPrefix(connection);
        cmd += "scp " + buildCommonOptions(connection);

        if (connection.port != 22) {
            cmd += QString("-P %1 ").arg(connection.port);
        }

        QString remote = QString("%1@%2:%3").arg(
            escapeShellArgument(connection.username),
            escapeShellArgument(connection.host),
            escapeShellArgument(remoteFile));
        QString local = escapeShellArgument(localFile);

        cmd += localToRemote ? QString("%1 %2").arg(local, remote)
                             : QString("%1 %2").arg(remote, local);
        return cmd;
    }

    // Build safe SSH command
    static QString buildSafeSSHCommand(const SSHConnection &connection, 
                                      const QString &remoteCommand = QString()) {
        QString cmd = buildPasswordPrefix(connection);
        
        cmd += "ssh " + buildCommonOptions(connection);
        
        if (connection.port != 22) {
            cmd += QString("-p %1 ").arg(connection.port);
        }
        
        cmd += QString("%1@%2").arg(
            escapeShellArgument(connection.username),
            escapeShellArgument(connection.host)
        );
        
        if (!remoteCommand.isEmpty()) {
            cmd += QString(" %1").arg(escapeShellArgument(remoteCommand));
        }
        
        return cmd;
    }
};

#endif // COMMANDSAFETY_H