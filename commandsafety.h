#ifndef COMMANDSAFETY_H
#define COMMANDSAFETY_H

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
    
    // Build safe SSH command
    static QString buildSafeSSHCommand(const SSHConnection &connection, 
                                      const QString &remoteCommand = QString()) {
        QString cmd;
        
        if (!connection.password.isEmpty()) {
            cmd = QString("sshpass -p %1 ").arg(escapeShellArgument(connection.password));
        }
        
        cmd += QString("ssh -o ServerAliveInterval=60 -o ServerAliveCountMax=3 "
                      "-o StrictHostKeyChecking=accept-new ");
        
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