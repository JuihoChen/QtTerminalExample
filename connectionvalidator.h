#ifndef CONNECTIONVALIDATOR_H
#define CONNECTIONVALIDATOR_H

#include <QFileInfo>
#include <QString>
#include <QStringList>
#include "sshconnection.h"
#include "commandsafety.h"

class ConnectionValidator {
public:
    struct ValidationResult {
        bool isValid;
        QString errorMessage;
        QStringList warnings;
    };
    
    static ValidationResult validateConnection(const SSHConnection &connection) {
        ValidationResult result;
        result.isValid = true;
        
        // Basic validation
        if (connection.name.trimmed().isEmpty()) {
            result.isValid = false;
            result.errorMessage = "Connection name cannot be empty";
            return result;
        }
        
        if (connection.host.trimmed().isEmpty()) {
            result.isValid = false;
            result.errorMessage = "Host cannot be empty";
            return result;
        }
        
        if (connection.username.trimmed().isEmpty()) {
            result.isValid = false;
            result.errorMessage = "Username cannot be empty";
            return result;
        }
        
        if (connection.port < 1 || connection.port > 65535) {
            result.isValid = false;
            result.errorMessage = "Port must be between 1 and 65535";
            return result;
        }
        
        // Warnings for common issues
        if (connection.useKeyAuth) {
            QString keyFile = CommandSafetyHelper::expandHomePath(connection.identityFile);
            if (!keyFile.isEmpty() && !QFileInfo(keyFile).isReadable()) {
                result.warnings.append(QString("SSH key file not readable: %1").arg(keyFile));
            }
            if (keyFile.isEmpty() && !connection.useAgent) {
                result.warnings.append("SSH key authentication has no key file and the SSH agent is disabled");
            }
        } else if (connection.password.isEmpty()) {
            result.warnings.append("No password set - you'll need to enter it manually");
        }
        
        if (connection.port != 22) {
            result.warnings.append(QString("Using non-standard SSH port: %1").arg(connection.port));
        }
        
        return result;
    }
};

#endif // CONNECTIONVALIDATOR_H